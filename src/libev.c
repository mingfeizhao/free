#include <sys/types.h>          /* for socket/bind/listen() */
#include <sys/socket.h>
#include <arpa/inet.h>          /* for htons()/inet_pton() */
#include <linux/tcp.h>          /* for TCP_NODELAY */
#include "glb.h"
#include "libev.h"
#include "idle.h"
#include "log.h"

/* 维护连接的信息结构 */
typedef struct st_socket_conn_t {
    PKT pkt;            /* <TAKECARE!!!>必须做为第一个成员 */

    ev_io read;
    ev_io write;
    
    io_callback rcb;
    io_callback wcb;
    
    struct sockaddr_in ip;           /* 服务器角色，用于记录客户端地址；
                                        客户端角色，用于记录服务器地址 */
    struct st_socket_conn_t *next;
}SOCK_CONN;



static struct ev_loop *loop = NULL;  /* libev消息队列，用于注册监听插口fd及定时器 */
static SOCK_CONN *conn = NULL;       /* 维护socket连接 */
static ev_idle idle;                 /* 空闲事件观测器 */
static struct msghdr msg;            /* 报文信息结构体 */
static struct iovec iovec;           /* 报文缓存向量 */

#if 0
重要的数据结构注解
struct msghdr {
    void *msg_name;         /* protocol address, 
                               TCP或连接型UDP应该设置为NULL
                               recvmsg时由函数填充发送端地址 
                               sendmsg时手工填充接收目的地址*/
    socklen_t msg_namelen;  /* size of protocol address */
    struct iovec *msg_iov;  /* scatter/gather array */
    int msg_iovlen;         /* # elements in msg_iov */
    void *msg_control;      /* ancillary data (cmsghdr struct) */
    socklen_t msg_controllen;   /* length of ancillary data */
    int msg_flags;          /* flags returned by recvmsg() 
                               MSG_EOR         ends a logical record
                               MSG_OOB         带外数据, 被OSI使用
                               MSG_BCAST       是否为广播报文
                               MSG_MCAST       是否为多播报文
                               MSG_TRUNC       用户态buff不足导致截断
                               MSG_CTRUNC      ancillary data被截断
                               MSG_NOTIFICATION    被SCTP使用
                            */
};
struct iovec {
    void *iov_base;         /* starting address of buffer */
    size_t iov_len;         /* size of buffer */
};

recvmsg/sendmsg()的第三个参数flags:
MSG_DONTROUTE   [sendmsg]               直连网络, 不需要查找路由表
    MSG_DONTWAIT    [sendmsg + recvmsg]     不阻塞等待IO完成
    MSG_PEER        [recvmsg]               查看数据但系统不清空缓存
    MSG_WAITALL     [recvmsg]               收到了指定大小的报文后再返回
#endif



/**
 * 获取/释放空闲的链接信息结构
 *
 * FIXME：本进程耗费的内存只增加不减少；如果链接信息结构会成为内存瓶颈，
 *        考虑设置经验阈值，空闲内存过多时，释放回系统
 */
    static SOCK_CONN *get_conn()
{
    SOCK_CONN *tmp_conn = conn;
    if (conn) {
        conn = conn->next;
        tmp_conn->next = NULL;
    } else {
        MY_WARN("%s", "need MALLOC new SOCK_CONN");
        tmp_conn = (SOCK_CONN*)malloc(sizeof(SOCK_CONN));
        if (tmp_conn) {
            (void)memset(tmp_conn, 0, sizeof(SOCK_CONN));
        } else {
            MY_ERR("%s", strerror(errno));
        }
    }
    
    return tmp_conn;
}
static void free_conn(SOCK_CONN *cn)
{
    cn->next = conn;
    conn = cn;
}

static void clear_conn_res(SOCK_CONN *cn)
{
    if (cn == NULL) {
        MY_ERR("%s", "should NOT null");
        return;
    }
    close(cn->read.fd);
    close(cn->read.fd);
    ev_io_stop(EV_A_  &cn->read);
    ev_io_stop(EV_A_  &cn->write);
}

/* IDLE事件观测器的回调函数 */
static void idle_cb (EV_P_ ev_idle *w, int revents)
{
    (void)idle_main();
}


/**
 * 通用读函数
 * @param conn: 对应的网络层链路
 * @param ip: 需要记录的远端设备地址
 * @param close_conn_when_fin: 收到FIN后，是否关闭链接
 */
static void do_generic_read(SOCK_CONN* conn, bool store_remote_addr, bool close_conn_when_fin)
{
    int len;
    
    /* 接收数据前初始化 */
    if (store_remote_addr) {
        msg.msg_name = &conn->ip;
        msg.msg_namelen = sizeof(struct sockaddr_in);
    } else {
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
    }
    iovec.iov_base = conn->pkt.buff;
    iovec.iov_len = MSG_MAX_LEN;
    msg.msg_iov = &iovec;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;

    /* 接收数据 */
    len = recvmsg(conn->read.fd, &msg, 0);
    if (len < 0) {           /* 错误 */
        if (errno == EAGAIN
            || errno == EINTR
            || errno == EWOULDBLOCK) {
            MY_WARN("%s", strerror(errno));
        }
        return;
    } else if (len == 0) {   /* 收到FIN报文 */
        MY_WARN("%s", "recv FIN!!!");
        if (close_conn_when_fin) {
            clear_conn_res(conn);
        }
        return;
    }
    conn->pkt.len = len;

    /* 调用用户注册的回调 */
    conn->rcb(&conn->pkt);
}
static void do_generic_write(SOCK_CONN* conn, bool set_remote_addr)
{
    /* 发送数据前初始化 */
    if (set_remote_addr) {
        msg.msg_name = &conn->ip;
        msg.msg_namelen = sizeof(struct sockaddr_in);
    } else {
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
    }
    iovec.iov_base = conn->pkt.buff;
    iovec.iov_len = conn->pkt.len;
    msg.msg_iov = &iovec;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;

    /* 发送数据，尝试3次 */
    int count = 0;
    do {
        int len = sendmsg(conn->write.fd, &msg, MSG_DONTWAIT);
        if (len<0) {
            MY_ERR("%s", strerror(errno));
            break;
        } else {
            iovec.iov_base = conn->pkt.buff + len;
            iovec.iov_len = conn->pkt.len - len;
        }
    } while ((iovec.iov_len > 0) && (count++ < 3));
    if (count >= 3) {
        MY_ERR("%s, count[%d]", "send failed", count);
    }

    /* 关闭写事件 */
    ev_io_stop(EV_A_  &conn->write);

    /* 写成功后调用用户注册的回调 */
    if (conn->wcb) {
        conn->wcb(&conn->pkt);
    }
}

/* tcp链路EV_READ事件入口 */
static void do_tcp_read(EV_P_ ev_io *w, int revents)
{
    SOCK_CONN *tmp_conn = (SOCK_CONN *)w->data;

    if (revents & EV_ERROR) {
        MY_ERR("%s", "rece EV_ERROR");
        return;
    }

    do_generic_read(tmp_conn, false, true);
}

/* tcp链路EV_WRITE事件入口 */
static void do_tcp_write(EV_P_ ev_io *w, int revents)
{
    SOCK_CONN *tmp_conn = (SOCK_CONN *)w->data;
    
    if (revents & EV_ERROR) {
        MY_ERR("%s", "rece EV_ERROR");
        return;
    }
    
    do_generic_write(tmp_conn, false);
}

/**
 * TCP监听接口的处理函数，accept客户端，并将新fd加入事件循环
 */
static void do_accept(EV_P_ ev_io *w, int revents)
{
    SOCK_CONN *tmp_conn;
    int fd;

    if (revents & EV_ERROR) {
        MY_ERR("%s", "rece EV_ERROR");
        return;
    }
    tmp_conn = get_conn();
    if (tmp_conn == NULL) {
        MY_ERR("%s", "get conn failed");
        return;
    }
    
    int len = sizeof(tmp_conn->ip);
    fd = accept(w->fd, (struct sockaddr*)&tmp_conn->ip, (socklen_t *)&len);
    if (fd == -1) {
        MY_ERR("%s", strerror(errno));
        free_conn(tmp_conn);
        return;
    }

    /* 继承回调函数指针 */
    tmp_conn->rcb = ((SOCK_CONN*)(w->data))->rcb;
    tmp_conn->wcb = ((SOCK_CONN*)(w->data))->wcb;
    tmp_conn->read.data = tmp_conn;
    tmp_conn->write.data = tmp_conn;

    ev_io_init(&tmp_conn->read, do_tcp_read, fd, EV_READ);
    ev_io_init(&tmp_conn->write, do_tcp_write, fd, EV_WRITE);
    ev_io_start(EV_A_  &tmp_conn->read);
}

/* UDP链路EV_READ事件入口 */
static void do_udp_read(EV_P_ ev_io *w, int revents)
{
    SOCK_CONN *tmp_conn = (SOCK_CONN *)w->data;

    if (revents & EV_ERROR) {
        MY_ERR("%s", "rece EV_ERROR");
        return;
    }

    do_generic_read(tmp_conn, true, false);
}
static void do_cudp_read(EV_P_ ev_io *w, int revents)
{
    SOCK_CONN *tmp_conn = (SOCK_CONN *)w->data;
    struct sockaddr_in sip;
    
    if (revents & EV_ERROR) {
        MY_ERR("%s", "rece EV_ERROR");
        return;
    }

    /* 做为UDP客户端时，收到的远端IP地址可能不是对应的服务器IP地址；因此
       此处先保存此链接的服务器IP地址，接收数据报文后再恢复。

       做为UDP服务器时，不存在此问题，必须保存远端IP地址，以便回应时使用 */
    /* FIXME：如果收到的报文非原链接的服务器IP地址，报文应该直接丢弃!!! */
    (void)memcpy(&sip, &tmp_conn->ip, sizeof(struct sockaddr_in));
    do_generic_read(tmp_conn, true, false);
    (void)memcpy(&tmp_conn->ip, &sip, sizeof(struct sockaddr_in));
}

/* UDP链路EV_WRITE事件入口 */
static void do_udp_write(EV_P_ ev_io *w, int revents)
{
    SOCK_CONN *tmp_conn = (SOCK_CONN *)w->data;

    if (revents & EV_ERROR) {
        MY_ERR("%s", "rece EV_ERROR");
        return;
    }

    do_generic_write(tmp_conn, true);
}

/* 宏：获取链接结构，并初始化；初始化服务器IP；*/
#define INIT_CONN_AND_SIP do {                              \
        tmp_conn = get_conn();                              \
        if (tmp_conn == NULL) {                             \
            goto JUST_RET;                                  \
        }                                                   \
        tmp_conn->rcb = rcb;                                \
        tmp_conn->wcb = wcb;                                \
        tmp_conn->read.data = tmp_conn;                     \
        tmp_conn->write.data = tmp_conn;                    \
                                                            \
        (void)memset(&sip, 0, sizeof(struct sockaddr_in));   \
        sip.sin_family = AF_INET;                          \
        sip.sin_port = htons(port);                        \
        if(inet_pton(AF_INET, ip, &sip.sin_addr) <= 0) {   \
            goto FREE;                                      \
        }                                                   \
    } while(0);



int watch_tcp(char *ip, unsigned short port, io_callback rcb, io_callback wcb)
{
    struct sockaddr_in sip;
    SOCK_CONN *tmp_conn = NULL;
    int fd;

    INIT_CONN_AND_SIP;
    
    fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd == -1) {
        MY_ERR("%s", strerror(errno));
        goto FREE;
    }
    
    int flag = 1;                  /* 设置地址重用，非阻塞模式 */
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == -1) {
        MY_ERR("%s", strerror(errno));
        goto CLOSEFD;        
    }

    if (bind(fd, (const struct sockaddr *)&sip, sizeof(struct sockaddr_in)) == -1) {
        MY_ERR("%s", strerror(errno));
        goto CLOSEFD;
    }

    if (listen(fd, 512) == -1) {   /* magic 512: linux系统推荐值 */
        MY_ERR("%s", strerror(errno));
        goto CLOSEFD;
    }
    
    ev_io_init(&tmp_conn->read, do_accept, fd, EV_READ);
    ev_io_start(EV_A_  &tmp_conn->read);

    return RET_OK;
CLOSEFD:
    close(fd);
FREE:
    free_conn(tmp_conn);
JUST_RET:
    return RET_ERR;
}

int watch_udp(char *ip, unsigned short port, io_callback rcb, io_callback wcb)
{
    struct sockaddr_in sip;
    SOCK_CONN *tmp_conn = NULL;
    int fd;

    INIT_CONN_AND_SIP;
    
    fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd == -1) {
        MY_ERR("%s", strerror(errno));
        goto FREE;
    }
    
    int flag = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    if (bind(fd, (const struct sockaddr *)&sip, sizeof(struct sockaddr_in)) == -1) {
        MY_ERR("%s", strerror(errno));        
        goto CLOSEFD;
    }

    ev_io_init(&tmp_conn->read, do_udp_read, fd, EV_READ);
    ev_io_init(&tmp_conn->write, do_udp_write, fd, EV_WRITE);
    ev_io_start(EV_A_  &tmp_conn->read);

    return RET_OK;
CLOSEFD:
    close(fd);
FREE:
    free_conn(tmp_conn);
JUST_RET:
    return RET_ERR;
}

PKT *alloc_tcp(char *ip, unsigned short port, io_callback rcb, io_callback wcb)
{
    struct sockaddr_in sip;
    SOCK_CONN *tmp_conn = NULL;
    int fd;
    
    INIT_CONN_AND_SIP;
    
    fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd == -1) {
        MY_ERR("%s", strerror(errno));
        goto FREE;
    }

    int flag = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == -1) {
        MY_ERR("%s", strerror(errno));
        goto CLOSEFD;        
    }
    
    if (connect(fd, (const struct sockaddr *)&sip, sizeof(struct sockaddr_in)) == -1) {
        if (errno != EINPROGRESS) {
            MY_ERR("%s", strerror(errno));
            goto CLOSEFD;
        }
    }

    ev_io_init(&tmp_conn->read, do_tcp_read, fd, EV_READ);
    ev_io_init(&tmp_conn->write, do_tcp_write, fd, EV_WRITE);
    ev_io_start(EV_A_  &tmp_conn->read);

    goto JUST_RET;
CLOSEFD:
    close(fd);
FREE:
    free_conn(tmp_conn);
    tmp_conn = NULL;
JUST_RET:
    return (tmp_conn?&(tmp_conn->pkt):NULL);
}

PKT *alloc_udp(char *ip, unsigned short port, io_callback rcb, io_callback wcb)
{
    SOCK_CONN *tmp_conn = NULL;
    struct sockaddr_in sip;
    int fd;
    
    INIT_CONN_AND_SIP;
    (void)memcpy(&tmp_conn->ip, &sip, sizeof(struct sockaddr_in));
    
    fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd == -1) {
        MY_ERR("%s", strerror(errno));
        goto FREE;
    }

    ev_io_init(&tmp_conn->read, do_cudp_read, fd, EV_READ);
    ev_io_init(&tmp_conn->write, do_udp_write, fd, EV_WRITE);
    ev_io_start(EV_A_  &tmp_conn->read);

    goto JUST_RET;
FREE:
    free_conn(tmp_conn);
    tmp_conn = NULL;
JUST_RET:
    return (tmp_conn?&(tmp_conn->pkt):NULL);
}

int send_pkt(PKT *pkt)
{
    SOCK_CONN *tmp_conn = (SOCK_CONN*)pkt;
    ev_io_start(EV_A_  &tmp_conn->write);
    
    return RET_OK;
}

int close_pkt(PKT *pkt)
{
    SOCK_CONN *tmp_conn = (SOCK_CONN*)pkt;
    clear_conn_res(tmp_conn);
    
    return RET_OK;
}


int libev_init()
{
    /* 当对端read插口关闭时，write()会触发SIGPIPE信号； 如果屏蔽掉，
     则会返回错误EPIPE，被错误判断捕捉；如果不屏蔽，则默认导致进
     程退出 */
    signal(SIGPIPE, SIG_IGN);

    /* 初始化事件循环 */
    loop = EV_DEFAULT;

    /* 预分配链接结构; magic 100: 预分配结构数量 */
    int conn_num = 100;
    conn = (SOCK_CONN*)malloc(conn_num * sizeof(SOCK_CONN));
    (void)memset(conn, 0, conn_num * sizeof(SOCK_CONN));
    for (int i=0; i<conn_num-1; i++) {
        conn[i].next = &conn[i+1];
    }
    conn[conn_num-1].next = NULL;

    return RET_OK;
}


void event_loop(void)
{
    /* 注册idle事件 */
    ev_idle_init (&idle, idle_cb);
    ev_idle_start (EV_A_ &idle);

    /* 开启libev事件循环 */
    ev_run(EV_A_ 0);
}

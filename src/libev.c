#include <sys/types.h>          /* for socket/bind/listen() */
#include <sys/socket.h>
#include <arpa/inet.h>          /* for htons()/inet_pton() */
#include <linux/tcp.h>          /* for TCP_NODELAY */
#include "glb.h"
#include "libev.h"
#include "idle.h"

/* 维护连接的信息结构 */
typedef struct st_socket_conn_t {
    PKT pkt;            /* <TAKECARE!!!>必须做为第一个成员 */

    ev_io read;
    ev_io write;
    
    io_callback rcb;
    io_callback wcb;
    
    struct sockaddr_in cip;          /* 记录客户端地址 */
    
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
        tmp_conn = (SOCK_CONN*)malloc(sizeof(SOCK_CONN));
        if (tmp_conn) {
            (void)memset(tmp_conn, 0, sizeof(SOCK_CONN));
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

static void do_tcp_read(EV_P_ ev_io *w, int revents)
{
    SOCK_CONN *tmp_conn = (SOCK_CONN *)w->data;
    int len;

    printf("%s\n", __func__);
    if (revents & EV_ERROR) {
        printf("%s/%s/%d\n", __FILE__, __func__, __LINE__);
        return;
    }

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    iovec.iov_base = tmp_conn->pkt.buff;
    iovec.iov_len = MSG_MAX_LEN;
    msg.msg_iov = &iovec;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    
    len = recvmsg(w->fd, &msg, 0);
    if (len < 0) {
        if (errno == EAGAIN
            || errno == EINTR
            || errno == EWOULDBLOCK) {
            printf("%s/%s/%d\n", __FILE__, __func__, __LINE__);
            /* do nothing */
        }
        return;
    } else if (len == 0) {
        printf("%s/%s/%d\n", __FILE__, __func__, __LINE__);
        clear_conn_res(tmp_conn);
        return;
    }

    tmp_conn->pkt.len = len;
    tmp_conn->rcb(&tmp_conn->pkt);
}

static void do_tcp_write(EV_P_ ev_io *w, int revents)
{
    SOCK_CONN *tmp_conn = (SOCK_CONN *)w->data;
    
    printf("%s\n", __func__);
    if (revents & EV_ERROR) {
        printf("%s/%s/%d\n", __FILE__, __func__, __LINE__);
        return;
    }
    
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    iovec.iov_base = tmp_conn->pkt.buff;
    iovec.iov_len = tmp_conn->pkt.len;
    msg.msg_iov = &iovec;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;

    /* FIXME：发送成功判断？？？ */
    int len = sendmsg(w->fd, &msg, MSG_DONTWAIT);
    printf("server send %d\n", len);
    ev_io_stop(EV_A_  &tmp_conn->write);
    
    if (tmp_conn->wcb) {
        tmp_conn->wcb(&tmp_conn->pkt);
    }
}

/**
 * TCP监听接口的处理函数，accept客户端，并将新fd加入事件循环
 */
static void do_accept(EV_P_ ev_io *w, int revents)
{
    SOCK_CONN *tmp_conn;
    int len;
    int fd;

    printf("%s\n", __func__);
    if (revents & EV_ERROR) {
        printf("%s/%s/%d\n", __FILE__, __func__, __LINE__);
        return;
    }
    tmp_conn = get_conn();
    if (tmp_conn == NULL) {
        printf("%s/%s/%d\n", __FILE__, __func__, __LINE__);
        return;
    }
    
    len = sizeof(tmp_conn->cip);
    fd = accept(w->fd, (struct sockaddr*)&tmp_conn->cip, (socklen_t *)&len);
    if (fd == -1) {
        printf("%s/%s/%d\n", __FILE__, __func__, __LINE__);
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

/**
 * UDP监听接口的读处理函数，记录客户端信息，并读取数据；
 * 数据读取完毕后，调用注册的处理回调
 */
static void do_udp_read(EV_P_ ev_io *w, int revents)
{
    SOCK_CONN *tmp_conn = (SOCK_CONN *)w->data;
    int len;

    if (revents & EV_ERROR) {
        return;
    }
    
    /* 接收数据 */
    msg.msg_name = &tmp_conn->cip;
    msg.msg_namelen = sizeof(tmp_conn->cip);
    iovec.iov_base = tmp_conn->pkt.buff;
    iovec.iov_len = MSG_MAX_LEN;
    msg.msg_iov = &iovec;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    
    len = recvmsg(w->fd, &msg, 0);
    if (len < 0) {          /* 错误 */
        if (errno == EAGAIN
            || errno == EINTR
            || errno == EWOULDBLOCK) {
            /* do nothing */
        }
        return;
    } else if (len == 0) {  /* 收到了FIN??? */
        return;
    }

    /* 处理数据 */
    tmp_conn->pkt.len = len;
    tmp_conn->pkt.buff[len] = 0;
    tmp_conn->rcb(&tmp_conn->pkt);
}

/**
 * UDP监听接口的写处理函数, 回应客户端
 */
static void do_udp_write(EV_P_ ev_io *w, int revents)
{
    SOCK_CONN *tmp_conn = (SOCK_CONN *)w->data;

    if (revents & EV_ERROR) {
        return;
    }
    
    msg.msg_name = &tmp_conn->cip;
    msg.msg_namelen = sizeof(tmp_conn->cip);
    iovec.iov_base = tmp_conn->pkt.buff;
    iovec.iov_len = tmp_conn->pkt.len;
    msg.msg_iov = &iovec;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;

    /* FIXME：发送成功判断？？？ */
    (void)sendmsg(w->fd, &msg, MSG_DONTWAIT);
    ev_io_stop(EV_A_  &tmp_conn->write);
    
    if (tmp_conn->wcb) {
        tmp_conn->wcb(&tmp_conn->pkt);
    }
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

int watch_tcp(char *ip, unsigned short port, io_callback rcb, io_callback wcb)
{
    struct sockaddr_in sip;
    SOCK_CONN *tmp_conn = NULL;
    int fd;
    int ret = RET_ERR;
    
    tmp_conn = get_conn();
    if (tmp_conn == NULL) {
        goto TCP_RET;
    }
    tmp_conn->rcb = rcb;
    tmp_conn->wcb = wcb;
    tmp_conn->read.data = tmp_conn;
    tmp_conn->write.data = tmp_conn;

    (void)memset(&sip, 0, sizeof(sip));
    sip.sin_family = AF_INET;
    sip.sin_port = htons(port);
    if(inet_pton(AF_INET, ip, &sip.sin_addr) <= 0) {
        goto TCP_FREE;
    }
    
    fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd == -1) {
        goto TCP_FREE;
    }
    
    /* 设置地址重用，非阻塞模式 */
    int flag = 1;
    if((setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag))) == -1) {
        /* do nothing */
    }
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == -1) {
        goto TCP_CLOSEFD;        
    }

    if (bind(fd, (const struct sockaddr *)&sip, sizeof(sip)) == -1) {
        goto TCP_CLOSEFD;
    }

    if (listen(fd, 512) == -1) {   /* magic 512: linux系统推荐值 */
        goto TCP_CLOSEFD;
    }

    
    ev_io_init(&tmp_conn->read, do_accept, fd, EV_READ);
    ev_io_start(EV_A_  &tmp_conn->read);

    ret = RET_OK;
    goto TCP_RET;
TCP_CLOSEFD:
    close(fd);
TCP_FREE:
    free_conn(tmp_conn);
TCP_RET:
    return ret;
}

int watch_udp(char *ip, unsigned short port, io_callback rcb, io_callback wcb)
{
    struct sockaddr_in sip;
    SOCK_CONN *tmp_conn = NULL;
    int fd;
    int ret = RET_ERR;
    
    tmp_conn = get_conn();
    if (tmp_conn == NULL) {
        goto UDP_RET;
    }
    tmp_conn->rcb = rcb;
    tmp_conn->wcb = wcb;
    tmp_conn->read.data = tmp_conn;
    tmp_conn->write.data = tmp_conn;
    
    (void)memset(&sip, 0, sizeof(sip));
    sip.sin_family = AF_INET;
    sip.sin_port = htons(port);
    if(inet_pton(AF_INET, ip, &sip.sin_addr) <= 0) {
        goto UDP_FREE;
    }
    
    fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd == -1) {
        goto UDP_FREE;
    }
    
    /* 设置地址重用，非阻塞模式 */
    int flag = 1;
    if((setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag))) == -1) {
        /* do nothing */
    }
    if (bind(fd, (const struct sockaddr *)&sip, sizeof(sip)) == -1) {
        goto UDP_CLOSEFD;
    }


    /* 加入并启动事件循环，仅监控读事件 */
    ev_io_init(&tmp_conn->read, do_udp_read, fd, EV_READ);
    ev_io_init(&tmp_conn->write, do_udp_write, fd, EV_WRITE);
    ev_io_start(EV_A_  &tmp_conn->read);

    ret = RET_OK;
    goto UDP_RET;
UDP_CLOSEFD:
    close(fd);
UDP_FREE:
    free_conn(tmp_conn);
UDP_RET:
    return ret;
}

PKT *alloc_tcp(char *ip, unsigned short port, io_callback rcb, io_callback wcb)
{
    return NULL;
}

PKT *alloc_udp(char *ip, unsigned short port, io_callback rcb, io_callback wcb)
{
    return NULL;
}

int send_pkt(PKT *pkt)
{
    printf("%s\n", __func__);
    SOCK_CONN *tmp_conn = (SOCK_CONN*)pkt;
    ev_io_start(EV_A_  &tmp_conn->write);
    
    return RET_OK;
}

int close_pkt(PKT *pkt)
{
    SOCK_CONN *tmp_conn = (SOCK_CONN*)pkt;

    printf("%s\n", __func__);
    clear_conn_res(tmp_conn);
    
    return RET_OK;
}

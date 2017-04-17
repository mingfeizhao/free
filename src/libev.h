#ifndef _LIBEV_H_
#define _LIBEV_H_

/* 
设计：
1) 支持UDP、TCP监听接口，watch_tcp/watch_udp()
2) 支持UDP、TCP客户端，alloc_tcp/alloc_udp()
3) 支持服务器应答、支持客户端主动发送，send_pkt()
4) 所有的报文处理必须无阻塞操作，延时操控可通过定时器实现
5) UDP服务器收取的报文不支持延时应答
6) TCP、UDP的读取通道维持打开，注意可重入性；
7) 而发送通道，每调用一次send_pkt()打开一次；
 */

#include "ev.h"          /* libev库头文件 */

/* 报文缓存 */
typedef struct st_pkt_t {
    char buff[MSG_MAX_LEN];
    int len;     /* 读取时，接收长度；发送时，已发送长度 */
}PKT;

/**
 * socket插口事件的回调函数
 * @param pkt: 接收到/已发送的数据报文
 */
typedef void (*io_callback)(PKT *pkt);

/**
 * 初始化libev环境
 */
int libev_init(void);

/**
 * 事件循环
 *
 * FIXME：当前采用单进程模式，如有需要后续采用多进程模式，届时此函数可去掉
 */
void event_loop(void);

/**
 * 注册TCP/UDP监控插口
 * @param ip: 待监控的IP地址
 * @param port: 待监控的端口
 * @param rcb: 接收到数据报文后，调用此函数
 * @param wcb: 发送数据包文后，调用此函数
 *
 * <NOTE>
 *   1) 如果不关注发送结果，wcb可以设置为NULL
 */
int watch_tcp(char *ip, unsigned short port, io_callback rcb, io_callback wcb);
int watch_udp(char *ip, unsigned short port, io_callback rcb, io_callback wcb);

/**
 * 做为客户端，申请向其他服务器通信的数据包
 */
PKT *alloc_tcp(char *ip, unsigned short port, io_callback rcb, io_callback wcb);
PKT *alloc_udp(char *ip, unsigned short port, io_callback rcb, io_callback wcb);

/**
 * 发送数据包
 */
int send_pkt(PKT *pkt);

/**
 * 关闭TCP链接
 */
int close_pkt(PKT *pkt);

#endif

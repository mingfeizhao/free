#ifndef _IDLE_H_
#define _IDLE_H_

/**
 * 空闲函数，当每次事件循环中没有待处理的注册事件时被调用；此函数
 * 可用于统计输出、状态监控等低级非紧急事件。
 */
void idle_main(void);


#endif

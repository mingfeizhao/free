#include "glb.h"
#include "log.h"
#include "redis.h"
#include "libev.h"

void rtcp_pkt(PKT *pkt)
{
    printf("tcp recv: %s\n", pkt->buff);
    send_pkt(pkt);
}
void wtcp_pkt(PKT *pkt)
{
    printf("%s\n", __func__);
    //close_pkt(pkt);
}
void rudp_pkt(PKT *pkt)
{
    printf("udp recv: %s\n", pkt->buff);
    send_pkt(pkt);
}


int main(int argc, char** argv)
{
    /* 各模块儿初始化 */
    if (log_init() != RET_OK) {
        exit(EXIT_FAILURE);
    }
    if (libev_init() != RET_OK) {
        exit(EXIT_FAILURE);
    }

    /* 注册待监控插口 */
    watch_tcp("127.0.0.1", 50000, rtcp_pkt, wtcp_pkt);
    watch_udp("127.0.0.1", 50000, rudp_pkt, NULL);

    /* 开启libev事件循环 */
    event_loop();

    
    while (1) {
        sleep(3);
        
        printf("%d\n", store_str("test1", "val1"));
        printf("%d\n", store_binary("test2", "val2", 4));
        printf("%d\n", store_str_by_hash("hash", "test3", "val3"));
        printf("%d\n", store_binary_by_hash("hash", "test4", "val4", 4));
        
        char res[128] = {0};
        printf("%d\n", get_str("test1", res, 128));
        printf("test1=%s\n", res);
        printf("%d\n", get_binary("test2", res, 128));
        printf("test2=%s\n", res);
        printf("%d\n", get_str_by_hash("hash", "test3", res, 128));
        printf("test3=%s\n", res);
        printf("%d\n", get_binary_by_hash("hash", "test4", res, 128));
        printf("test4=%s\n", res);

        printf("%d\n", del_key("test1"));
        printf("%d\n", del_key("test2"));
        printf("%d\n", del_key_by_hash("hash", "test3"));
        printf("%d\n", del_key_by_hash("hash", "test4"));

        printf("%d\n", get_str("test1", res, 128));
        printf("%d\n", get_binary("test2", res, 128));
        printf("%d\n", get_str_by_hash("hash", "test3", res, 128));
        printf("%d\n", get_binary_by_hash("hash", "test4", res, 128));
    }
    
    return 0;
}


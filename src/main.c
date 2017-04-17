#include "glb.h"
#include "log.h"
#include "redis.h"
#include "libev.h"

/* <TAKECARE!!!>仅用于开发阶段，调试运行时，请删除此宏包围的代码 */
#define TEST_REDIS 0
#define TEST_LIBEV_SEV 1

#if TEST_LIBEV_SEV
/* 测试libev框架服务器封装 */
void sev_rtcp_pkt(PKT *pkt)
{
    printf("tcp SERVER recv: %s\n", pkt->buff);
    send_pkt(pkt);
}
void sev_wtcp_pkt(PKT *pkt)
{
    printf("%s\n", __func__);
}
void sev_rudp_pkt(PKT *pkt)
{
    printf("udp SERVER recv: %s\n", pkt->buff);
    send_pkt(pkt);
}
#endif

#if TEST_REDIS
/* 测试redis封装接口 */
void test_redis_encap()
{
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
#endif


int main(int argc, char** argv)
{
    /* 各模块儿初始化 */
    if (log_init() != RET_OK) {
        exit(EXIT_FAILURE);
    }
    if (libev_init() != RET_OK) {
        exit(EXIT_FAILURE);
    }

#if TEST_REDIS
    test_redis_encap();
#endif
    
#if TEST_LIBEV_SEV
    watch_tcp("127.0.0.1", 50000, sev_rtcp_pkt, sev_wtcp_pkt);
    watch_udp("127.0.0.1", 50000, sev_rudp_pkt, NULL);
#endif

    /* 开启libev事件循环 */
    event_loop();
    
    
    return 0;
}



#include "glb.h"
#include "idle.h"

static int count = 0;

/* <TAKECARE!!!>仅用于开发阶段，调试运行时，请删除此宏包围的代码 */
#define TEST_LIBEV_CLI 1

#if TEST_LIBEV_CLI
#include "libev.h"

void cli_r_pkt(PKT *pkt)
{
    printf("CLI recv: %s\n", pkt->buff);
    close_pkt(pkt);
}
void cli_w_pkt(PKT *pkt)
{
    printf("%s\n", __func__);
}
#endif

void idle_main()
{
    count++;
    if (count / 30000000) {
        printf("in IDLE\n");
        count = 0;

#if TEST_LIBEV_CLI
        PKT *udp_pkt = alloc_udp("127.0.0.1", 50000, cli_r_pkt, cli_w_pkt);
        udp_pkt->len = snprintf(udp_pkt->buff, sizeof(udp_pkt->buff), "%s", "JUST TEST UDP CLIENT");
        printf("%d/%s\n", udp_pkt->len, udp_pkt->buff);
        send_pkt(udp_pkt);

        PKT *tcp_pkt = alloc_tcp("127.0.0.1", 50000, cli_r_pkt, cli_w_pkt);
        tcp_pkt->len = snprintf(tcp_pkt->buff, sizeof(tcp_pkt->buff), "%s", "JUST TEST TCP CLIENT");
        printf("%d/%s\n", tcp_pkt->len, tcp_pkt->buff);
        send_pkt(tcp_pkt);
#endif
    }
}







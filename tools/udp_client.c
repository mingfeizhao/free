#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>


int main(int argc, char **argv)
{
    int fd;
    const char *ip = "127.0.0.1";
    unsigned short port = 50000;
    struct sockaddr_in sip;
    const char *buff = "hello, world";

    fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd<0) {
        printf("%s/%d: %s\n", __FILE__, __LINE__, strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    (void)memset(&sip, 0, sizeof(sip));
    sip.sin_family = AF_INET;
    sip.sin_port = htons(port);
    if(inet_pton(AF_INET, ip, &sip.sin_addr) <= 0) {
        printf("%s/%d: %s\n", __FILE__, __LINE__, strerror(errno));
        exit(EXIT_FAILURE);
    }

    while (1) {
        int len = sendto(fd, buff, strlen(buff), 0, (struct sockaddr*)&sip, sizeof(sip));
        printf("send %d\n", len);
        
        char rec_buff[64] = {0};
        len = recv(fd, rec_buff, sizeof(rec_buff), 0);
        if (len == 0) {
            printf("recv FIN\n");
            break;
        } else {
            printf("recv msg, %d/%s\n", len, rec_buff);
        }
        
        sleep(3);
    }
    
    close(fd);
    return 0;
}


















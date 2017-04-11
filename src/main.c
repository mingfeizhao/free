#include "glb.h"
#include "log.h"
#include "redis.h"

int main(int argc, char** argv)
{
    if(log_init() != RET_OK) {
        exit(EXIT_FAILURE);
    }


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


#include "glb.h"
#include "log.h"
#include "hiredis.h"
#include "redis.h"


static int REDIS_PORT = 6379;         /* redis默认端口 */
static char REDIS_IP[] = "127.0.0.1"; /* redis服务器IP */


/** 
 * FIXME：链接redis，前期采用每次请求独立请求链接的方式，以减少链接状态维护
 *        成本；如果性能不足，则需要修改为长链接！ */
static redisContext* connect_redis() 
{
    redisContext *c;
    struct timeval timeout = { 1, 500000 };    /* 超时时限，1.5s */

    c = redisConnectWithTimeout(REDIS_IP, REDIS_PORT, timeout);
    if (c == NULL || c->err) {
        if (c) {
            MY_ERR("conn redis error: [%s]", c->errstr);
            redisFree(c);
        } else {
            MY_ERR("can't allocate redis context");
        }

        return NULL;
    }

    return c;
}

/**
 * 辅助函数
 * @param res: 存放结果的字符数组
 * @param rlen: 字符数组长度
 *
 * ~/deps/redis/deps/hiredis/hiredis.c
 * 调用函数原型void *redisCommand(redisContext *c, const char *format, ...)
 */
static int base(char *res, size_t rlen, const char *format, ...)
{
    redisContext *c;
    redisReply *reply;
    va_list args;
    int ret = RET_ERR;

    /* 连接redis服务器，默认阻塞式(发送命令并等待结果回传) */
    c = connect_redis();
    if (c == NULL) {
        goto JRET;
    }

    /* 存储数据 */
    va_start(args, format);
    reply = redisvCommand(c, format, args);
    va_end(args);
    if (reply == NULL) {
        MY_ERR("%d/%s", c->err, c->errstr);
        goto DISCONN;
    } else if (reply->type == REDIS_REPLY_ERROR) {
        MY_ERR("%s", reply->str);
        goto FREERLY;
    }
    
    /* 处理返回数据 */
    ret = RET_OK;
    switch (reply->type) {
    case REDIS_REPLY_STRING:
        if (res) {
            snprintf(res, rlen, "%s", reply->str);
        }
        break;
    case REDIS_REPLY_NIL:
        if (res) {
            snprintf(res, rlen, "%s", "\0");
        }
        MY_WARN("NO reply!!!");
        break;
    case REDIS_REPLY_STATUS:
        if (strcasecmp(reply->str,"ok") != 0) {
            ret = RET_ERR;
            MY_ERR("reply status err, [%s]", reply->str);
        }
        break;
    }
    
FREERLY:
    freeReplyObject(reply);
DISCONN:
    redisFree(c);
JRET:
    return ret;
}

int store_str(char *field, char *val)
{
    if (field==NULL || val==NULL) {
        MY_ERR("param err, field[%s]/val[%s]", field?field:"NULL", val?val:"NULL");
        return RET_ERR;
    }
    return base(NULL, -1, "SET %s %s", field, val);
}
int get_str(char *field, char *val, size_t len)
{
    if (field==NULL || val==NULL || len<=0) {
        MY_ERR("param err, field[%s]/len[%d]", field?field:"NULL", (int)len);
        return RET_ERR;
    }
    return base(val, len, "GET %s", field);
}
int del_key(char *field)
{
    if (field==NULL) {
        MY_ERR("param err, field[%s]", field?field:"NULL");
        return RET_ERR;
    }
    return base(NULL, -1, "DEL %s", field);
}

int store_binary(char *field, void *val, size_t len)
{
    if (field==NULL || val==NULL || len<=0) {
        MY_ERR("param err, field[%s]/len[%d]", field?field:"NULL", (int)len);
        return RET_ERR;
    }
    return base(NULL, -1, "SET %s %b", field, val, len);
}
int get_binary(char *field, void *val, size_t len)
{
    return get_str(field, val, len);
}

int store_str_by_hash(char *hash, char *field, char *val)
{
    if (hash==NULL || field==NULL || val==NULL) { 
        MY_ERR("param err, hash[%s]/field[%s]/val[%s]", hash?hash:"NULL", field?field:"NULL", val?val:"NULL");
        return RET_ERR;
    }
    return base(NULL, -1, "HSET %s %s %s", hash, field, val);
}
int get_str_by_hash(char *hash, char *field, char *val, size_t len)
{
    if (hash==NULL || field==NULL || val==NULL || len<=0) {
        MY_ERR("param err, hash[%s]/field[%s]/len[%d]", hash?hash:"NULL", field?field:"NULL", (int)len);
        return RET_ERR;
    }
    return base(val, len, "HGET %s %s", hash, field);
}
int del_key_by_hash(char *hash, char *field)
{
    if (hash==NULL || field==NULL) {
        MY_ERR("param err, hash[%s]/field[%s]", hash?hash:"NULL", field?field:"NULL");
        return RET_ERR;
    }
    return base(NULL, -1, "HDEL %s %s", hash, field);
}

int store_binary_by_hash(char *hash, char *field, void *val, size_t len)
{
    if (hash==NULL || field==NULL || val==NULL || len<=0) {
        MY_ERR("param err, hash[%s]/field[%s]/len[%d]", hash?hash:"NULL", field?field:"NULL", (int)len);
        return RET_ERR;
    }
    return base(NULL, -1, "HSET %s %s %b", hash, field, val, len);
}
int get_binary_by_hash(char *hash, char *field, void *val, size_t len)
{
    return get_str_by_hash(hash, field, val ,len);
}













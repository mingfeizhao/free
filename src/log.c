#include "glb.h"
#include "log.h"


#define MAX_LOG_STR_LEN     1024  /* 单条日志的最大长度 */


/* 日志级别 */
static struct st_log_level {
    char name[32];
} log_level[] = {
    {"LOG_EMERG"},
    {"LOG_ALERT"},
    {"LOG_CRIT"},
    {"LOG_ERR"},
    {"LOG_WARNING"},
    {"LOG_NOTICE"},
    {"LOG_INFO"},
    {"LOG_DEBUG"},
    {""}
};


int log_init()
{
#ifdef DNS_DEBUG
    openlog("FREE", LOG_NDELAY|LOG_PID|LOG_PERROR, LOG_LOCAL0);
#else
    openlog("FREE", LOG_NDELAY|LOG_PID, LOG_LOCAL0);
#endif
    
    MY_DEBUG("log init OK!");
    return RET_OK;
}

void log_base(const char *file, const char *func, int line, 
              int level, const char *fmt, ...)
{
    char tmp_str[MAX_LOG_STR_LEN] = {0};
    va_list   args;

    va_start(args, fmt);
    vsnprintf(tmp_str, MAX_LOG_STR_LEN, fmt, args);
    va_end(args);

    syslog(level, "<%s><%s|%s|%d>: %s", log_level[level].name, file, func, line, tmp_str);
}





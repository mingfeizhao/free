#ifndef _LOG_H_
#define _LOG_H_

#include <syslog.h>

/**
 * 罗列宏操控示例, 储备!!!
 *
 * 示例一:
 *  #define STRING(x)  #x
 *  char *pChar = "hello";      ==>     char *pChar = STRING(hello);
 *
 * 示例二:
 *  #define makechar(x) #@x
 *  char ch = makechar(b);      ==>     char ch = 'b';
 *
 * 示例三:
 * #define link(n)  token##n
 * int link(9)  = 100;          ==>     int token9   = 100;
 *
 * <NOTE>嵌套宏遇到##或#不再展开的问题
 *  #define STRING(x)   #x
 *  char *pChar = STRING(__FILE__); ==> char *pChar = "__FILE__";
 *
 *  引入中间宏, 展开外层的宏
 *  #define _STRING(x)  #x
 *  #define STRING(x)   _STRING(x) 
 *  char *pChar = STRING(__FILE__); ==> char *pChar = "\"/usr/.../test.c\"";
 */

/**
 * 日志初始化
 */
int log_init(void);

/**
 * 日志辅助函数
 * @param file: 代码文件
 * @param func: 代码函数
 * @param line: 代码行
 * @param level: 日志级别
 * @param fmt: 格式字符串, 同printf
 */
void log_base(const char *file, const char *func, int line, 
        int level, const char *fmt, ...) 
    __attribute__((format (printf, 5, 6)));

/**
 * 日志输出宏
 */
#define MY_ERR(fmt, ...) do{\
    log_base(__FILE__, __func__, __LINE__, LOG_ERR, fmt, ##__VA_ARGS__);\
}while(0);
#define MY_WARN(fmt, ...) do{\
    log_base(__FILE__, __func__, __LINE__, LOG_WARNING, fmt, ##__VA_ARGS__);\
}while(0);
#define MY_DEBUG(fmt, ...) do{\
    log_base(__FILE__, __func__, __LINE__, LOG_DEBUG, fmt, ##__VA_ARGS__);\
}while(0);


#endif

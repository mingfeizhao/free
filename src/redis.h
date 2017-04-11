#ifndef _REDIS_H_
#define _REDIS_H_


/**
 * 以KEY-VALUE对形式，存储字符串或二进制数据
 * @param field: 索引键值
 * @param val: 待存储值
 * @param len: 二进制数据段长度
 * @ret: 0/errno, 成功/失败
 */
int store_str(char *field, char *val);
int store_binary(char *field, void *val, size_t len);
/**
 * 从存储系统获取对应的字符串或二进制数据
 * @param len: 内存长度
 *
 * <NOTE>
 *   1) val由调用者提供，以增加可控性，同时使得此函数可重入
 */
int get_str(char *field, char *val, size_t len);
int get_binary(char *field, void *val, size_t len);
/**
 * 删除特定键
 */
int del_key(char *field);
/**
 * 存储字符串或二进制数据，到hash数组
 * @param hash: 哈希表名
 * @ret: 0/errno, 成功/失败
 */
int store_str_by_hash(char *hash, char *field, char *val);
int store_binary_by_hash(char *hash, char *field, void *val, size_t len);
int get_str_by_hash(char *hash, char *field, char *val, size_t len);
int get_binary_by_hash(char *hash, char *field, void *val, size_t len);
int del_key_by_hash(char *hash, char *field);


#endif

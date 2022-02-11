#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <time.h>
#include <pthread.h>

#define DEFAULT_MAP_NUM 128
#define DEFAULT_MAP_INIT_LEN 1024
#define DEFAULT_CACHE_SECONDS 60
#define DEFAULT_SWEEP_SECONDS 150
#define DEFAULT_EXPAND_SECONDS 300

// hasher_t 自定义哈希函数
typedef uint32_t (*hasher_t)(const char* key, uint32_t max_len);

// expander_t 自定义扩容函数
typedef bool (*expander_t)(uint32_t length, uint32_t size);

// cache_node_t 哈希表上的节点
typedef struct _cache_node {
    char* key;                // 缓存 key
    void* data;               // 缓存内容
    size_t size;              // 缓存内容的大小
    time_t expr;              // 过期时间
    struct _cache_node* next; // 下一节点指针
} cache_node_t;

// cache_map_t 哈希表
typedef struct _cache_map {
    cache_node_t** nodes;          // 哈希节点 (哈希槽)
    pthread_rwlock_t* node_locks;  // 哈希节点读写锁 (每个哈希槽单独一个读写锁)
    uint32_t length;               // 哈希表长度, 即哈希槽的个数
    atomic_uint size;              // 哈希表的实际大小, 当哈希冲突时, 哈希表实际大小会大于哈希表长度
} cache_map_t;

// config_t 缓存配置
typedef struct _config {
    uint32_t map_num;        // 哈希表的数量
    uint32_t map_init_len;   // 哈希表的初始长度
    uint32_t cache_second;   // 缓存时间
    uint32_t sweep_second;   // 缓存清理间隔
    uint32_t expand_second;  // 缓存扩容间隔
    hasher_t hasher;         // 哈希函数
    expander_t expander;     // 扩容判断函数
} config_t;

// cache_t 缓存对象
typedef struct _cache {
    config_t* config;             // 配置
    cache_map_t** cache_maps;     // 各哈希表
    pthread_rwlock_t* map_locks;  // 各哈希表读写锁 (仅当哈希表扩容时使用写锁)
} cache_t;

// new_cache 新建缓存
cache_t* new_cache(config_t* conf);

// delete_cache 释放缓存. 释放缓存应仅在程序退出前执行, 执行时和执行后均不得使用缓存
void delete_cache(cache_t* cache);

// cache_get 读取缓存, 返回是否读取成功. 缓存数据会复制于 dst 指针中, dst 指针必须已分配内存且分配内存空间大于等于 dst_size
// 注意: 当 dst_size 小于缓存中的数据实际大小时, 只会返回 dst_size 大小的缓存数据, 以避免空间越界
bool cache_get(cache_t* cache, const char* key, void* dst, size_t dst_size);

// cache_get_block 读取缓存的阻塞方法, 此方法当多线程抢占锁时会阻塞直至成功, 只适用于必须要读出缓存的情形, 其他与 cache_get 一致
bool cache_get_block(cache_t* cache, const char* key, void* dst, size_t dst_size);

// cache_set 写入缓存, 返回是否写入成功. 需缓存数据会从 src 指针中复制, src 指针必须已分配内存且分配内存空间大于等于 src_size
bool cache_set(cache_t* cache, const char* key, void* src, size_t src_size);

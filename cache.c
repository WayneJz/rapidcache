#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

#include "cache.h"


// panic 抛出 rapidcache 致命错误 
#define panic(_msg_) { \
    fprintf(stderr, "rapidcache panic: %s (at %s: line %d)\n", (_msg_), __FILE__, __LINE__); \
    exit(1); \
}

// panic_ext 自定义文件和行数抛出 rapidcache 致命错误
#define panic_ext(_msg_, _file_, _line_) { \
    fprintf(stderr, "rapidcache panic: %s (at %s: line %d)\n", (_msg_), (_file_), (_line_)); \
    exit(1); \
}

// _must_auto_malloc 申请并清空内存, 失败则 panic
static inline void* _must_auto_malloc(size_t size, const char* file, int line) {
    void* p = malloc(size);
    if (p == NULL)
        panic_ext("malloc panic: is memory run out of?", file, line);
    memset(p, 0, size);
    return p;
}

// must_auto_malloc 申请并清空内存宏
#define must_auto_malloc(_size_) _must_auto_malloc(_size_, __FILE__, __LINE__);

// _must_auto_realloc 重新申请并清空内存, 失败则 panic
static inline void* _must_auto_realloc(void* ptr, size_t size, const char* file, int line) {
    void* p = realloc(ptr, size);
    if (p == NULL)
        panic_ext("realloc panic: is memory run out of?", file, line);
    memset(p, 0, size);
    return p;
}

// must_auto_realloc 重新申请并清空内存宏
#define must_auto_realloc(_ptr_, _size_) _must_auto_realloc(_ptr_, _size_, __FILE__, __LINE__);

// default_hasher 默认哈希函数 DJB Hash
static inline uint32_t default_hasher(const char* key, uint32_t max_len) {
    uint32_t hash = 5381;
    while (*key) {
        hash += (hash << 5) + (*key++);
    }
    return (hash & 0x7FFFFFFF) % max_len;
}

// default_expander 默认扩容判断函数
static inline bool default_expander(uint32_t length, uint32_t size) {
    return size > 2 * length;
}

// _find_pos 哈希位置查找
static inline uint32_t _find_pos(hasher_t hasher, const char* key, uint32_t max_len) {
    uint32_t pos = hasher(key, max_len);
    if (pos >= max_len) {
        panic("hasher produces invalid position");
    }
    return pos;
}

// _new_node 新建节点
static inline cache_node_t* _new_node(size_t key_size, size_t data_size) {
    cache_node_t* node = must_auto_malloc(sizeof(cache_node_t));
    node->key = must_auto_malloc(key_size);
    node->data = must_auto_malloc(data_size);
    node->next = NULL;
    return node;
}

// _copy_node_data 拷贝节点数据
static inline void _copy_node_data(cache_node_t* node, void* dst, size_t dst_size) {
    if (dst == NULL) {
        panic("destination pointer is not allocated");
    }
    memcpy(dst, node->data, dst_size < node->size ? dst_size : node->size);
}

// _valid_node 检查是否为有效节点
static inline bool _valid_node(cache_node_t* node, time_t now) {
    return node->expr <= 0 || now <= node->expr;
}

// _traverse_copy_node 递归哈希冲突链表并复制数据
bool _traverse_copy_node(cache_node_t* node, time_t now, const char* key, void* dst, size_t dst_size) {
    if (node == NULL) {
        return false;
    }
    if ( !_valid_node(node, now) || strcmp(node->key, key) != 0) {
        return _traverse_copy_node(node->next, now, key, dst, dst_size);
    }
    _copy_node_data(node, dst, dst_size);
    return true;
}

// new_cache 初始化缓存
cache_t* new_cache(config_t* conf) {
    cache_t* cache = must_auto_malloc(sizeof(cache_t));
    cache->config = must_auto_malloc(sizeof(config_t));

    cache->config->map_num = conf != NULL && conf->map_num > 0 ? conf->map_num : DEFAULT_MAP_NUM;
    cache->config->map_init_len = conf != NULL && conf->map_init_len > 0 ? conf->map_init_len : DEFAULT_MAP_INIT_LEN;
    cache->config->cache_second = conf != NULL && conf->cache_second > 0 ? conf->cache_second : DEFAULT_CACHE_SECONDS;
    cache->config->sweep_second = conf != NULL && conf->sweep_second > 0 ? conf->sweep_second : DEFAULT_SWEEP_SECONDS;
    cache->config->expand_second = conf != NULL && conf->expand_second > 0 ? conf->expand_second : DEFAULT_EXPAND_SECONDS;
    cache->config->hasher = conf != NULL && conf->hasher != NULL ? conf->hasher : default_hasher;
    cache->config->expander = conf != NULL && conf->expander != NULL ? conf->expander : default_expander;
    
    cache->cache_maps = must_auto_malloc(cache->config->map_num * sizeof(cache_map_t*));
    cache->map_locks = must_auto_malloc(cache->config->map_num * sizeof(pthread_rwlock_t));

    for (uint32_t i = 0; i < cache->config->map_num; ++ i) {
        cache_map_t* map = must_auto_malloc(sizeof(cache_map_t));

        map->length = cache->config->map_init_len;
        map->size = 0;
        map->nodes = must_auto_malloc(map->length * sizeof(cache_node_t));
        map->node_locks = must_auto_malloc(map->length * sizeof(pthread_rwlock_t));

        for (uint32_t j = 0; j < map->length; ++ j) {
            map->nodes[j] = NULL;
            pthread_rwlock_init(&map->node_locks[j], NULL);
        }
        cache->cache_maps[i] = map;
        pthread_rwlock_init(&cache->map_locks[i], NULL);
    }
    return cache;
}

// delete_cache 释放缓存
void delete_cache(cache_t* cache) {
    for (uint32_t i = 0; i < cache->config->map_num; ++ i) { 
        if (pthread_rwlock_wrlock(&cache->map_locks[i]) != 0) {
            continue;
        }
        cache_map_t* map = cache->cache_maps[i];

        for (uint32_t j = 0; j < map->length; ++ j) {
            if (pthread_rwlock_wrlock(&map->node_locks[j]) != 0) {
                continue;
            }
            cache_node_t* node = map->nodes[j];

            // 清理哈希节点链表
            while (node != NULL) {
                cache_node_t* origin_ptr = node;
                node = node->next;

                origin_ptr->next = NULL;
                free(origin_ptr->data);
                free(origin_ptr->key);
                free(origin_ptr);
            }
            pthread_rwlock_destroy(&map->node_locks[j]);
        }
        free(map->node_locks);
        pthread_rwlock_destroy(&cache->map_locks[i]);
    }
    free(cache->map_locks);
    free(cache->cache_maps);
    free(cache->config);
    free(cache);
}

// cache_get 读取缓存
bool cache_get(cache_t* cache, const char* key, void* dst, size_t dst_size) {

    uint32_t mappos = _find_pos(cache->config->hasher, key, cache->config->map_num);
    if (pthread_rwlock_tryrdlock(&cache->map_locks[mappos]) != 0) {
        return false;
    }
    cache_map_t* map = cache->cache_maps[mappos];

    uint32_t nodepos = _find_pos(cache->config->hasher, key, map->length);
    if (pthread_rwlock_tryrdlock(&map->node_locks[nodepos]) != 0) {
        return false;
    }
    time_t now = time(NULL);
    bool found = _traverse_copy_node(map->nodes[nodepos], now, key, dst, dst_size);

    pthread_rwlock_unlock(&map->node_locks[nodepos]);
    pthread_rwlock_unlock(&cache->map_locks[mappos]);
    return found;
}

// cache_get_block 读取缓存 (阻塞方法)
bool cache_get_block(cache_t* cache, const char* key, void* dst, size_t dst_size) {
    
    // 双重哈希到哈希槽位
    uint32_t mappos = _find_pos(cache->config->hasher, key, cache->config->map_num);
    if (pthread_rwlock_rdlock(&cache->map_locks[mappos]) != 0) {
        return false;
    }
    cache_map_t* map = cache->cache_maps[mappos];

    uint32_t nodepos = _find_pos(cache->config->hasher, key, map->length);
    if (pthread_rwlock_rdlock(&map->node_locks[nodepos]) != 0) {
        return false;
    }

    // 寻找数据
    time_t now = time(NULL);
    bool found = _traverse_copy_node(map->nodes[nodepos], now, key, dst, dst_size);

    pthread_rwlock_unlock(&map->node_locks[nodepos]);
    pthread_rwlock_unlock(&cache->map_locks[mappos]);
    return found;
}

// cache_set 写入缓存
bool cache_set(cache_t* cache, const char* key, void* src, size_t src_size) {


    // 双重哈希到哈希槽位
    uint32_t mappos = _find_pos(cache->config->hasher, key, cache->config->map_num);
    if (pthread_rwlock_rdlock(&cache->map_locks[mappos]) != 0) {
        return false;
    }
    cache_map_t* map = cache->cache_maps[mappos];

    uint32_t nodepos = _find_pos(cache->config->hasher, key, map->length);
    if (pthread_rwlock_wrlock(&map->node_locks[nodepos]) != 0) {
        return false;
    }

    cache_node_t* root = map->nodes[nodepos];

    // 检查是否可更新
    cache_node_t* p = root;
    bool updated = false;

    while (p != NULL) {
        if (strcmp(p->key, key) == 0) {
            if (p->size != src_size) {
                p->data = must_auto_realloc(p->data, src_size);
                p->size = src_size;
                memcpy(p->data, src, p->size);
            } else {
                memset(p->data, 0, p->size);
                memcpy(p->data, src, p->size);
            }
            p->expr = time(NULL) + cache->config->cache_second;
            updated = true;
            break;
        }
        p = p->next;
    }

    // 如未更新, 新建节点写入
    if (! updated) {

        // 初始化节点
        cache_node_t* node = _new_node(strlen(key) + 1, src_size);
        memcpy(node->key, key, strlen(key) + 1);
        memcpy(node->data, src, src_size);
        node->expr = time(NULL) + cache->config->cache_second;
        node->size = src_size;

        // 根据 LRU 思想, 最近写入的数据会比之前写入的数据更频繁被读出, 故这里使用头插法存储节点, 冗余数据由清理线程处理
        node->next = root;
        map->nodes[nodepos] = node;
        map->size++;
    }
 
    pthread_rwlock_unlock(&map->node_locks[nodepos]);
    pthread_rwlock_unlock(&cache->map_locks[mappos]);
    return true;
}

// _cleaner_thread 缓存清理定时线程
void _cleaner_thread(cache_t* cache) {

    for (uint32_t i = 0; i < cache->config->map_num; ++ i) {

        if (pthread_rwlock_rdlock(&cache->map_locks[i]) != 0) {
            continue;
        }
        cache_map_t* map = cache->cache_maps[i];

        // 哈希节点清理
        for (uint32_t j = 0; j < map->length; ++ j) {
            cache_node_t* node = map->nodes[j];
            if (pthread_rwlock_wrlock(&map->node_locks[j]) != 0) {
                continue;
            }
            time_t now = time(NULL);

            // 先清理后续节点
            while (node != NULL) {

                cache_node_t* next_node = node->next;
                if (next_node != NULL && next_node->expr > 0 && now > next_node->expr) {
                    free(next_node->data);
                    free(next_node->key);
                    node->next = node->next->next;

                    next_node->next = NULL;
                    free(next_node);
                }
                node = node->next;
            }

            // 最后清理头节点
            if (map->nodes[j] != NULL && map->nodes[j]->expr > 0 && now > map->nodes[j]->expr) {
                free(map->nodes[j]->data);
                free(map->nodes[j]->key);
                map->nodes[j]->next = NULL;

                free(map->nodes[j]);
                map->nodes[j] = NULL;
            }
            pthread_rwlock_unlock(&map->node_locks[j]);
        }
        pthread_rwlock_unlock(&cache->map_locks[i]);
    }
}

// _cache_map_expand 复制扩容哈希表并删除原哈希表
cache_map_t* _cache_map_expand(cache_t* cache, cache_map_t* origin) {

    // 初始化新的哈希表
    cache_map_t* map = must_auto_malloc(sizeof(cache_map_t));

    map->length = origin->length * 2;
    map->size = origin->size;
    map->nodes = must_auto_malloc(map->length * sizeof(cache_node_t));
    map->node_locks = must_auto_malloc(map->length * sizeof(pthread_rwlock_t));

    for (uint32_t i = 0; i < map->length; ++ i) {
        map->nodes[i] = NULL;
        pthread_rwlock_init(&map->node_locks[i], NULL);
    }

    // 将原哈希表元素复制到新哈希表
    for (uint32_t i = 0; i < origin->length; ++ i) {
        cache_node_t* node = origin->nodes[i];
        while (node != NULL) {
            
            cache_node_t* new_node = _new_node(strlen(node->key) + 1, node->size);
            memcpy(new_node->key, node->key, strlen(node->key) + 1);
            memcpy(new_node->data, node->data, node->size);
            new_node->expr = time(NULL) + cache->config->cache_second;
            new_node->size = node->size;

            uint32_t new_pos = _find_pos(cache->config->hasher, new_node->key, map->length);
            
            // 新哈希表也可能存在冲突, 按照原哈希表的顺序, 这里应使用尾插法
            if (map->nodes[new_pos] == NULL) {
                map->nodes[new_pos] = new_node;
            } else {
                cache_node_t* p = map->nodes[new_pos];
                while (p->next != NULL) {
                    p = p->next;
                }
                p->next = new_node;
            }

            // 保留原指针, 并向后遍历
            cache_node_t* origin_ptr = node;
            node = node->next;

            // 原指针释放
            origin_ptr->next = NULL;
            free(origin_ptr->data);
            free(origin_ptr->key);
            free(origin_ptr);
        }
        origin->nodes[i] = NULL;
        pthread_rwlock_destroy(&origin->node_locks[i]);
    }
    
    // 释放原哈希表
    origin->length = 0;
    origin->size = 0;
    free(origin->node_locks);
    free(origin);
    return map;
}


// _expand_thread 缓存检查扩容定时线程
void _expand_thread(cache_t* cache) {

    for (uint32_t i = 0; i < cache->config->map_num; ++ i) {

        if (pthread_rwlock_wrlock(&cache->map_locks[i]) != 0) {
            continue;
        }
        cache_map_t* origin_map = cache->cache_maps[i];

        // 缓存扩容 
        if (cache->config->expander(origin_map->length, origin_map->size)) {
            cache->cache_maps[i] = _cache_map_expand(cache, origin_map);
        }
        pthread_rwlock_unlock(&cache->map_locks[i]);
    }
}

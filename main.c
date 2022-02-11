#include <stdlib.h>
#include <stdio.h>

#include "cache.h"

int main() {
    cache_t* cache = new_cache(NULL);

    void* dst = malloc(10000);

    printf("is cached=%d\n", cache_get(cache, "haha", dst, 10000));

    // 测试加入缓存
    cache_set(cache, "haha", "hehe", 4);
    printf("is cached=%d\n", cache_get(cache, "haha", dst, 10000));
    printf("dst=%s\n", (char*)dst);

    // 测试修改缓存, 数据有6位但只设置4位
    cache_set(cache, "haha", "hohoho", 4);
    printf("is cached=%d\n", cache_get(cache, "haha", dst, 10000));
    printf("dst=%s\n", (char*)dst);

    // 测试修改缓存, 设置6位
    cache_set(cache, "haha", "hohoho", 6);
    printf("is cached=%d\n", cache_get(cache, "haha", dst, 10000));
    printf("dst=%s\n", (char*)dst);

    delete_cache(cache);

    free(dst);
}
# rapidcache
**纯 C 语言实现的高速缓存**

## 功能与特性

- 纯 C 语言实现的哈希表 (双重哈希)

- 使用最小维度的读写锁以提升并发性能

- 可自定义的参数 (包括缓存时间, 清理时间, 可自定义的 hash 算法等等)

- [WIP] 独立的清理线程和扩容线程, 以减少内存占用和减少哈希碰撞

## 简单测试

```C
gcc -std=c11 cache.c main.c -lpthread -o cache

./cache
```

## 使用方法

下载到要使用的项目中, 然后引用唯一的头文件即可.

详细用法请参阅 `main.c` 的简单测试.
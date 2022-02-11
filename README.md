# rapidcache
**A Pure C Implementation of Fast Cache - 纯 C 语言实现的高速缓存**

[中文说明](/README_CN.md)

## Features

- Pure C implementation of Hash (two levels of hash) 

- Minimum level of read-write lock in use, improving concurrent efficiency 

- Self-defined options (including cache second, sweep second, hash function etc.)

- [WIP] Independent cleaner and expander threads to reduce memory usage and hash collision


## Demo & Simple Test

```C
gcc -std=c11 cache.c main.c -lpthread -o cache

./cache
```

## Usage

Download to your project, and include the only header file.

For details, see `main.c` the demo.
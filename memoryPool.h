#include <stdatomic.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// 内存申请 并赋初始值
#define NEW(type, ...) ({ \
    type* _ptr = pool_alloc(sizeof(type)); \
    if(_ptr) { \
        *_ptr = (type){__VA_ARGS__}; \
    } \
    _ptr; \
})

// todo: 依赖用户正确传递size 可能容易出错
#define DELETE(ptr, type) do { \
    if(ptr) { \
        pool_free(ptr, sizeof(type)); \
        ptr = NULL; \
    } \
} while(0)

// 全局基础配置
#define MEMORY_POOL_NUM 64
#define SLOT_BASE_SIZE 8
#define MAX_SLOT_SIZE 512
#define BLOCK_SIZE 4096

typedef struct Slot {
    _Atomic(struct Slot*) next; // 原子指针
} Slot;

typedef struct MemoryPool {
    size_t block_size;
    size_t slot_size;
    Slot* first_block;
    Slot* cur_slot;
    _Atomic(Slot*) free_list;
    Slot* last_slot;
    pthread_mutex_t block_mutex;
} MemoryPool;

// 声明全局内存池数组
extern MemoryPool memory_pools[MEMORY_POOL_NUM];

// 初始化全局hashBucket
void init_memory_pools();

void memory_pool_destroy(MemoryPool* pool);

MemoryPool* get_memory_pool(size_t size);

// 核心功能接口
void* pool_alloc(size_t size);


// todo：用户可能传递错误的 size，导致内存被错误释放到其他池子。
void pool_free(void* ptr, size_t size);

void destroy_memory_pools();




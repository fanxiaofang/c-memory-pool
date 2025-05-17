#include "memoryPool.h"
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <stdint.h>

// 获取高精度时间戳（微秒）
static long long get_time_us() {
    struct timespec ts;
    // 线程安全
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

// ================= 功能测试 =================
void test_basic_allocation() {
    printf("==== Running basic allocation test ====\n");
    int* ptr = NEW(int, 42);
    assert(ptr != NULL && *ptr == 42);
    DELETE(ptr, int);
    printf("Basic allocation test passed.\n\n");
}

// 内存池分配的内存边界对齐测试
void test_alignment() {
    printf("==== Running alignment test ====\n");
    for (int size = 1; size <= MAX_SLOT_SIZE; size++) {
        void* ptr = pool_alloc(size);
        assert(ptr != NULL);
        
        MemoryPool* pool = get_memory_pool(size);
        assert(pool != NULL);
        
        uintptr_t addr = (uintptr_t)ptr;
        assert(addr % pool->slot_size == 0 && "Alignment failed");
        
        pool_free(ptr, size);
    }
    printf("Alignment test passed.\n\n");
}

void test_pool_selection() {
    printf("==== Running pool selection test ====\n");
    for (int i = 1; i <= MEMORY_POOL_NUM; i++) {
        size_t expect_size = i * SLOT_BASE_SIZE;
        size_t test_size = expect_size - 1;
        
        MemoryPool* pool = get_memory_pool(test_size);
        assert(pool->slot_size == expect_size);
    }
    printf("Pool selection test passed.\n\n");
}

void test_large_allocation() {
    printf("==== Running large allocation test ====\n");
    void* ptr = pool_alloc(MAX_SLOT_SIZE + 1);
    assert(ptr != NULL);
    pool_free(ptr, MAX_SLOT_SIZE + 1);
    
    ptr = pool_alloc(MAX_SLOT_SIZE);
    assert(ptr != NULL);
    pool_free(ptr, MAX_SLOT_SIZE);
    printf("Large allocation test passed.\n\n");
}

// ================= 内存完整性测试 =================
void test_data_integrity() {
    printf("==== Running data integrity test ====\n");
    int* ptr[100];
    
    // 写入阶段
    for (int i = 0; i < 100; i++) {
        ptr[i] = NEW(int, 0);
        assert(ptr[i] != NULL);
        *ptr[i] = i;
    }
    
    // 验证阶段
    for (int i = 0; i < 100; i++) {
        assert(*ptr[i] == i && "Data corruption detected");
        DELETE(ptr[i], int);
    }
    printf("Data integrity test passed.\n\n");
}

// ================= 性能测试 =================

typedef struct u1 {
    int s[2];
}u1;

typedef struct u2 {
    int s[4];
}u2;

typedef struct u3 {
    int s[8];
}u3;

typedef struct u4 {
    int s[16];
}u4;

typedef struct u5 {
    int s[32];
}u5;

typedef struct {
    int count;
    int round;
} ThreadArgs;

void warmup_memory_pool(int warmup_round, int warmup_count)
{
    printf("=== Memory Pool Warmup ===\n");

    // 预热不同大小的内存块
    size_t warmup_sizes[] = {
        sizeof(u1), // 数字是 size_t 类型
        sizeof(u2),
        sizeof(u3),
        sizeof(u4),
        sizeof(u5)
    };
    int sizes_num = sizeof(warmup_sizes) / sizeof(warmup_sizes[0]);

    // 多轮预热
    for (int round = 0; round < warmup_round; round++) {
        for (int i = 0; i < warmup_count; i++) {
            for (int j = 0; j < sizes_num; j++) {
                void* ptr = pool_alloc(warmup_sizes[j]);
                if (ptr) {
                    memset(ptr, 1, warmup_sizes[j]);
                    pool_free(ptr, warmup_sizes[j]);
                }
            }
        }
    }
    printf("=== Memory Pool Warmup Completed\n\n");
}

void* pthread_pool_func(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    const int test_round = args->round;
    const int test_count = args->count;

    for (int i = 0; i < test_round; i++) {
        for (int j = 0; j < test_count; j++) {
            void* p1 = pool_alloc(sizeof(u1));
            pool_free(p1, sizeof(u1));
            void* p2 = pool_alloc(sizeof(u2));
            pool_free(p2, sizeof(u2));
            void* p3 = pool_alloc(sizeof(u3));
            pool_free(p3, sizeof(u3));
            void* p4 = pool_alloc(sizeof(u4));
            pool_free(p4, sizeof(u4));
            void* p5 = pool_alloc(sizeof(u5));
            pool_free(p5, sizeof(u5));
        }
    }
    return NULL;
}

void* pthread_malloc_func(void* arg) {
    ThreadArgs* args = (ThreadArgs*)arg;
    const int test_round = args->round;
    const int test_count = args->count;

    for (int i = 0; i < test_round; i++) {
        for (int j = 0; j < test_count; j++) {
            void* p1 = malloc(sizeof(u1));
            free(p1);
            void* p2 = malloc(sizeof(u2));
            free(p2);
            void* p3 = malloc(sizeof(u3));
            free(p3);
            void* p4 = malloc(sizeof(u4));
            free(p4);
            void* p5 = malloc(sizeof(u5));
            free(p5);
        }
    }
    return NULL;
}

void performance_pool_test(int thread_num, int alloc_times, int round) {
    pthread_t threads[thread_num];
    ThreadArgs args = {alloc_times, round};
    
    printf("==== Starting memoryPoll performance test (%d threads) ====\n", thread_num);
    long long start = get_time_us();
    
    for (int i = 0; i < thread_num; i++) {
        pthread_create(&threads[i], NULL, pthread_pool_func, &args);
    }
    
    for (int i = 0; i < thread_num; i++) {
        pthread_join(threads[i], NULL);
    }

    long long cost_time = get_time_us() - start;
    
    printf("%d个线程并发执行 %d 轮次，每轮次pool_alloc&pool_free %d次，花费时间 %llu us\n\n", thread_num, round, alloc_times, cost_time);

}

void performance_malloc_test(int thread_num, int alloc_times, int round) {
    pthread_t threads[thread_num];
    ThreadArgs args = {alloc_times, round};
    
    printf("==== Starting malloc performance test (%d threads) ====\n", thread_num);
    long long start = get_time_us();
    
    for (int i = 0; i < thread_num; i++) {
        pthread_create(&threads[i], NULL, pthread_malloc_func, &args);
    }
    
    for (int i = 0; i < thread_num; i++) {
        pthread_join(threads[i], NULL);
    }

    long long cost_time = get_time_us() - start;
    
    printf("%d个线程并发执行 %d 轮次，每轮次malloc&free %d次，花费时间 %llu us\n\n", thread_num, round, alloc_times, cost_time);
}

// ================= 主测试流程 =================
int main() {
    init_memory_pools();
    
    // 功能测试
    test_basic_allocation();
    test_alignment();
    test_pool_selection();
    test_large_allocation();
    test_data_integrity();
    
    // 内存预热
    warmup_memory_pool(3, 100);

    // 性能测试: 线程数 每轮申请次数 轮次 
    performance_pool_test(1, 1000, 10);
    performance_malloc_test(1, 1000, 10);
    performance_pool_test(2, 1000, 10);
    performance_malloc_test(2, 1000, 10);
    performance_pool_test(5, 1000, 10);
    performance_malloc_test(5, 1000, 10);
    
    // 清理检测
    destroy_memory_pools();
    
    printf("All tests passed!\n");
    return 0;
}
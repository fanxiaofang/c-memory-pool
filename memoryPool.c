#include "./memoryPool.h"
#include <stdint.h>

MemoryPool memory_pools[MEMORY_POOL_NUM];

/*
cur_slot竞争条件 多线程同时移动cur_slot导致内存从夫分配或越界
内存延迟分配，比如刚从空闲链表分配出去的slot 立马就被释放重新加入了空闲链表
*/

/*
对齐边界：aline (slotsize)
确保block的first slot的内存地址为align的倍数
p已经是对齐的，返回需要填充的字节0
p非对齐的，返回需要填充的字节 offset
*/
static size_t pad_pointer(char* p, size_t align)
{
    // 指针类型的整数, 足够大存储指针类型的无符号整数 C99
    uintptr_t addr = (uintptr_t)p;
    size_t offset = align - (addr % align);
    return (offset == align) ? 0 : offset;
}

static void memory_pool_init(MemoryPool* pool, size_t slot_size)
{
    pool->block_size = BLOCK_SIZE;
    pool->slot_size = slot_size;
    pool->first_block = NULL;
    pool->cur_slot = NULL;
    atomic_init(&pool->free_list, NULL);
    pool->last_slot = NULL;
}

void init_memory_pools()
{
    for (int i = 0; i < MEMORY_POOL_NUM; i++) {
        memory_pool_init(&memory_pools[i], (i+1)*SLOT_BASE_SIZE);
    }
}

/*
size 向上对齐 SLOT_BASE_SIZE的倍数，确保分配的内存池足够大
*/
MemoryPool* get_memory_pool(size_t size)
{
    if (size > MAX_SLOT_SIZE || size == 0) return NULL;
    int index = (size + SLOT_BASE_SIZE - 1) / SLOT_BASE_SIZE - 1;
    return &memory_pools[index];
}

static int allocate_new_block(MemoryPool* pool)
{

    size_t alloc_size = pool->block_size;
    void* new_block = (void*)malloc(alloc_size);
    if (!new_block) {
        pthread_mutex_unlock(&pool->block_mutex);
        return 0;
    }

    Slot* cast_block = (Slot*)new_block;
    cast_block->next = pool->first_block;
    pool->first_block = cast_block;

    char* body = (char*)new_block + sizeof(Slot*);
    size_t padding_size = pad_pointer(body, pool->slot_size);
    pool->cur_slot = (Slot*)(body + padding_size);

    char* end = new_block + alloc_size - pool->slot_size;
    pool->last_slot = (Slot*)(end + 1);
    // todo：如果原freelist有未释放的slot呢 是属于原first block，如果分配新块之后
    // 原freelist有释放slot，此时freelist被空置可能会导致这些slot无法被重用
    // pool->free_list = NULL;

    return 1;
}

static void insert_into_free_list(MemoryPool* pool, Slot* slot)
{
    // 1、初始化插入slot的next指针
    // 2、将slot插入链表头部(原子操作更新头指针)
    Slot* old_head = atomic_load_explicit(&pool->free_list, memory_order_relaxed);
    do {
        slot->next = old_head;
    } while(!atomic_compare_exchange_weak_explicit(
        &pool->free_list,
        &old_head,
        slot,
        memory_order_release, //CAS成功时的内存序：确保slot->next对其他线程可见
        memory_order_relaxed //CAS失败时的内存序：仅需重试，无需同步
    ));

    // memory_order_release 确保next写操作不会被重排序到原子存储之后，确保其他线程正确看到 新内存块的完整状态
}

static Slot* remove_from_free_list(MemoryPool* pool)
{
    // 1、读取当前头指针
    // 2、通过 CAS操作原子地更新头指针为next节点
    // 使用memory_order_acquire 确保后续对内存块的操作（如读取next指针或使用内存块）不会被重排序到加载之前
    Slot* old_head = atomic_load_explicit(&pool->free_list, memory_order_acquire);
    while (old_head != NULL) {
        // 获取新节点候选
        Slot* new_head = old_head->next;
        // CAS原子更新：将链表 从old_head替换为new_head
        if (atomic_compare_exchange_weak_explicit(
            &pool->free_list, &old_head, new_head,
            memory_order_release,//CAS成功时的内存序：新头指针对其他线程可见(同步新头节点)
            memory_order_acquire //CAS失败时的内存序：重新加载并同步
        )) {
            // CAS成功：返回被移除 的old_head(用户获得这块内存)
            return old_head;
        }
        // CAS失败：old_head被更新为最新之，循环重试
    }

    return NULL; //链表为空时返回NULL
}

void* pool_alloc(size_t size)
{
    // 申请大小超过MAX_SLOT_size 直接malloc
    if (size > MAX_SLOT_SIZE) {
        void* ptr = malloc(size);
        return ptr ? ptr:NULL;
    }

    // 根据申请大小 选择合适的MemoryPool
    MemoryPool* pool = get_memory_pool(size);
    if (!pool) return NULL;

    // 先从空闲链表中分配内存
    Slot* slot = remove_from_free_list(pool);
    if (slot != NULL) {
        return (void*)slot;
    }

    // 空闲链表中无内存时，申请新的block
    pthread_mutex_lock(&pool->block_mutex);
    if (pool->cur_slot >= pool->last_slot) {
        if(!allocate_new_block(pool)) {
            pthread_mutex_unlock(&pool->block_mutex);
            return NULL;
        }
    }

    // 从新的block中分配slot给用户，并更新cur_slot
    void* ptr = pool->cur_slot;
    Slot* new_cur_slot = (Slot*)((char*)pool->cur_slot + pool->slot_size);

    pool->cur_slot = new_cur_slot;

    pthread_mutex_unlock(&pool->block_mutex);
    return ptr;
}

void pool_free(void* ptr, size_t size)
{
    if (!ptr)
        return;
    if (size > MAX_SLOT_SIZE) {
        free(ptr);
        return;
    }

    MemoryPool* pool = get_memory_pool(size);
    if (!pool) 
        return;
    insert_into_free_list(pool, (Slot*)ptr);
}

// todo:可能在释放块时，其他线程仍然在访问这些块，导致use-after-free错误。因此，destroy函数应该在所有线程都停止使用内存池之后调用，否则会有安全隐患。
void memory_pool_destroy(MemoryPool* pool)
{
    pthread_mutex_lock(&pool->block_mutex);

    Slot* current_block = pool->first_block;
    while (current_block) {
        // 使用普通的next指针遍历
        Slot* next = current_block->next;
        // 安全释放内存块
        free(current_block);

        current_block = next;
    }

    pool->first_block = NULL;
    pool->cur_slot = NULL;
    pool->last_slot = NULL;

    // 原子清空空闲列表，确保可见性和内存一致性
    atomic_store(&pool->free_list, NULL);

    pthread_mutex_unlock(&pool->block_mutex);
}

void destroy_memory_pools()
{
    for (int i = 0; i < MEMORY_POOL_NUM; i++) {
        memory_pool_destroy(&memory_pools[i]);
    }
}
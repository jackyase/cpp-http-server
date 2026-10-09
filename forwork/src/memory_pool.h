#pragma once

#include <cstddef>
#include <vector>

// 固定大小块内存池（空闲链表实现）。
// 适用：高频创建/销毁的固定大小对象，避免反复 malloc/free 的开销和内存碎片。
class MemoryPool {
public:
    explicit MemoryPool(size_t blockSize, size_t blocksPerChunk = 256);
    ~MemoryPool();

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    void* allocate();          // 取一块（空则扩容）
    void deallocate(void* p);  // 归还一块

    size_t blockSize() const { return blockSize_; }

private:
    void expand();             // 分配一大块内存，切成 block 挂到空闲链表

    struct FreeNode {
        FreeNode* next;
    };

    size_t blockSize_;
    size_t blocksPerChunk_;
    FreeNode* freeList_ = nullptr;
    std::vector<void*> chunks_;   // 记录所有大块，析构时统一 free
};

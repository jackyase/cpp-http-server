#include "memory_pool.h"

#include <cstddef>
#include <cstdlib>
#include <new>

namespace {

size_t alignUp(size_t n, size_t align) {
    return (n + align - 1) & ~(align - 1);
}

}  // namespace

MemoryPool::MemoryPool(size_t blockSize, size_t blocksPerChunk)
    : blockSize_(alignUp(blockSize < sizeof(FreeNode) ? sizeof(FreeNode) : blockSize,
                         alignof(std::max_align_t))),
      blocksPerChunk_(blocksPerChunk) {}

MemoryPool::~MemoryPool() {
    for (void* p : chunks_) std::free(p);
}

void MemoryPool::expand() {
    size_t chunkBytes = blockSize_ * blocksPerChunk_;
    void* raw = std::malloc(chunkBytes);
    if (!raw) throw std::bad_alloc();
    chunks_.push_back(raw);

    // 把整块切成 blockSize_ 大小，串成空闲链表
    char* base = static_cast<char*>(raw);
    for (size_t i = 0; i < blocksPerChunk_; ++i) {
        FreeNode* node = reinterpret_cast<FreeNode*>(base + i * blockSize_);
        node->next = freeList_;
        freeList_ = node;
    }
}

void* MemoryPool::allocate() {
    if (!freeList_) expand();
    FreeNode* node = freeList_;
    freeList_ = node->next;
    return node;
}

void MemoryPool::deallocate(void* p) {
    if (!p) return;
    FreeNode* node = static_cast<FreeNode*>(p);
    node->next = freeList_;
    freeList_ = node;
}

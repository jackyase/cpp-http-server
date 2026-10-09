#pragma once

#include <cstddef>

#include "memory_pool.h"

// 连接读缓冲：从内存池取固定大小块，避免高频 malloc。
// 容量固定（= 池的块大小），超出即溢出（上层据此返回 413）。
class Buffer {
public:
    explicit Buffer(MemoryPool& pool);
    ~Buffer();
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    bool append(const char* data, size_t n);  // 空间不足返回 false
    void erase(size_t n);                     // 消费掉前 n 字节
    const char* data() const { return block_; }
    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }

private:
    MemoryPool* pool_;
    char* block_;
    size_t size_ = 0;
    size_t capacity_ = 0;
};

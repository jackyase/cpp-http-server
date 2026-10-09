#include "buffer.h"

#include <cstring>

Buffer::Buffer(MemoryPool& pool)
    : pool_(&pool),
      block_(static_cast<char*>(pool.allocate())),
      capacity_(pool.blockSize()) {}

Buffer::~Buffer() {
    if (block_) pool_->deallocate(block_);
}

Buffer::Buffer(Buffer&& other) noexcept
    : pool_(other.pool_), block_(other.block_),
      size_(other.size_), capacity_(other.capacity_) {
    other.block_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        if (block_) pool_->deallocate(block_);
        pool_ = other.pool_;
        block_ = other.block_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.block_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    return *this;
}

bool Buffer::append(const char* data, size_t n) {
    if (size_ + n > capacity_) return false;
    std::memcpy(block_ + size_, data, n);
    size_ += n;
    return true;
}

void Buffer::erase(size_t n) {
    if (n >= size_) {
        size_ = 0;
        return;
    }
    std::memmove(block_, block_ + n, size_ - n);
    size_ -= n;
}

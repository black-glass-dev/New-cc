#include "timeline/memory_pool.hpp"

#include <algorithm>
#include <stdexcept>

namespace owncut::timeline {

PooledBuffer::~PooledBuffer() {
    release();
}

PooledBuffer::PooledBuffer(PooledBuffer&& other) noexcept
    : owner_(other.owner_),
      slab_index_(other.slab_index_),
      data_(other.data_),
      capacity_bytes_(other.capacity_bytes_) {
    other.owner_ = nullptr;
    other.slab_index_ = 0;
    other.data_ = nullptr;
    other.capacity_bytes_ = 0;
}

PooledBuffer& PooledBuffer::operator=(PooledBuffer&& other) noexcept {
    if (this != &other) {
        release();
        owner_ = other.owner_;
        slab_index_ = other.slab_index_;
        data_ = other.data_;
        capacity_bytes_ = other.capacity_bytes_;
        other.owner_ = nullptr;
        other.slab_index_ = 0;
        other.data_ = nullptr;
        other.capacity_bytes_ = 0;
    }
    return *this;
}

void PooledBuffer::release() noexcept {
    if (owner_ != nullptr && data_ != nullptr) {
        owner_->return_slab(slab_index_);
    }
    owner_ = nullptr;
    slab_index_ = 0;
    data_ = nullptr;
    capacity_bytes_ = 0;
}

FrameMemoryPool::FrameMemoryPool(std::size_t slab_bytes, std::size_t slab_count)
    : slab_count_(slab_count) {
    if (slab_bytes == 0 || slab_count == 0) {
        throw std::invalid_argument("FrameMemoryPool requires non-zero slab size and count");
    }
    // Round each slab size up to a multiple of kCacheLineSize (64 bytes).
    slab_bytes_ = ((slab_bytes + kCacheLineSize - 1) / kCacheLineSize) * kCacheLineSize;
    const std::size_t total_bytes = slab_bytes_ * slab_count_;
    raw_arena_ = static_cast<uint8_t*>(
        ::operator new[](total_bytes, std::align_val_t(kCacheLineSize)));
    slots_.resize(slab_count_);
    free_stack_.reserve(slab_count_);
    for (std::size_t i = 0; i < slab_count_; ++i) {
        slots_[i].ptr = raw_arena_ + i * slab_bytes_;
        free_stack_.push_back(slab_count_ - 1 - i);
    }
}

FrameMemoryPool::~FrameMemoryPool() {
    if (raw_arena_ != nullptr) {
        ::operator delete[](raw_arena_, std::align_val_t(kCacheLineSize));
        raw_arena_ = nullptr;
    }
}

PooledBuffer FrameMemoryPool::acquire() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (free_stack_.empty()) {
        ++misses_;
        return PooledBuffer{};
    }
    const std::size_t idx = free_stack_.back();
    free_stack_.pop_back();
    ++hits_;
    const std::size_t in_use = slab_count_ - free_stack_.size();
    peak_in_use_ = std::max(peak_in_use_, in_use);
    return PooledBuffer(this, idx, slots_[idx].ptr, slab_bytes_);
}

void FrameMemoryPool::return_slab(std::size_t slab_index) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (slab_index < slab_count_) {
        free_stack_.push_back(slab_index);
    }
}

PoolStats FrameMemoryPool::stats() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    PoolStats out;
    out.total_slabs = slab_count_;
    out.free_slabs = free_stack_.size();
    out.in_use_slabs = slab_count_ - free_stack_.size();
    out.peak_in_use = peak_in_use_;
    out.hits = hits_;
    out.misses = misses_;
    out.slab_bytes = slab_bytes_;
    return out;
}

} // namespace owncut::timeline

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <vector>

namespace owncut::timeline {

inline constexpr std::size_t kCacheLineSize = 64;

enum class PixelFormat : uint8_t {
    RGBA32F = 0,
    RGBA8 = 1,
    YUV420P_8 = 2,
    YUV420P_10LE = 3
};

struct PoolStats final {
    std::size_t total_slabs{0};
    std::size_t free_slabs{0};
    std::size_t in_use_slabs{0};
    std::size_t peak_in_use{0};
    std::size_t hits{0};
    std::size_t misses{0};
    std::size_t slab_bytes{0};
};

class FrameMemoryPool;

// RAII cache-line-aligned slab handle returned to the pool automatically on destruction.
class PooledBuffer final {
public:
    PooledBuffer() noexcept = default;
    ~PooledBuffer();

    PooledBuffer(const PooledBuffer&) = delete;
    PooledBuffer& operator=(const PooledBuffer&) = delete;

    PooledBuffer(PooledBuffer&& other) noexcept;
    PooledBuffer& operator=(PooledBuffer&& other) noexcept;

    [[nodiscard]] bool valid() const noexcept { return data_ != nullptr; }
    [[nodiscard]] explicit operator bool() const noexcept { return valid(); }

    [[nodiscard]] uint8_t* data() noexcept { return data_; }
    [[nodiscard]] const uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] float* as_float() noexcept { return reinterpret_cast<float*>(data_); }
    [[nodiscard]] const float* as_float() const noexcept { return reinterpret_cast<const float*>(data_); }
    [[nodiscard]] std::size_t capacity_bytes() const noexcept { return capacity_bytes_; }

    void release() noexcept;

private:
    friend class FrameMemoryPool;
    PooledBuffer(FrameMemoryPool* owner, std::size_t slab_index, uint8_t* data, std::size_t bytes) noexcept
        : owner_(owner), slab_index_(slab_index), data_(data), capacity_bytes_(bytes) {}

    FrameMemoryPool* owner_{nullptr};
    std::size_t slab_index_{0};
    uint8_t* data_{nullptr};
    std::size_t capacity_bytes_{0};
};

// Fixed-capacity 64-byte aligned arena/slab allocator for zero-jitter video frame reuse.
class FrameMemoryPool final {
public:
    FrameMemoryPool(std::size_t slab_bytes, std::size_t slab_count);
    ~FrameMemoryPool();

    FrameMemoryPool(const FrameMemoryPool&) = delete;
    FrameMemoryPool& operator=(const FrameMemoryPool&) = delete;

    [[nodiscard]] PooledBuffer acquire() noexcept;
    [[nodiscard]] PoolStats stats() const noexcept;
    [[nodiscard]] std::size_t slab_bytes() const noexcept { return slab_bytes_; }
    [[nodiscard]] std::size_t slab_count() const noexcept { return slab_count_; }

private:
    friend class PooledBuffer;
    void return_slab(std::size_t slab_index) noexcept;

    struct alignas(kCacheLineSize) SlabSlot {
        uint8_t* ptr{nullptr};
    };

    std::size_t slab_bytes_{0};
    std::size_t slab_count_{0};
    uint8_t* raw_arena_{nullptr};
    std::vector<SlabSlot> slots_;
    std::vector<std::size_t> free_stack_;
    mutable std::mutex mutex_;
    std::size_t peak_in_use_{0};
    std::size_t hits_{0};
    std::size_t misses_{0};
};

// Wait-free cache-line-padded SPSC ring buffer for real-time audio and scrubbing pipelines.
template <typename T, std::size_t Capacity>
class LockFreeRingBuffer final {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "LockFreeRingBuffer Capacity must be a power of two >= 2");

public:
    LockFreeRingBuffer() noexcept = default;
    LockFreeRingBuffer(const LockFreeRingBuffer&) = delete;
    LockFreeRingBuffer& operator=(const LockFreeRingBuffer&) = delete;

    [[nodiscard]] bool push(const T& item) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & kMask;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool push(T&& item) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & kMask;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        buffer_[head] = std::move(item);
        head_.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool pop(T& out) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        out = std::move(buffer_[tail]);
        tail_.store((tail + 1) & kMask, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t size() const noexcept {
        const std::size_t h = head_.load(std::memory_order_acquire);
        const std::size_t t = tail_.load(std::memory_order_acquire);
        return (h - t) & kMask;
    }

    static constexpr std::size_t capacity() noexcept { return Capacity - 1; }

private:
    static constexpr std::size_t kMask = Capacity - 1;
    alignas(kCacheLineSize) std::atomic<std::size_t> head_{0};
    alignas(kCacheLineSize) std::atomic<std::size_t> tail_{0};
    alignas(kCacheLineSize) T buffer_[Capacity]{};
};

// Bounded lock-free MPSC queue using Vyukov per-slot sequence counters (zero heap allocs).
template <typename T, std::size_t Capacity>
class LockFreeMPSCQueue final {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "LockFreeMPSCQueue Capacity must be a power of two >= 2");

public:
    LockFreeMPSCQueue() noexcept {
        for (std::size_t i = 0; i < Capacity; ++i) {
            cells_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    LockFreeMPSCQueue(const LockFreeMPSCQueue&) = delete;
    LockFreeMPSCQueue& operator=(const LockFreeMPSCQueue&) = delete;

    [[nodiscard]] bool enqueue(const T& item) noexcept {
        Cell* cell = nullptr;
        std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &cells_[pos & kMask];
            const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
            const std::ptrdiff_t diff = static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(pos);
            if (diff == 0) {
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false; // full
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
        cell->data = item;
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool dequeue(T& out) noexcept {
        Cell& cell = cells_[dequeue_pos_ & kMask];
        const std::size_t seq = cell.sequence.load(std::memory_order_acquire);
        const std::ptrdiff_t diff =
            static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(dequeue_pos_ + 1);
        if (diff < 0) {
            return false; // empty
        }
        if (diff == 0) {
            out = std::move(cell.data);
            cell.sequence.store(dequeue_pos_ + Capacity, std::memory_order_release);
            ++dequeue_pos_;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool empty() const noexcept {
        const Cell& cell = cells_[dequeue_pos_ & kMask];
        const std::size_t seq = cell.sequence.load(std::memory_order_acquire);
        return static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(dequeue_pos_ + 1) < 0;
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;
    struct alignas(kCacheLineSize) Cell {
        std::atomic<std::size_t> sequence{0};
        T data{};
    };

    alignas(kCacheLineSize) Cell cells_[Capacity]{};
    alignas(kCacheLineSize) std::atomic<std::size_t> enqueue_pos_{0};
    alignas(kCacheLineSize) std::size_t dequeue_pos_{0};
};

} // namespace owncut::timeline

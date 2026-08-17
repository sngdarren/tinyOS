#pragma once
#include "tinyos/memory/allocator.hpp"
#include <algorithm>
#include <cstddef>
#include <atomic>
#include <stdexcept>
#include <utility>

namespace tinyos {

template<typename T>
class SPSC {

public:
    explicit SPSC(std::size_t capacity) : capacity_(capacity), mask_(capacity - 1), 
        cached_tail_(0), cached_head_(0) {
        if (!is_power_of_two(capacity)) {
            throw std::invalid_argument("capacity needs to be power of 2");
        }
        
        buffer_ = new T[capacity];
    }

    ~SPSC() { delete[] buffer_; }
    
    // copy 
    SPSC(SPSC const&) = delete;
    SPSC& operator=(SPSC const&) = delete;
    
    // move
    SPSC(SPSC&& other) noexcept {
        buffer_ = other.buffer_;
        other.buffer_ = nullptr;

        head_.store(other.head_.load());
        other.head_.store(0);
        
        tail_.store(other.tail_.load());
        other.tail_.store(0);

        cached_tail_ = other.cached_tail_;
        other.cached_tail_ = 0;
        cached_head_ = other.cached_head_;
        other.cached_head_ = 0;
        capacity_ = other.capacity_;
        other.capacity_ = 0;
        mask_ = other.mask_;
        other.mask_ = 0;
    }

    SPSC& operator=(SPSC&& other) noexcept {
        
        if (this != &other) {
            delete[] buffer_;
            buffer_ = nullptr;
            
            std::swap(other.buffer_, buffer_);
            std::swap(other.capacity_, capacity_);
            std::swap(other.mask_, mask_);
            
            head_.store(other.head_.load());
            other.head_.store(0);

            tail_.store(other.tail_.load());
            other.tail_.store(0);

            std::swap(other.cached_tail_, cached_tail_);
            std::swap(other.cached_head_, cached_head_);
        }

        return *this;
    }

    // producer side 
    bool try_produce(T const& item) {
        // Our own index, nobody else writes it, so relaxed is enough
        std::size_t const head = head_.load(std::memory_order_relaxed);

        // Fast path: trust the stale copy. tail_ only ever grows, so if the
        // cached value says there is room, there is definitely still room.
        if (head - cached_tail_ == capacity_) {
            // Looks full. Only now pay to read the consumer's cache line.
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (head - cached_tail_ == capacity_) {
                return false;
            }
        }

        buffer_[head & mask_] = item;

        // Release: the item write above must be visible to the consumer before
        // it can observe the new head. This store is what publishes the item.
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Returns how many were actually written -- may be fewer than n.
    std::size_t produce_n(T const* items, std::size_t n) {
        std::size_t const head = head_.load(std::memory_order_relaxed);

        std::size_t free_slots = capacity_ - (head - cached_tail_);
        if (free_slots < n) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            free_slots = capacity_ - (head - cached_tail_);
        }

        std::size_t const count = std::min(n, free_slots);
        for (std::size_t i = 0; i < count; ++i) {
            buffer_[(head + i) & mask_] = items[i];
        }

        // One release store for the whole batch instead of `count` of them.
        // This is where batching earns its keep.
        head_.store(head + count, std::memory_order_release);
        return count;
    }

    // consumer side

    // Pointer to the oldest item, or nullptr if empty. Valid only until the
    // next try_consume() -- after that the producer may overwrite the slot.
    T* try_peek() {
        std::size_t const tail = tail_.load(std::memory_order_relaxed);

        if (tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (tail == cached_head_) {
                return nullptr;
            }
        }
        return &buffer_[tail & mask_];
    }

    bool try_consume() {
        std::size_t const tail = tail_.load(std::memory_order_relaxed);

        if (tail == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (tail == cached_head_) {
                return false;
            }
        }

        // Release: our reads of the slot must complete before the producer can
        // see the slot as free and overwrite it.
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Copies up to n items into `out`, returns how many were actually read.
    std::size_t consume_n(T* out, std::size_t n) {
        std::size_t const tail = tail_.load(std::memory_order_relaxed);

        std::size_t available = cached_head_ - tail;
        if (available < n) {
            cached_head_ = head_.load(std::memory_order_acquire);
            available = cached_head_ - tail;
        }

        std::size_t const count = std::min(n, available);
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = buffer_[(tail + i) & mask_];
        }

        tail_.store(tail + count, std::memory_order_release);
        return count;
    }

    // observers - callable from either thread
    std::size_t size() const {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }

    bool empty() const { return size() == 0; }
    bool full() const { return size() == capacity_; }
    std::size_t capacity() const { return capacity_; }

private:
    T* buffer_;
    std::size_t capacity_;
    std::size_t mask_;    
    
    // Each index sits on its own cache line, together with the cached copy its
    // owner reads. kCacheLineSize is 128 on this machine, not 64 -- padding to
    // 64 here would leave head_ and tail_ sharing a line and quietly reinstate
    // the false sharing this is meant to prevent.
    alignas(kCacheLineSize) std::atomic<std::size_t> head_;
    std::size_t cached_tail_;

    alignas(kCacheLineSize) std::atomic<std::size_t> tail_;
    std::size_t cached_head_;
};

} // namespace tinyos
#pragma once
#include "tinyos/memory/allocator.hpp"
#include <cstddef>
#include <atomic>
#include <stdexcept>
#include <utility>

namespace tinyos {

template<typename T>
class SPSC {

public:
    explicit SPSC(std::size_t capacity) : capacity_(capacity), mask_(capacity - 1), 
        cached_head_(0), cached_tail_(0) {
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

    bool try_produce(T const& item);
    bool try_consume();
    T* try_peek();
    
    bool full() const;
    bool empty() const;
    std::size_t size() const;
    std::size_t capacity() const;

    std::size_t produce_n(T const* items, std::size_t n);
    std::size_t consume_n(T* out, std::size_t n); //out is a vector of T that consume_n writes to

private:
    T* buffer_;
    std::size_t capacity_;
    std::size_t mask_;    
    
    // cached values sit on diff cachelines
    alignas(64) std::atomic<std::size_t> head_;
    std::size_t cached_tail_;
    
    alignas(64) std::atomic<std::size_t> tail_;
    std::size_t cached_head_;
};

} // namespace tinyos
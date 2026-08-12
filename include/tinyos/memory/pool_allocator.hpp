#pragma once
#include "tinyos/memory/allocator.hpp"
#include <cstddef>
#include <stdexcept>

namespace tinyos {

class PoolAllocator final : public Allocator {

public:
    PoolAllocator(void* buffer, std::size_t capacity, std::size_t block_size, 
        std::size_t block_alignment)
        : base_(static_cast<std::byte*>(buffer)), capacity_(capacity), 
        block_alignment_(block_alignment), free_head_(nullptr) {
            
            // block size needs to be > size of FreeNode, because we cannot allocate a fraction of a FreeNode
            if (block_size < sizeof(FreeNode)) {
                throw std::invalid_argument("The block size cannot be smaller than the size of a FreeNode!");
            }

            else if (!is_power_of_two(block_alignment)) {
                throw std::invalid_argument("Block alignment needs to be a power of 2");
            }
            
            auto raw = reinterpret_cast<std::uintptr_t>(buffer);
            auto aligned = align_up(raw, block_alignment);
            base_ = reinterpret_cast<std::byte*>(aligned);

            block_size_ = align_up(block_size, block_alignment);

            capacity_ = ((static_cast<std::byte*>(buffer) + capacity) - base_);
            block_count_ = capacity_ / block_size_;
            stats_.bytes_reserved = block_count_ * block_size_;
            reset();
    }

    void * allocate(std::size_t bytes, 
        std::size_t alignment = alignof(std::max_align_t)) override;
    void deallocate(void *ptr, std::size_t bytes) override;
    void reset() override;
    const char * name() const override;

    std::size_t bytes_used() const;
    std::size_t bytes_remaining() const;

private:
    struct FreeNode {
        FreeNode* nextNode;
    };

    std::byte* base_;
    std::size_t capacity_;
    std::size_t block_size_;
    std::size_t block_count_;
    std::size_t blocks_free_;
    std::size_t block_alignment_;
    FreeNode* free_head_;
};

}

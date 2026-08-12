#include "tinyos/memory/pool_allocator.hpp"
#include <new>
#include <cassert>

namespace tinyos {

void* PoolAllocator::allocate(std::size_t bytes, std::size_t alignment) {
    if (alignment > block_alignment_) {
        ++stats_.failed_allocations;
        return nullptr;
    }

    else if (bytes > block_size_) {
        ++stats_.failed_allocations;
        return nullptr;
    }

    else if (blocks_free_ == 0) {
        ++stats_.failed_allocations;
        return nullptr;
    }

    void* out = free_head_;
    free_head_ = free_head_->nextNode;
    blocks_free_--;
    ++stats_.allocations;
    stats_.bytes_in_use += block_size_;
    return out;
}

void PoolAllocator::deallocate(void *ptr, std::size_t /*bytes*/) {
    
    if (ptr == nullptr) return;
    assert(ptr < base_ + (block_count_ * block_size_) && ptr >= base_);

    FreeNode* temp = free_head_;
    free_head_ = new (ptr) FreeNode();
    free_head_->nextNode = temp;

    blocks_free_++;
    stats_.deallocations++;
    stats_.bytes_in_use -= block_size_;
}

// resets the pool allocator, frees the buffer completely 
// Note: Objects in the memory space are not destroyed by the Pool Allocator
void PoolAllocator::reset() {
    
    if (block_count_ == 0) {
        free_head_ = nullptr;
        blocks_free_ = 0;
        stats_.bytes_in_use = 0;
        return;
    }
    
    free_head_ = new (base_) FreeNode();
    std::byte* curr = base_;
    FreeNode* currNode = free_head_;
    
    for (std::size_t i = 1; i < block_count_; i++) {
        curr += block_size_;
        FreeNode* newNode = new (curr) FreeNode();
        currNode->nextNode = newNode;
        currNode = newNode;
    }

    stats_.bytes_in_use = 0;
    blocks_free_ = block_count_;
}
    
const char* PoolAllocator::name() const {
    return "pool";
}

std::size_t PoolAllocator::bytes_used() const {
    return (block_count_ - blocks_free_) * block_size_;
}

std::size_t PoolAllocator::bytes_remaining() const {
    return blocks_free_ * block_size_;
}

} // namespace tinyos
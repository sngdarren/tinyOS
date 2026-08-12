#include "tinyos/memory/bump_allocator.hpp"

namespace tinyos {

void* BumpAllocator::allocate(std::size_t bytes, std::size_t alignment) {

    if (align_up(offset_, alignment) + bytes > capacity_) {
        ++stats_.failed_allocations;
        return nullptr;
    }

    offset_ = align_up(offset_, alignment);

    void* ret = base_ + offset_;

    offset_ += bytes;
    stats_.bytes_in_use = offset_;
    ++stats_.allocations;
    return ret;
}

void BumpAllocator::deallocate(void* /*ptr*/, std::size_t /*bytes*/) {
    ++stats_.deallocations;
}

void BumpAllocator::reset() {
    stats_.bytes_in_use = 0;
    offset_ = 0;
}   

const char* BumpAllocator::name() const {
    return "bump";
}
    
std::size_t BumpAllocator::bytes_used() const {
    return stats_.bytes_in_use;
}

std::size_t BumpAllocator::bytes_remaining() const {
    return capacity_ - stats_.bytes_in_use;
}

} // namespace tinyos

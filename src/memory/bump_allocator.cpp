#include "tinyos/memory/bump_allocator.hpp"

namespace tinyos {

void* BumpAllocator::allocate(std::size_t bytes, std::size_t alignment) {
    offset_ = align_up(offset_, alignment);

    if (offset_ + bytes > capacity_) {
        ++stats_.failed_allocations;
        return nullptr;
    }

    void* ret = base_ + offset_;

    offset_ += bytes;
    stats_.bytes_in_use = offset_;
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

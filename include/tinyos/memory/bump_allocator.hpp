#pragma once
#include "tinyos/memory/allocator.hpp"
#include <cstddef>

namespace tinyos {

class BumpAllocator final : public Allocator {
public:

    BumpAllocator(void* buffer, std::size_t capacity) 
        : base_(static_cast<std::byte*> (buffer)), capacity_(capacity), 
        offset_(0) {
            stats_.bytes_reserved = capacity;
        }

    // override virtuals
    void * allocate(std::size_t bytes, 
        std::size_t alignment = alignof(std::max_align_t)) override;
    void deallocate(void *ptr, std::size_t bytes) override;
    void reset() override;
    const char * name() const override;
    
    std::size_t bytes_used() const;
    std::size_t bytes_remaining() const;

private:
    std::byte* base_;
    std::size_t capacity_;
    std::size_t offset_;
    
};

}

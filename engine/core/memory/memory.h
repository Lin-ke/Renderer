#pragma once

#include <cereal/cereal.hpp>
#include <cstdint>
#include <cstddef>
#include <memory_resource>

namespace memory {

enum class Category : uint32_t { General, Asset, RenderPacket, RenderGraph, Profiler, Count };

struct Stats {
    uint64_t current_bytes = 0;
    uint64_t peak_bytes = 0;
    uint64_t allocations = 0;
    template<class Archive> void serialize(Archive& ar) {
        ar(CEREAL_NVP(current_bytes), CEREAL_NVP(peak_bytes), CEREAL_NVP(allocations));
    }
};

// Explicit tracked allocations. Returns nullptr on allocation failure.
void* allocate(size_t bytes, size_t alignment = alignof(std::max_align_t), Category category = Category::General);
void deallocate(void* pointer);
Stats snapshot(Category category);
Stats snapshot();

// A pool/arena can use this as its upstream resource. Statistics count backing
// allocations once, rather than counting each suballocation as additional memory.
class Resource : public std::pmr::memory_resource {
public:
    explicit Resource(Category category) : category_(category) {}
private:
    void* do_allocate(size_t bytes, size_t alignment) override;
    void do_deallocate(void* pointer, size_t, size_t) override { memory::deallocate(pointer); }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    Category category_;
};

Resource* resource(Category category);

} // namespace memory

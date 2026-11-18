#include "memory.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <limits>

namespace memory {
namespace {
struct Counters {
    std::atomic<uint64_t> current{0}, peak{0}, allocations{0};
};
std::array<Counters, static_cast<size_t>(Category::Count)> counters;
Counters total;
struct Header { void* base; size_t bytes; Category category; };
void add(Counters& value, size_t bytes) {
    auto current = value.current.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    auto peak = value.peak.load(std::memory_order_relaxed);
    while (peak < current && !value.peak.compare_exchange_weak(peak, current, std::memory_order_relaxed)) {}
    value.allocations.fetch_add(1, std::memory_order_relaxed);
}
Stats read(const Counters& value) {
    return {value.current.load(std::memory_order_relaxed), value.peak.load(std::memory_order_relaxed),
        value.allocations.load(std::memory_order_relaxed)};
}
}

void* allocate(size_t bytes, size_t alignment, Category category) {
    if (category >= Category::Count || !alignment || (alignment & (alignment - 1))) return nullptr;
    alignment = std::max(alignment, alignof(Header));
    if (alignment > SIZE_MAX - sizeof(Header) || bytes > SIZE_MAX - alignment - sizeof(Header)) return nullptr;
    auto* base = std::malloc(bytes + alignment + sizeof(Header));
    if (!base) return nullptr;
    auto address = (reinterpret_cast<uintptr_t>(base) + sizeof(Header) + alignment - 1) & ~(uintptr_t(alignment) - 1);
    auto* header = reinterpret_cast<Header*>(address) - 1;
    *header = {base, bytes, category};
    add(counters[static_cast<size_t>(category)], bytes);
    add(total, bytes);
    return reinterpret_cast<void*>(address);
}

void deallocate(void* pointer) {
    if (!pointer) return;
    auto* header = reinterpret_cast<Header*>(pointer) - 1;
    counters[static_cast<size_t>(header->category)].current.fetch_sub(header->bytes, std::memory_order_relaxed);
    total.current.fetch_sub(header->bytes, std::memory_order_relaxed);
    std::free(header->base);
}
Stats snapshot(Category category) { return read(counters[static_cast<size_t>(category)]); }
Stats snapshot() { return read(total); }
void* Resource::do_allocate(size_t bytes, size_t alignment) {
    auto* pointer = memory::allocate(bytes, alignment, category_);
    // memory_resource must return usable storage. This engine does not use exceptions.
    if (!pointer) std::abort();
    return pointer;
}
Resource* resource(Category category) {
    static Resource resources[] = {Resource(Category::General), Resource(Category::Asset),
        Resource(Category::RenderPacket), Resource(Category::RenderGraph), Resource(Category::Profiler)};
    return &resources[static_cast<size_t>(category)];
}
} // namespace memory

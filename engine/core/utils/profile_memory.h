#pragma once

#include <cereal/cereal.hpp>
#include <cstdint>
#include <string>
#include <vector>

// Values are copied out by the device thread; no native object escapes in a snapshot.
struct ProfileGpuAllocation {
    uint64_t id = 0;
    uint64_t bytes = 0;
    std::string name;
    std::string category;
    bool external = false;
    bool cached = false;
    template<class Archive> void serialize(Archive& ar) {
        ar(CEREAL_NVP(id), CEREAL_NVP(bytes), CEREAL_NVP(name), CEREAL_NVP(category),
            CEREAL_NVP(external), CEREAL_NVP(cached));
    }
};

struct ProfileGpuMemory {
    uint64_t allocated_bytes = 0;
    uint64_t peak_bytes = 0;
    uint64_t cached_bytes = 0;
    uint64_t pending_release_bytes = 0;
    uint64_t external_bytes = 0;
    uint64_t upload_bytes = 0;
    uint64_t readback_bytes = 0;
    uint64_t local_usage = 0;
    uint64_t local_budget = 0;
    uint64_t nonlocal_usage = 0;
    uint64_t nonlocal_budget = 0;
    bool budget_available = false;
    std::vector<ProfileGpuAllocation> allocations;
    template<class Archive> void serialize(Archive& ar) {
        ar(CEREAL_NVP(allocated_bytes), CEREAL_NVP(peak_bytes), CEREAL_NVP(cached_bytes),
            CEREAL_NVP(pending_release_bytes), CEREAL_NVP(external_bytes), CEREAL_NVP(upload_bytes),
            CEREAL_NVP(readback_bytes), CEREAL_NVP(local_usage), CEREAL_NVP(local_budget),
            CEREAL_NVP(nonlocal_usage), CEREAL_NVP(nonlocal_budget), CEREAL_NVP(budget_available),
            CEREAL_NVP(allocations));
    }
};

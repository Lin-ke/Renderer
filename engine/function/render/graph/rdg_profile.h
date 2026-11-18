#pragma once

#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cstdint>
#include <string>
#include <vector>

struct RDGProfileAccess {
    uint32_t resource_id;
    bool output;
    template<class Archive> void serialize(Archive& ar) { ar(CEREAL_NVP(resource_id), CEREAL_NVP(output)); }
};
struct RDGProfilePass {
    uint32_t id, type, order;
    std::string name;
    bool culled;
    std::vector<uint32_t> dependencies;
    std::vector<RDGProfileAccess> accesses;
    template<class Archive> void serialize(Archive& ar) {
        ar(CEREAL_NVP(id), CEREAL_NVP(type), CEREAL_NVP(order), CEREAL_NVP(name),
            CEREAL_NVP(culled), CEREAL_NVP(dependencies), CEREAL_NVP(accesses));
    }
};
struct RDGProfileResource {
    uint32_t id, type, first_use, last_use;
    std::string name;
    bool imported;
    uint64_t allocation_id = 0;
    template<class Archive> void serialize(Archive& ar) {
        ar(CEREAL_NVP(id), CEREAL_NVP(type), CEREAL_NVP(first_use), CEREAL_NVP(last_use),
            CEREAL_NVP(name), CEREAL_NVP(imported), CEREAL_NVP(allocation_id));
    }
};
struct RDGProfile {
    uint64_t frame_id;
    std::vector<RDGProfilePass> passes;
    std::vector<RDGProfileResource> resources;
    template<class Archive> void serialize(Archive& ar) {
        ar(CEREAL_NVP(frame_id), CEREAL_NVP(passes), CEREAL_NVP(resources));
    }
};

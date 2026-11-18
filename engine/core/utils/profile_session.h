#pragma once

#include "engine/core/memory/memory.h"
#include "profile_memory.h"
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <atomic>
#include <array>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <map>
#include <string>
#include <vector>

struct ProfileContext {
    uint64_t frame_id = UINT64_MAX;
    uint64_t operation_id = 0;
    uint32_t pass_id = UINT32_MAX;
    template<class Archive> void serialize(Archive& ar) {
        ar(CEREAL_NVP(frame_id), CEREAL_NVP(operation_id), CEREAL_NVP(pass_id));
    }
};

struct ProfileSpan {
    uint64_t start = 0, generation = 0, thread = 0;
    ProfileContext context;
    std::string name;
};

struct ProfileMemorySample {
    std::string name;
    double time_ms = 0;
    uint64_t process_private_bytes = 0, working_set_bytes = 0;
    memory::Stats tracked;
    bool gpu_available = false;
    ProfileGpuMemory gpu;
    template<class Archive> void serialize(Archive& ar) {
        ar(CEREAL_NVP(name), CEREAL_NVP(time_ms), CEREAL_NVP(process_private_bytes),
            CEREAL_NVP(working_set_bytes), CEREAL_NVP(tracked), CEREAL_NVP(gpu_available), CEREAL_NVP(gpu));
    }
};

// An opt-in capture, independent of UI frame history. Finish producers before end().
class ProfileSession {
public:
    static ProfileSession& instance();
    bool begin(std::string name);
    void end();
    uint64_t generation() const { return active_.load(std::memory_order_relaxed); }
    static ProfileContext context();
    static void set_context(ProfileContext context);
    static uint64_t thread_id();
    ProfileSpan begin_operation(std::string name);
    void end_operation(const ProfileSpan& span);
    void record(const char* name, uint64_t start, uint64_t end, ProfileContext context,
        uint64_t generation, uint64_t thread = 0);
    void sample_memory(std::string name, std::optional<ProfileGpuMemory> gpu = std::nullopt);
    void set_metadata(std::string key, std::string value);
    void name_thread(std::string name);
    // Device thread adds data-only records; JSON payloads are produced by RDG/GPU capture.
    void add_graph(std::string json);
    void add_gpu_frame(std::string json);
    // Writes JSON and a same-stem offline HTML report; true only when both succeed.
    // A .html path selects the HTML filename and a sibling .json filename.
    bool write(const std::filesystem::path& path) const;
    void print() const;
private:
    struct Event {
        char name[96]{};
        uint64_t start = 0, end = 0, thread = 0, generation = 0;
        ProfileContext context;
    };
    struct Buffer {
        std::mutex mutex;
        std::array<Event, 16384> events;
        size_t count = 0;
        uint64_t dropped = 0;
    };
    Buffer* buffer();
    std::atomic<uint64_t> active_{0};
    uint64_t next_generation_ = 0, start_ = 0, end_ = 0;
    std::atomic<uint64_t> next_operation_{1};
    std::string name_;
    std::map<std::string, std::string> metadata_;
    std::map<uint64_t, std::string> thread_names_;
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<Buffer>> buffers_;
    std::vector<ProfileMemorySample> memory_samples_;
    std::vector<std::string> graphs_, gpu_frames_;
};

class ProfileContextScope {
public:
    explicit ProfileContextScope(ProfileContext context) : previous_(ProfileSession::context()) { ProfileSession::set_context(context); }
    ~ProfileContextScope() { ProfileSession::set_context(previous_); }
    ProfileContextScope(const ProfileContextScope&) = delete;
    ProfileContextScope& operator=(const ProfileContextScope&) = delete;
private:
    ProfileContext previous_;
};

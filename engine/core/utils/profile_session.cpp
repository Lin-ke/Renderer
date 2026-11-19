#include "profile_session.h"
#include "cpu_profiler.h"
#include "profile_report.h"
#include "profiler.h"
#include "engine/core/os/thread_pool.h"

#include <cereal/archives/json.hpp>
#include <cereal/external/rapidjson/document.h>
#include <cereal/external/rapidjson/ostreamwrapper.h>
#include <cereal/external/rapidjson/writer.h>
#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace json = CEREAL_RAPIDJSON_NAMESPACE;
namespace {
thread_local ProfileContext current_context;
double milliseconds(uint64_t ticks) { return double(ticks) * 1000.0 / CpuProfiler::get_cpu_frequency(); }
}

ProfileSession& ProfileSession::instance() { static ProfileSession session; return session; }
ProfileContext ProfileSession::context() { return current_context; }
void ProfileSession::set_context(ProfileContext context) { current_context = context; }
uint64_t ProfileSession::thread_id() { return GetCurrentThreadId(); }

bool ProfileSession::begin(std::string name) {
    std::scoped_lock lock(mutex_);
    if (generation() || pending_graphs_ || ending_) return false;
    for (auto& buffer : buffers_) {
        std::scoped_lock buffer_lock(buffer->mutex);
        buffer->count = 0;
        buffer->dropped = 0;
    }
    name_ = std::move(name);
    metadata_.clear();
    memory_samples_.clear(); graphs_.clear(); gpu_frames_.clear();
    dropped_graphs_ = 0;
    start_ = CpuProfiler::get_timestamp(); end_ = 0;
    active_.store(++next_generation_, std::memory_order_release);
    return true;
}
void ProfileSession::end() {
    std::unique_lock lock(mutex_);
    if (!generation()) return;
    ending_ = true;
    graph_condition_.wait(lock, [this] { return pending_graphs_ == 0; });
    end_ = CpuProfiler::get_timestamp();
    active_.store(0, std::memory_order_release);
    ending_ = false;
}
ProfileSession::Buffer* ProfileSession::buffer() {
    static thread_local Buffer* local = nullptr;
    if (!local) {
        std::scoped_lock lock(mutex_);
        buffers_.push_back(std::allocate_shared<Buffer>(std::pmr::polymorphic_allocator<Buffer>(memory::resource(memory::Category::Profiler))));
        local = buffers_.back().get();
    }
    return local;
}
void ProfileSession::record(const char* name, uint64_t start, uint64_t end, ProfileContext context,
    uint64_t captured_generation, uint64_t thread) {
    if (!captured_generation || captured_generation != generation()) return;
    auto* destination = buffer();
    std::scoped_lock lock(destination->mutex);
    if (captured_generation != generation()) return;
    if (destination->count == destination->events.size()) { ++destination->dropped; return; }
    auto& event = destination->events[destination->count++];
    std::strncpy(event.name, name ? name : "", sizeof(event.name) - 1);
    event.name[sizeof(event.name) - 1] = 0;
    event.start = start; event.end = end; event.context = context;
    event.thread = thread ? thread : thread_id(); event.generation = captured_generation;
}
ProfileSpan ProfileSession::begin_operation(std::string name) {
    auto context = current_context;
    context.operation_id = next_operation_.fetch_add(1, std::memory_order_relaxed);
    return {CpuProfiler::get_timestamp(), generation(), thread_id(), context, std::move(name)};
}
void ProfileSession::end_operation(const ProfileSpan& span) {
    record(span.name.c_str(), span.start, CpuProfiler::get_timestamp(), span.context, span.generation, span.thread);
}
void ProfileSession::sample_memory(std::string name, std::optional<ProfileGpuMemory> gpu) {
    if (!generation()) return;
    PROCESS_MEMORY_COUNTERS_EX process{};
    process.cb = sizeof(process);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process), sizeof(process));
    ProfileMemorySample sample{std::move(name), milliseconds(CpuProfiler::get_timestamp() - start_),
        process.PrivateUsage, process.WorkingSetSize, memory::snapshot(), gpu.has_value(), gpu ? std::move(*gpu) : ProfileGpuMemory{}};
    std::scoped_lock lock(mutex_);
    memory_samples_.push_back(std::move(sample));
}
void ProfileSession::set_metadata(std::string key, std::string value) {
    std::scoped_lock lock(mutex_);
    metadata_[std::move(key)] = std::move(value);
}
void ProfileSession::name_thread(std::string name) {
    std::scoped_lock lock(mutex_);
    thread_names_[thread_id()] = std::move(name);
}
bool ProfileSession::enqueue_graph(ThreadPool* pool, uint64_t captured_generation, size_t snapshot_bytes,
    std::function<std::string()> serialize) {
    constexpr size_t max_pending_graphs = 32;
    constexpr size_t max_pending_bytes = 64 * 1024 * 1024;
    size_t index;
    {
        std::scoped_lock lock(mutex_);
        if (!captured_generation || captured_generation != generation() || ending_) return false;
        if (!pool || pending_graphs_ >= max_pending_graphs || snapshot_bytes > max_pending_bytes - pending_graph_bytes_) {
            ++dropped_graphs_;
            return false;
        }
        index = graphs_.size();
        graphs_.emplace_back();
        ++pending_graphs_; pending_graph_bytes_ += snapshot_bytes;
    }
    // ThreadPool uses packaged_task; discarding its future does not join the task.
    pool->enqueue([this, captured_generation, snapshot_bytes, index, serialize = std::move(serialize)]() mutable {
        std::string json;
        {
            PROFILE_SCOPE("RDG_SerializeJSON");
            json = serialize();
        }
        {
            std::scoped_lock lock(mutex_);
            if (captured_generation == next_generation_) graphs_[index] = std::move(json);
            --pending_graphs_; pending_graph_bytes_ -= snapshot_bytes;
        }
        graph_condition_.notify_all();
    });
    return true;
}
void ProfileSession::add_gpu_frame(std::string json) {
    if (!generation()) return;
    std::scoped_lock lock(mutex_);
    gpu_frames_.push_back(std::move(json));
}

bool ProfileSession::write(const std::filesystem::path& path) const {
    std::unique_lock lock(mutex_);
    if (generation() || !end_ || pending_graphs_ || ending_) return false;
    std::error_code error;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    auto json_path = path;
    auto html_path = path;
    auto extension = path.extension().wstring();
    for (auto& character : extension) {
        if (character >= L'A' && character <= L'Z') character += L'a' - L'A';
    }
    if (extension == L".html") json_path.replace_extension(".json");
    else html_path.replace_extension(".html");
    std::ostringstream output;
    json::OStreamWrapper stream(output);
    json::Writer<json::OStreamWrapper> writer(stream);
    writer.StartObject();
    writer.Key("schema_version"); writer.Uint(1);
    writer.Key("name"); writer.String(name_.c_str());
    writer.Key("duration_ms"); writer.Double(milliseconds(end_ - start_));
    writer.Key("cpu_memory_coverage"); writer.String("Explicit memory:: allocations; process memory is reported separately");
    writer.Key("metadata"); writer.StartObject();
    for (const auto& [key, value] : metadata_) { writer.Key(key.c_str()); writer.String(value.c_str()); }
    writer.EndObject();
    writer.Key("traceEvents"); writer.StartArray();
    for (const auto& [id, name] : thread_names_) {
        writer.StartObject(); writer.Key("ph"); writer.String("M");
        writer.Key("name"); writer.String("thread_name");
        writer.Key("pid"); writer.Uint(1); writer.Key("tid"); writer.Uint64(id);
        writer.Key("args"); writer.StartObject(); writer.Key("name"); writer.String(name.c_str());
        writer.EndObject(); writer.EndObject();
    }
    uint64_t dropped = 0;
    for (auto& buffer : buffers_) {
        std::scoped_lock buffer_lock(buffer->mutex);
        dropped += buffer->dropped;
        for (size_t i = 0; i < buffer->count; ++i) {
            const auto& event = buffer->events[i];
            if (event.generation != next_generation_) continue;
            writer.StartObject();
            writer.Key("name"); writer.String(event.name);
            writer.Key("cat"); writer.String("cpu");
            writer.Key("ph"); writer.String("X");
            writer.Key("pid"); writer.Uint(1);
            writer.Key("tid"); writer.Uint64(event.thread);
            writer.Key("ts"); writer.Double(milliseconds(event.start - start_) * 1000.0);
            writer.Key("dur"); writer.Double(milliseconds(event.end - event.start) * 1000.0);
            writer.Key("args"); writer.StartObject();
            writer.Key("frame_id"); writer.Uint64(event.context.frame_id);
            writer.Key("operation_id"); writer.Uint64(event.context.operation_id);
            writer.Key("pass_id"); writer.Uint(event.context.pass_id);
            writer.EndObject(); writer.EndObject();
        }
    }
    writer.EndArray();
    writer.Key("dropped_cpu_events"); writer.Uint64(dropped);
    writer.Key("dropped_graphs"); writer.Uint64(dropped_graphs_);
    writer.Key("graphs"); writer.StartArray();
    for (const auto& graph : graphs_) writer.RawValue(graph.data(), graph.size(), json::kObjectType);
    writer.EndArray();
    writer.Key("gpu_frames"); writer.StartArray();
    for (const auto& frame : gpu_frames_) writer.RawValue(frame.data(), frame.size(), json::kObjectType);
    writer.EndArray();
    std::ostringstream serialized;
    { cereal::JSONOutputArchive archive(serialized); archive(cereal::make_nvp("samples", memory_samples_)); }
    json::Document document;
    document.Parse(serialized.str().c_str());
    writer.Key("memory"); document.Accept(writer);
    writer.EndObject();
    const auto report = output.str();
    std::ofstream file(json_path, std::ios::binary);
    if (!file) return false;
    file.write(report.data(), static_cast<std::streamsize>(report.size()));
    file.close();
    return bool(file) && write_profile_html(html_path, report);
}
void ProfileSession::print() const {
    std::scoped_lock lock(mutex_);
    std::printf("Profile %s: %.2f ms\n", name_.c_str(), milliseconds(end_ - start_));
    for (auto& buffer : buffers_) {
        std::scoped_lock buffer_lock(buffer->mutex);
        for (size_t i = 0; i < buffer->count; ++i) {
            const auto& event = buffer->events[i];
            if (event.context.operation_id && event.context.frame_id == UINT64_MAX &&
                (!std::strncmp(event.name, "Load", 4) || !std::strncmp(event.name, "Engine", 6) ||
                 !std::strncmp(event.name, "First", 5)))
                std::printf("  %s: %.2f ms\n", event.name, milliseconds(event.end - event.start));
        }
    }
    for (const auto& sample : memory_samples_) {
        constexpr double mb = 1024.0 * 1024.0;
        if (!sample.gpu_available) {
            std::printf("  %s: process private %.1f MiB, tracked CPU %.1f MiB; GPU unavailable\n",
                sample.name.c_str(), sample.process_private_bytes / mb, sample.tracked.current_bytes / mb);
            continue;
        }
        std::printf("  %s: process private %.1f MiB, tracked CPU %.1f MiB, GPU owned %.1f MiB (peak %.1f, cached %.1f), external %.1f MiB\n",
            sample.name.c_str(), sample.process_private_bytes / mb, sample.tracked.current_bytes / mb,
            sample.gpu.allocated_bytes / mb, sample.gpu.peak_bytes / mb, sample.gpu.cached_bytes / mb,
            sample.gpu.external_bytes / mb);
    }
}

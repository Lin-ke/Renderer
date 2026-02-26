#include "asset_manager.h"
#include "engine/core/log/Log.h"
#include "engine/core/os/thread_pool.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/main/engine_context.h"
#include "engine/core/utils/profiler.h"

#include <algorithm>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/vector.hpp>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <future>
#include <optional>
#include <ranges>
#include <vector>


namespace fs = std::filesystem;

DEFINE_LOG_TAG(LogAsset, "Asset"); // Define the tag here

constexpr bool use_thread_pool = true;
struct AssetUID {
	UID uid;

	template <class Archive>
	void serialize(Archive &ar) {
		ar(cereal::make_nvp("uid", uid));
	}
};

struct AssetHeader : public AssetUID {
	AssetDeps deps;

	template <class Archive>
	void serialize(Archive &ar) {
		AssetUID::serialize(ar);
		ar(cereal::make_nvp("deps", deps));
	}
};

struct AssetFile : public AssetHeader {
	AssetRef asset;

	template <class Archive>
	void serialize(Archive &ar) {
		AssetHeader::serialize(ar);
		ar(cereal::make_nvp("asset", asset));
	}
};

template <typename T, typename Func>
static auto with_asset_read(const fs::path &path, Func &&func)
		-> std::invoke_result_t<Func, T &&> {
	using ResultT = std::invoke_result_t<Func, T &&>;

	if (std::ifstream ifs(path, std::ios::binary); !ifs.is_open()) {
		ERR(LogAsset, "Failed to open file: {}", path.string());
		return ResultT{};
	} else {
		try {
			T data_container;
			auto ext = path.extension();
			if (ext == ".binasset") {
				cereal::BinaryInputArchive ar(ifs);
				data_container.serialize(ar);
			} else if (ext == ".asset") {
				cereal::JSONInputArchive ar(ifs);
				data_container.serialize(ar);
			} else {
				ERR(LogAsset, "Unknown asset format: {}", path.string());
				return ResultT{};
			}
			return func(std::move(data_container));
		} catch (const std::exception &e) {
			ERR(LogAsset, "Asset read error {}: {}", path.string(), e.what());
		}
	}
	return ResultT{};
}

template <typename Func>
static void with_asset_write(const fs::path &path, Func &&func) {
	auto ext = path.extension();
	if (ext != ".binasset" && ext != ".asset") {
		ERR(LogAsset, "Unsupported save format: {}", path.string());
		return;
	}

	if (path.has_parent_path()) {
		std::error_code ec;
		fs::create_directories(path.parent_path(), ec);
	}

	if (std::ofstream ofs(path, std::ios::binary); !ofs.is_open()) {
		ERR(LogAsset, "Failed to open file for writing: {}", path.string());
		return;
	} else {
		AssetFile file_data;
		func(file_data); // 填充数据

		try {
			if (ext == ".binasset") {
				cereal::BinaryOutputArchive ar(ofs);
				file_data.serialize(ar);
			} else if (ext == ".asset") {
				cereal::JSONOutputArchive ar(ofs);
				file_data.serialize(ar);
			}
		} catch (const std::exception &e) {
			ERR(LogAsset, "Asset write error {}: {}", path.string(), e.what());
		}
	}
}

template <typename T>
static std::shared_future<T> make_ready_future(const T &value) {
	std::promise<T> promise;
	promise.set_value(value);
	return promise.get_future().share();
}

AssetManager::~AssetManager() = default;

void AssetManager::init(const fs::path &game_path) {
	engine_path_ = fs::absolute(ENGINE_PATH) / "assets";
	game_path_ = game_path.empty() ? engine_path_ : fs::absolute(game_path) / "assets";

	for (const auto &p : game_path == fs::path{} ? std::vector<fs::path>{ engine_path_ } : std::vector<fs::path>{ engine_path_, game_path_ }) {
		if (fs::exists(p)) {
			scan_directory(p);
		} else {
			fs::create_directories(p);
			INFO(LogAsset, "Created asset directory: {}", p.string());
		}
	}
}

void AssetManager::tick() {}

AssetDeps AssetManager::peek_asset_deps(const fs::path &path) {
	return with_asset_read<AssetHeader>(path, [](AssetHeader &&header) {
		return header.deps;
	});
}

UID AssetManager::peek_uid_from_file(const fs::path &path) {
	return with_asset_read<AssetUID>(path, [](AssetUID &&uid) {
		return uid.uid;
	});
}

AssetRef AssetManager::perform_load_from_disk(UID uid, const fs::path &path) {
	PROFILE_SCOPE("AssetManager::load_from_disk");
	return with_asset_read<AssetFile>(path, [&](AssetFile &&file) -> AssetRef {
		return file.asset;
	});
}

void AssetManager::perform_save_to_disk(AssetRef asset, const fs::path &phys_path) {
	PROFILE_SCOPE("AssetManager::save_to_disk");
	if (!asset) {
		return;
	}

	with_asset_write(phys_path, [&](AssetFile &file_data) {
		file_data.uid = asset->get_uid();
        std::unordered_set<UID> dep_uids;
		asset->traverse_deps([&](AssetRef dep) {
			if (dep) {
                UID uid = dep->get_uid();
                if (!dep_uids.contains(uid)) {
                    dep_uids.insert(uid);
                    file_data.deps.push_back(uid);
                }
            }
		});
		file_data.asset = asset;

		INFO(LogAsset, "Saved asset {} to {}", file_data.uid.to_string(), phys_path.string());
	});

	register_asset(asset, phys_path.generic_string());
}

void AssetManager::collect_dependencies_recursive(UID uid, std::vector<UID> &sorted_uids, std::unordered_set<UID> &visited) {
	if (uid.is_empty() || visited.contains(uid)) {
		return;
	}

	auto it = uid_to_path_.find(uid);
	if (it == uid_to_path_.end()) {
		visited.insert(uid);
		return;
	}

	visited.insert(uid);
	for (const auto &dep_uid : peek_asset_deps(it->second)) {
		collect_dependencies_recursive(dep_uid, sorted_uids, visited);
	}
	sorted_uids.push_back(uid);
}

std::optional<std::shared_future<AssetRef>> AssetManager::check_pending_cache(UID uid) {
	std::scoped_lock lock(asset_mutex_);
	if (assets_.contains(uid)) {
		return make_ready_future(assets_[uid]);
	}
	if (pending_assets_.contains(uid)) {
		return pending_assets_[uid];
	}
	return std::nullopt;
}

std::vector<std::shared_future<AssetRef>> AssetManager::enqueue_load_task(UID uid) {
	// Resolve Dependencies
	std::vector<UID> load_order;
	std::unordered_set<UID> visited;
	collect_dependencies_recursive(uid, load_order, visited); // include self

	auto *pool = EngineContext::thread_pool();
	std::vector<std::shared_future<AssetRef>> futures;
	futures.reserve(load_order.size());

	// Phase 1: Under lock, plan all tasks and collect paths
	struct LoadTask {
		UID uid;
		fs::path disk_path;
		std::shared_ptr<std::promise<AssetRef>> promise;
		std::shared_future<AssetRef> future;
	};
	std::vector<LoadTask> tasks_to_execute;

	{
		std::scoped_lock lock(asset_mutex_);
		for (const auto &dep_uid : load_order) {
			// Skip if already ready or processing
			if (assets_.count(dep_uid)) {
				futures.push_back(make_ready_future(assets_[dep_uid]));
				continue;
			}
			if (pending_assets_.count(dep_uid)) {
				futures.push_back(pending_assets_[dep_uid]);
				continue;
			}
			if (!uid_to_path_.contains(dep_uid)) {
				ERR(LogAsset, "Asset UID {} has no registered path.", dep_uid.to_string());
				futures.push_back(make_ready_future<AssetRef>(nullptr));
				continue;
			}

			auto promise = std::make_shared<std::promise<AssetRef>>();
			std::shared_future<AssetRef> future = promise->get_future().share();
			pending_assets_[dep_uid] = future;
			futures.push_back(future);

			tasks_to_execute.push_back({dep_uid, uid_to_path_[dep_uid], promise, future});
		}
	} // unlock

	// Phase 2: Execute all load tasks without holding the lock
	for (auto &task : tasks_to_execute) {
		auto task_lambda = [this, dep_uid = task.uid, disk_path = task.disk_path, promise = task.promise]() {
			AssetRef loaded = this->perform_load_from_disk(dep_uid, disk_path);
			{
				std::scoped_lock inner_lock(this->asset_mutex_);
				if (loaded) {
					this->assets_[dep_uid] = loaded;
				}
				this->pending_assets_.erase(dep_uid);
			}
			promise->set_value(loaded);
		};

		if (pool && use_thread_pool) {
			pool->enqueue(std::move(task_lambda));
		} else {
			task_lambda();
		}
	}

	return futures;
}

AssetRef AssetManager::load_asset_blocking(UID uid) {
	PROFILE_SCOPE("AssetManager::load_asset_blocking");
	if (uid.is_empty()) {
		return nullptr;
	}
	if (auto cached = check_pending_cache(uid); cached.has_value()) {
		return cached->get();
	}

	auto futures = enqueue_load_task(uid);
	for (auto &fut : futures) {
		fut.wait();
	}
	AssetRef result = nullptr;
	for (auto &fut : futures) {
		auto asset = fut.get();
		if (!asset) continue;

		if (asset->get_uid() == uid) {
			result = asset;
		}

		// Double-check under init_mutex_ to avoid double initialization
		if (!asset->is_initialized()) {
			std::unique_lock<std::mutex> init_lock(asset->init_mutex_);
			if (!asset->is_initialized()) {
				asset->on_load_asset();
				asset->mark_initialized();
			}
		}
	}
	if (result == nullptr) {
		ERR(LogAsset, "Failed to load asset UID: {}, See logs above.", uid.to_string());
	}
	return result;
}

AssetRef AssetManager::get_asset_immediate(UID uid) {
	std::scoped_lock lock(asset_mutex_);
	if (auto it = assets_.find(uid); it != assets_.end()) {
		return it->second;
	}
	return nullptr;
}

void AssetManager::unload_asset(UID uid) {
	std::scoped_lock lock(asset_mutex_);
	assets_.erase(uid);
}

size_t AssetManager::unload_unused() {
	std::scoped_lock lock(asset_mutex_);
	size_t count = 0;
	for (auto it = assets_.begin(); it != assets_.end(); ) {
		// use_count == 1 means only the cache holds a reference
		if (it->second.use_count() == 1) {
			it = assets_.erase(it);
			++count;
		} else {
			++it;
		}
	}
	return count;
}

void AssetManager::scan_directory(const fs::path &dir_path) {
	std::vector<fs::path> paths;
	for (auto &entry : fs::recursive_directory_iterator(dir_path)) {
		if (entry.is_regular_file()) {
			auto ext = entry.path().extension();
			if (ext == ".asset" || ext == ".binasset") {
				paths.push_back(entry.path());
			}
		}
	}

	auto *pool = EngineContext::thread_pool();
	std::vector<std::pair<UID, std::string>> results(paths.size());
	std::vector<std::future<void>> futures;

	for (size_t i = 0; i < paths.size(); ++i) {
		auto task = [this, &results, i, p = paths[i]]() {
			results[i] = { peek_uid_from_file(p), p.generic_string() };
		};
		if (pool && use_thread_pool) {
			futures.push_back(pool->enqueue(std::move(task)));
		} else {
			task();
		}
	}
	for (auto &f : futures) {
		f.wait();
	}

	for (const auto &[uid, path_str] : results) {
		if (!uid.is_empty()) {
			register_path(uid, path_str);
		}
	}
}

void AssetManager::register_path_internal(UID uid, const std::string &path) {
	if (auto it = uid_to_path_.find(uid); it != uid_to_path_.end()) {
		path_to_uid_.erase(it->second);
	}
	uid_to_path_[uid] = path;
	path_to_uid_[path] = uid;
}

void AssetManager::register_path(UID uid, const std::string &path) {
	std::scoped_lock lock(asset_mutex_);
	register_path_internal(uid, path);
}

void AssetManager::register_asset(AssetRef asset, const std::string &path) {
	if (!asset) {
		return;
	}
	std::scoped_lock lock(asset_mutex_);
	register_path_internal(asset->get_uid(), path);
	assets_[asset->get_uid()] = asset;
}

void AssetManager::collect_save_dependencies_recursive(AssetRef asset, std::vector<AssetRef> &sorted_assets, std::unordered_set<UID> &visited,
		std::unordered_set<UID> &visiting) {
	if (!asset) {
		return;
	}
	UID uid = asset->get_uid();
	if (uid.is_empty()) {
		// Auto-generate UID for dependencies without one
		asset->set_uid(UID::generate());
		uid = asset->get_uid();
		INFO(LogAsset, "Auto-generated UID {} for {} dependency", uid.to_string(), asset->get_asset_type_name());
	}
	if (visited.count(uid)) {
		return;
	}

    if (visiting.count(uid)) {
        ERR(LogAsset, "Circular dependency detected involving asset: {}", uid.to_string());
        return; 
    }

    visiting.insert(uid);
	
	asset->traverse_deps([&](AssetRef dep_asset) {
			if (dep_asset) {
				// INFO(LogAsset, "Asset {} depends on {}", uid.to_string(), dep_asset->get_uid().to_string());
			}
			collect_save_dependencies_recursive(dep_asset, sorted_assets, visited, visiting);
	});

    visiting.erase(uid);
    visited.insert(uid);
	sorted_assets.push_back(asset);
}

static std::string type_to_ext(AssetType type) {
    switch (type) {
        case AssetType::Model:
        case AssetType::Scene:
        case AssetType::Prefab:
            return ".asset";
        default:
            return ".binasset";
    }
}

void AssetManager::save_asset(AssetRef asset, const std::string &virtual_path) {
	PROFILE_SCOPE("AssetManager::save_asset");
	if (!asset) {
		return;
	}

	// Auto-generate UID if the asset doesn't have one
	if (asset->get_uid().is_empty()) {
		asset->set_uid(UID::generate());
	}

	// 1. Resolve Target Path
	fs::path v_path(virtual_path);
	auto physical_path_opt = get_physical_path(v_path.generic_string());
	if (!physical_path_opt) {
		ERR(LogAsset, "Invalid virtual path for saving: {}", virtual_path);
		return;
	}
	fs::path root_phys_path = *physical_path_opt;

	// Register root asset immediately to ensure dependencies can resolve it if needed
	register_asset(asset, root_phys_path.generic_string());

	// 2. Collect Dependencies (Snapshot)
	std::vector<AssetRef> sorted_assets;
	std::unordered_set<UID> visited_uids;
    std::unordered_set<UID> visiting_uids;
	collect_save_dependencies_recursive(asset, sorted_assets, visited_uids, visiting_uids);

	INFO(LogAsset, "Saving asset {} ({}) to {} with {} dependencies.", 
         asset->get_uid().to_string(), 
         asset->get_asset_type_name(),
         virtual_path,
         sorted_assets.size() - 1);

	// 3. Parallel Save
	auto *pool = EngineContext::thread_pool();
	std::vector<std::future<void>> futures;

	for (auto &asset_to_save : sorted_assets) {
		fs::path save_path;
		bool is_new = false;

		if (asset_to_save == asset) {
			save_path = root_phys_path;
			INFO(LogAsset, "Root asset {} save_path: {}, is_dirty: {}", 
			     asset_to_save->get_uid().to_string(), save_path.string(), asset_to_save->is_dirty());
		} else {
			// Look up existing path for dependencies
			std::scoped_lock lock(asset_mutex_);
			auto it = uid_to_path_.find(asset_to_save->get_uid());
			if (it != uid_to_path_.end()) {
				save_path = it->second;
			} else {
				// New asset dependency
				is_new = true;
				// Generate path relative to root asset's directory
				fs::path parent_dir = root_phys_path.parent_path();
				std::string ext = type_to_ext(asset_to_save->get_asset_type());
				save_path = parent_dir / (asset_to_save->get_uid().to_string() + ext);
				INFO(LogAsset, "Auto-generating path for new dependency {}: {}", asset_to_save->get_uid().to_string(), save_path.string());
			}
		}

		// Check if save is needed
		if (!is_new && !asset_to_save->is_dirty()) {
			INFO(LogAsset, "Skipping save for {} ({}): is_new={}, is_dirty={}", 
			     asset_to_save->get_uid().to_string(), 
			     asset_to_save->get_asset_type_name(),
			     is_new, 
			     asset_to_save->is_dirty());
			continue;
		}
		INFO(LogAsset, "Will save {} ({}) to {}", 
		     asset_to_save->get_uid().to_string(), 
		     asset_to_save->get_asset_type_name(),
		     save_path.string());

		// Call on_save_asset to sync internal state before saving
		asset_to_save->on_save_asset();

		auto task = [this, asset_to_save, save_path]() {
			this->perform_save_to_disk(asset_to_save, save_path);
			asset_to_save->clear_dirty();
		};

		if (pool && use_thread_pool) {
			futures.emplace_back(pool->enqueue(std::move(task)));
		} else {
			task();
		}
	}

	for (auto &f : futures) {
		f.wait();
	}
}

std::optional<fs::path> AssetManager::get_physical_path(std::string_view virtual_path) {
	// This function now assumes virtual_path is a generic string
	if (is_virtual_path(virtual_path)) {
		if (virtual_path.starts_with(virtual_game_path_.generic_string())) {
			fs::path p(virtual_path);
			return game_path_ / fs::relative(p, virtual_game_path_);
		}
		if (virtual_path.starts_with(virtual_engine_path_.generic_string())) {
			fs::path p(virtual_path);
			return engine_path_ / fs::relative(p, virtual_engine_path_);
		}
	}
	return std::nullopt;
}

UID AssetManager::get_uid_by_path(const std::string &path_str) {
	auto generic_path = fs::path(path_str).generic_string();
	{
		std::scoped_lock lock(asset_mutex_);
		if (auto it = path_to_uid_.find(generic_path); it != path_to_uid_.end()) {
			return it->second;
		}
	}
	// Try resolve physical
	if (auto phys = get_physical_path(generic_path)) {
		std::string phys_str = phys->generic_string();
		{
			std::scoped_lock lock(asset_mutex_);
			if (auto it = path_to_uid_.find(phys_str); it != path_to_uid_.end()) {
				// Register the virtual path if it was used but not yet mapped
				if (!path_to_uid_.contains(generic_path)) {
					path_to_uid_[generic_path] = it->second;
				}
				return it->second;
			}
		}
	}
	return UID::empty();
}

bool AssetManager::is_virtual_path(std::string_view path) const {
	return path.starts_with(virtual_engine_path_.generic_string()) || path.starts_with(virtual_game_path_.generic_string());
}

std::optional<fs::path> AssetManager::get_virtual_path(const fs::path &real_path) {
	auto try_map = [&](const fs::path &base, const fs::path &v_base) -> std::optional<fs::path> {
		auto rel = fs::relative(real_path, base);
		if (!rel.empty() && rel.generic_string().find("..") == std::string::npos) {
			return v_base / rel;
		}
		return std::nullopt;
	};

	if (auto p = try_map(engine_path_, virtual_engine_path_)) {
		return p;
	}
	if (auto p = try_map(game_path_, virtual_game_path_)) {
		return p;
	}
	return std::nullopt;
}

#pragma once // 1. 放在最顶端，这是最标准的写法，现代编译器支持极好

#include <vector>
#include <memory>
#include <type_traits>
#include "engine/main/engine_context.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/core/reflect/serialize.h"

// ==========================================
// Part 1: 类型萃取 & 核心逻辑
// ==========================================

template <typename T> struct is_vector_impl : std::false_type {};
template <typename T, typename A> struct is_vector_impl<std::vector<T, A>> : std::true_type {};
template <typename T> using is_vector = is_vector_impl<T>;

template <typename T>
struct UidStorageTraits {
    using type = std::conditional_t<is_vector<T>::value, std::vector<UID>, UID>;
};

struct AssetLogic {
    // --- Sync (Ptr -> UID) ---
    template <typename T>
    static void sync(const std::vector<std::shared_ptr<T>>& ptrs, std::vector<UID>& uids) {
        uids.clear();
        uids.reserve(ptrs.size());
        for (const auto& p : ptrs) uids.push_back(p ? p->get_uid() : UID::empty());
    }

    template <typename T>
    static void sync(const std::shared_ptr<T>& ptr, UID& uid) {
        uid = ptr ? ptr->get_uid() : UID::empty();
    }

    // --- Load (UID -> Ptr) ---
    template <typename T>
    static void load(std::vector<std::shared_ptr<T>>& ptrs, const std::vector<UID>& uids) {
        ptrs.clear();
        auto* mgr = EngineContext::asset();
        for (const auto& uid : uids) {
            auto asset = mgr->load_asset<T>(uid);
            if (asset) ptrs.push_back(asset);
        }
    }
    template <typename T>
    static void load(std::shared_ptr<T>& ptr, const UID& uid) {
        ptr = !uid.is_empty() ? EngineContext::asset()->load_asset<T>(uid) : nullptr;
    }

    template <typename T>
    static void collect(std::vector<std::shared_ptr<Asset>>& out, const std::vector<std::shared_ptr<T>>& ptrs) {
        for (const auto& p : ptrs) {
            if (p) out.push_back(p); 
        }
    }

    // Case 2: Single Pointer
    template <typename T>
    static void collect(std::vector<std::shared_ptr<Asset>>& out, const std::shared_ptr<T>& ptr) {
        if (ptr) out.push_back(ptr);
    }
};

// ==========================================
// Part 2: 宏定义 (基于 FOR_EACH)
// ==========================================

// 1. 声明
#define DECL_ENTRY(Type, Name) \
    Type Name; \
    typename UidStorageTraits<Type>::type Name##_uid;
// 2. Load
#define LOAD_ENTRY(Type, Name) \
    AssetLogic::load(Name, Name##_uid);
// 3. Save
#define SYNC_ENTRY(Type, Name) \
    AssetLogic::sync(Name, Name##_uid);
// 4. Serialize
#define SERIALIZE_ENTRY(Type, Name) \
    ar(cereal::make_nvp(#Name, Name##_uid)); 
// 5. Easy Get Deps
#define COLLECT_ENTRY(Type, Name) \
    AssetLogic::collect(__deps, Name);

// --- FOR_EACH 基础设施 (保持不变) ---
#define EXPAND(x) x
#define FE_1(ACT, X)      ACT X
#define FE_2(ACT, X, ...) ACT X EXPAND(FE_1(ACT, __VA_ARGS__))
#define FE_3(ACT, X, ...) ACT X EXPAND(FE_2(ACT, __VA_ARGS__))
#define FE_4(ACT, X, ...) ACT X EXPAND(FE_3(ACT, __VA_ARGS__))
#define FE_5(ACT, X, ...) ACT X EXPAND(FE_4(ACT, __VA_ARGS__))
#define FE_6(ACT, X, ...) ACT X EXPAND(FE_5(ACT, __VA_ARGS__))
#define FE_7(ACT, X, ...) ACT X EXPAND(FE_6(ACT, __VA_ARGS__))
#define FE_8(ACT, X, ...) ACT X EXPAND(FE_7(ACT, __VA_ARGS__))

#define GET_MACRO(_1, _2, _3, _4, _5, _6, _7, _8, NAME, ...) NAME
#define FOR_EACH(ACTION, ...) \
    EXPAND(GET_MACRO(__VA_ARGS__, FE_8, FE_7, FE_6, FE_5, FE_4, FE_3, FE_2, FE_1)(ACTION, __VA_ARGS__))

#define ASSET_DEPS(...) \
    FOR_EACH(DECL_ENTRY, __VA_ARGS__) \
    \
    virtual void load_asset_deps() override { \
        FOR_EACH(LOAD_ENTRY, __VA_ARGS__) \
    } \
    \
    virtual void save_asset_deps() override { \
        FOR_EACH(SYNC_ENTRY, __VA_ARGS__) \
    } \
    \
    template <class Archive> \
    void serialize_deps(Archive & ar) { \
        FOR_EACH(SERIALIZE_ENTRY, __VA_ARGS__) \
    } \
    \
    std::vector<std::shared_ptr<Asset>> get_deps() const override { \
        std::vector<std::shared_ptr<Asset>> __deps; \
        FOR_EACH(COLLECT_ENTRY, __VA_ARGS__) \
        return __deps; \
    }
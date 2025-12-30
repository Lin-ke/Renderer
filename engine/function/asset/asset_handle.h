#ifndef ASSET_HANDLE_H
#define ASSET_HANDLE_H
#include "asset.h"
#include "asset_manager.h"
#include "engine/main/engine_context.h"

template<typename T>
class AssetHandle {
public:
    AssetHandle() = default;
    AssetHandle(std::shared_ptr<T> ptr) : asset_ptr_(std::move(ptr)) {
        if (asset_ptr_) uid_ = asset_ptr_->get_uid();
    }

    T* get() const { 
        return asset_ptr_.get(); 
    }
    
    bool is_loaded() const { return asset_ptr_ != nullptr; }

    T* operator->() const { return get(); }
    T& operator*() const { assert(asset_ptr_); return *get(); } 
    UID get_uid() const { return uid_; }
private:
    std::shared_ptr<T> asset_ptr_ = nullptr;
    UID uid_ = UID::empty();
    friend class cereal::access;
public:
    template <class Archive>
    std::string save_minimal(const Archive&) const {
        UID save_uid = asset_ptr_ ? asset_ptr_->get_uid() : uid_;
        return save_uid.to_string(); 
    }

    template <class Archive>
    void load_minimal(const Archive&, const std::string& value) {
        uid_ = UID(value); 
        asset_ptr_ = EngineContext::asset()->template get_or_load_asset<T>(uid_); 
    }
};

#endif
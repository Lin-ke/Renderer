#include "asset.h"
#include "asset_manager.h"
using AssetRef = std::shared_ptr<Asset>;

template<IsAsset T>
class AssetHandle {
public:
    AssetHandle() = default;
    AssetHandle(std::shared_ptr<T> ptr) : asset_ptr_(std::move(ptr)) {}

    // 像指针一样访问
    T* operator->() const { return asset_ptr_.get(); }
    T& operator*() const { return *asset_ptr_; }
    operator bool() const { return asset_ptr_ != nullptr; }
    
    std::shared_ptr<T> ptr() const { return asset_ptr_; }
    UID get_uid() const { return asset_ptr_ ? asset_ptr_->get_uid() : UID::empty(); }

private:
    std::shared_ptr<T> asset_ptr_ = nullptr;
    friend class cereal::access;

    template <class Archive>
    void save(Archive& ar) const {
        UID uid = asset_ptr_ ? asset_ptr_->get_uid() : UID::empty();
        ar(cereal::make_nvp("uid", uid));
    }

	template <class Archive>
    void load(Archive& ar) {
        UID uid;
        ar(cereal::make_nvp("uid", uid));
        if (uid.is_empty()) {
            asset_ptr_ = nullptr;
        } else {
            asset_ptr_ = EngineContext::asset()->get_or_load_asset<T>(uid);
        }
    }
	
};
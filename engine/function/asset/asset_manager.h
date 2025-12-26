#include "uid.h"
#include <memory>
class AssetManager{
private:
    AssetManager() = default;
public:
    static AssetManager& get() {
        static AssetManager instance;
        return instance;
    }
    void init();
    



    template<typename T>
    std::shared_ptr<T> get_or_load_asset(const UID& uid){

    }
};
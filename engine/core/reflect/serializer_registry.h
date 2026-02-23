#pragma once
#include <typeindex>
#include <unordered_map>
#include <functional>
#include <any>
#include <string>

#include <cereal/cereal.hpp>
#include <cereal/archives/json.hpp>

class SerializerRegistry {
public:
    using JsonSerializerFn = std::function<void(const std::any&, cereal::JSONOutputArchive&, const std::string& name)>;
    using JsonDeserializerFn = std::function<void(std::any&, cereal::JSONInputArchive&, const std::string& name)>;

    static SerializerRegistry& get();

    template <typename T>
    void register_type() {
        auto type_idx = std::type_index(typeid(T));
        
        json_serializers_[type_idx] = [](const std::any& val, cereal::JSONOutputArchive& ar, const std::string& name) {
            // Check if any has value
            if (val.has_value()) {
                const T& typed_val = std::any_cast<const T&>(val);
                ar(cereal::make_nvp(name, typed_val));
            }
        };

        json_deserializers_[type_idx] = [](std::any& val, cereal::JSONInputArchive& ar, const std::string& name) {
            T typed_val;
            ar(cereal::make_nvp(name, typed_val));
            val = typed_val;
        };
    }

    const JsonSerializerFn* get_json_serializer(std::type_index type_idx) const {
        auto it = json_serializers_.find(type_idx);
        return it != json_serializers_.end() ? &it->second : nullptr;
    }

    const JsonDeserializerFn* get_json_deserializer(std::type_index type_idx) const {
        auto it = json_deserializers_.find(type_idx);
        return it != json_deserializers_.end() ? &it->second : nullptr;
    }

private:
    SerializerRegistry();

    std::unordered_map<std::type_index, JsonSerializerFn> json_serializers_;
    std::unordered_map<std::type_index, JsonDeserializerFn> json_deserializers_;
    
    // Helper to register built-in types
    void register_builtins();
};

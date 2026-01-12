#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <memory>
#include <sstream>
#include <any>
#include <cereal/archives/json.hpp>
#include "engine/core/log/Log.h"
#include "engine/core/math/math.h"

// 序列化策略：封装具体的序列化格式（当前为 JSON）
struct JsonSerializer {
    using OutputArchive = cereal::JSONOutputArchive;
    using InputArchive = cereal::JSONInputArchive;

    template<typename T>
    static std::string serialize(const T& value) {
        std::stringstream ss;
        {
            OutputArchive ar(ss);
            ar(cereal::make_nvp("v", value));
        }
        return ss.str();
    }

    template<typename T>
    static void deserialize(const std::string& str, T& value) {
        // 1. 尝试直接按照 {"v": value} 解析 (标准格式)
        try {
            std::stringstream ss(str);
            InputArchive ar(ss);
            ar(cereal::make_nvp("v", value));
            return;
        } catch (...) {}

        // 2. 回退：假设输入是裸值，手动包裹一层 {"v": input} 再次尝试
        {
            std::string wrapped = "{\"v\": " + str + "}";
            std::stringstream ss(wrapped);
            InputArchive ar(ss);
            ar(cereal::make_nvp("v", value));
        }
    }
};

// 定义当前反射使用的序列化方案
using ReflectScheme = JsonSerializer;

// 属性元数据
class Component;
struct PropertyInfo {
    std::string name;
    std::string type_name;
    std::string default_value_str; // 默认值的序列化字符串表示 (不再硬编码为 JSON)
    
    // 类型擦除的访问器
    std::function<std::string(const Component*)> getter;
    std::function<bool(Component*, const std::string&)> setter;

    // 高效访问器 (使用 std::any 避免序列化开销)
    std::function<std::any(const Component*)> getter_any;
    std::function<void(Component*, const std::any&)> setter_any;
};

// 类的元数据
struct ClassInfo {
    std::string class_name;
    std::string parent_class_name;
    std::vector<PropertyInfo> properties;
    std::unordered_map<std::string, size_t> property_map; // Name -> Index in vector
};

class ClassDB {
public:
    static ClassDB& get() {
        static ClassDB instance;
        return instance;
    }

    // 注册类
    void register_class(const std::string& class_name, const std::string& parent_class_name) {
        auto& info = classes_[class_name];
        info.class_name = class_name;
        if (!parent_class_name.empty()) {
            info.parent_class_name = parent_class_name;
        } else if (info.parent_class_name.empty()) {
            info.parent_class_name = "Component"; // 默认继承自 Component
        }
    }

    // 注册属性
    template <typename ClassType, typename PropertyType>
    void register_property(const std::string& class_name, const std::string& property_name, 
                           const PropertyType& default_value, 
                           PropertyType ClassType::* member_ptr) {
        
        // 确保类已注册
        if (classes_.find(class_name) == classes_.end()) {
            register_class(class_name, "Component");
        }
        auto& info = classes_[class_name];
        
        PropertyInfo prop;
        prop.name = property_name;
        prop.type_name = typeid(PropertyType).name(); // 简化的类型名，仅作参考
        
        // 序列化默认值
        prop.default_value_str = ReflectScheme::serialize(default_value);

        // 生成 Getter (Serialized String)
        prop.getter = [member_ptr](const Component* obj) -> std::string {
            const auto* casted = dynamic_cast<const ClassType*>(obj);
            if (!casted) return "";
            return ReflectScheme::serialize(casted->*member_ptr);
        };

        // 生成 Setter (Serialized String)
        prop.setter = [member_ptr](Component* obj, const std::string& str_val) -> bool {
            auto* casted = dynamic_cast<ClassType*>(obj);
            if (!casted) return false;
            try {
                ReflectScheme::deserialize(str_val, casted->*member_ptr);
                return true;
            } catch (...) {
                return false;
            }
        };

        // 生成 Getter (std::any) - 高效路径
        prop.getter_any = [member_ptr](const Component* obj) -> std::any {
            const auto* casted = dynamic_cast<const ClassType*>(obj);
            if (!casted) return {};
            return casted->*member_ptr;
        };

        // 生成 Setter (std::any) - 高效路径
        prop.setter_any = [member_ptr](Component* obj, const std::any& val) {
            auto* casted = dynamic_cast<ClassType*>(obj);
            if (!casted) return;
            // 只有类型完全匹配时才设置
            if (val.type() == typeid(PropertyType)) {
                casted->*member_ptr = std::any_cast<PropertyType>(val);
            }
        };

        info.property_map[property_name] = info.properties.size();
        info.properties.push_back(prop);
    }

    const ClassInfo* get_class_info(const std::string& class_name) const {
        auto it = classes_.find(class_name);
        return it != classes_.end() ? &it->second : nullptr;
    }

    // 获取包括父类在内的所有属性
    std::vector<const PropertyInfo*> get_all_properties(const std::string& class_name) const {
        std::vector<const PropertyInfo*> result;
        collect_properties_recursive(class_name, result);
        return result;
    }

private:
    std::unordered_map<std::string, ClassInfo> classes_;

    void collect_properties_recursive(const std::string& class_name, std::vector<const PropertyInfo*>& out) const {
        auto it = classes_.find(class_name);
        if (it == classes_.end()) return;

        // 先父类
        if (!it->second.parent_class_name.empty() && it->second.parent_class_name != "Component") {
            collect_properties_recursive(it->second.parent_class_name, out);
        }
        // 后子类
        for (const auto& prop : it->second.properties) {
            out.push_back(&prop);
        }
    }
};

// --- 新的链式注册 API ---

// 辅助 Trait 用于获取类型的“安全”默认值
template <typename T>
struct DefaultValueTrait {
    static T get(const T& val) { return val; }
};

// 针对 Eigen 类型的特化，确保它们不包含垃圾值
template <> struct DefaultValueTrait<Vec2> { static Vec2 get(const Vec2& v) { return v.allFinite() ? v : Vec2::Zero(); } };
template <> struct DefaultValueTrait<Vec3> { static Vec3 get(const Vec3& v) { return v.allFinite() ? v : Vec3::Zero(); } };
template <> struct DefaultValueTrait<Vec4> { static Vec4 get(const Vec4& v) { return v.allFinite() ? v : Vec4::Zero(); } };
template <> struct DefaultValueTrait<Quaternion> { static Quaternion get(const Quaternion& q) { return std::abs(q.norm() - 1.0f) < 0.001f ? q : Quaternion::Identity(); } };

template <typename T>
class ClassDefinitionHelper {
public:
    ClassDefinitionHelper(const std::string& name) : class_name_(name) {
        ClassDB::get().register_class(class_name_, "Component");
    }

    template <typename Base>
    ClassDefinitionHelper& base(const std::string& parent_name) {
        ClassDB::get().register_class(class_name_, parent_name);
        return *this;
    }

    template <typename PropertyType>
    ClassDefinitionHelper& member(const std::string& property_name, PropertyType T::* member_ptr) {
        // 使用值初始化 T{} 确保 POD 成员（如 int）被清零
        T default_obj{};
        // 获取该成员的值。如果是 Eigen 类型且未在类中初始化，Trait 会尝试提供一个安全的默认值
        PropertyType def_val = DefaultValueTrait<PropertyType>::get(default_obj.*member_ptr);
        
        ClassDB::get().register_property<T, PropertyType>(class_name_, property_name, def_val, member_ptr);
        return *this;
    }

    // 用于静态初始化的辅助转换
    operator bool() const { return true; }

private:
    std::string class_name_;
};

class Registry {
public:
    template <typename T>
    static ClassDefinitionHelper<T> add(const std::string& name) {
        return ClassDefinitionHelper<T>(name);
    }
};

#define REGISTER_CLASS_IMPL(Class) \
    static bool Class##_registered = [](){ Class::register_class(); return true; }();

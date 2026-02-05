#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <functional>
#include <sstream>
#include <any>
#include <typeindex> 

#include "engine/core/log/Log.h"
#include "engine/core/math/math.h"

#include <cereal/archives/json.hpp>
#include <cereal/cereal.hpp>


struct Serializer {
    template<typename T, typename OutputArchive>
    static std::string serialize(const T& value) {
        std::stringstream ss;
        {
            OutputArchive ar(ss, cereal::JSONOutputArchive::Options::NoIndent());
            ar(cereal::make_nvp("v", value));
        }
        std::string s = ss.str();
        size_t colon = s.find(':');
        if (colon != std::string::npos && s.size() >= colon + 2) {
            std::string result = s.substr(colon + 1, s.size() - colon - 2);
            // Trim
            size_t first = result.find_first_not_of(" \t\n\r");
            if (first == std::string::npos) return "";
            size_t last = result.find_last_not_of(" \t\n\r");
            return result.substr(first, (last - first + 1));
        }
        return s;
    }

    template<typename T, typename InputArchive>
    static void deserialize(const std::string& str, T& value) {
        std::stringstream ss(str);
        InputArchive ar(ss);
        // Deserialize value directly
        ar(value);
    }
    
    template<typename T, typename InputArchive>
    static void deserialize_wrapped(const std::string& str, T& value, const char* name) {
        std::stringstream ss(str);
        InputArchive ar(ss);
        ar(cereal::make_nvp(name, value));
    }
};

struct JsonSerializer {
    using OutputArchive = cereal::JSONOutputArchive;
    using InputArchive = cereal::JSONInputArchive;

    template<typename T>
    static std::string serialize(const T& value) {
        return Serializer::serialize<T, OutputArchive>(value);
    }

    template<typename T>
    static void deserialize(const std::string& str, T& value) {
        if (str.empty()) return;
        size_t first = str.find_first_not_of(" \t\n\r");
        
        bool looks_like_json = (first != std::string::npos && (str[first] == '{' || str[first] == '['));
        
        if (!looks_like_json) {
             // Case 1: Raw primitive "500"
             // Wrap it and read as NVP "v" so Cereal has a valid root object context // 为了简化，加个"v"方便parse
             std::string wrapped = "{\"v\": " + str + "}";
             try {
                Serializer::deserialize_wrapped<T, InputArchive>(wrapped, value, "v");
             } catch (...) {
             }
             return;
        }

        // Case 2: Object "{...}" or Array "[...]"
        // Could be a Class object OR a legacy wrapped primitive {"v": 500}
        try {
            // Try direct read first (Preferred for Class objects / new format)
            Serializer::deserialize<T, InputArchive>(str, value);
        } catch (...) {
            // If direct read fails, maybe it's wrapped in "v"? (Legacy format)
            try {
                Serializer::deserialize_wrapped<T, InputArchive>(str, value, "v");
            } catch (...) {
                // Try wrapping the whole thing in "v" (handles raw {"x":0} or [1,2,3])
                std::string wrapped = "{\"v\": " + str + "}";
                try {
                    Serializer::deserialize_wrapped<T, InputArchive>(wrapped, value, "v");
                } catch (...) {
                }
            }
        }
    }
};

using ReflectScheme = JsonSerializer;
class Component;

// --- 元数据结构 ---
struct PropertyInfo {
    std::string name;
    std::string type_name;
    std::type_index type_index; // Identifies the C++ type
    std::string default_value_str;
    
    std::function<std::string(const Component*)> getter;
    std::function<bool(Component*, std::string_view)> setter; // 在保存中使用（通过json）

    std::function<std::any(const Component*)> getter_any; // 其他情况
    std::function<void(Component*, const std::any&)> setter_any;
    
    PropertyInfo() : type_index(typeid(void)) {}
};

struct ClassInfo {
    std::string class_name;
    std::string parent_class_name;
    std::vector<PropertyInfo> properties;
    std::unordered_map<std::string, size_t> property_map; 
    std::function<std::unique_ptr<Component>()> creator;
};

// --- ClassDB 声明 ---
class ClassDB {
public:
    // 单例获取 
    static ClassDB& get();

    // 注册类 
    void register_class(std::string_view class_name, std::string_view parent_class_name, std::function<std::unique_ptr<Component>()> creator = nullptr);

    // 创建组件实例
    std::unique_ptr<Component> create_component(std::string_view class_name) const;

    // 获取类信息 
    const ClassInfo* get_class_info(std::string_view class_name) const;

    // 获取所有属性 
    std::vector<const PropertyInfo*> get_all_properties(std::string_view class_name) const;

    // 通用继承链遍历器 (Child -> Parent)
    // Visitor: function<bool(const ClassInfo*)> 返回 true 则停止遍历
    template <typename Visitor>
    void visit_class_chain(std::string_view start_class_name, Visitor visitor) const {
        std::string current_name(start_class_name);
        
        while (!current_name.empty()) {
            const auto* info = get_class_info(current_name);
            if (!info) break;

            // 执行访问器，如果返回 true 则终止遍历
            if (visitor(info)) return;

            // 继承链跳转逻辑
            if (info->parent_class_name.empty() || info->parent_class_name == "Component") {
                 // 如果父类是 Component，且当前还不是 Component，则跳转到 Component
                 if (info->parent_class_name == "Component" && current_name != "Component") {
                     current_name = "Component";
                     continue;
                 }
                 break; // 链条结束
            }
            current_name = info->parent_class_name;
        }
    }

    // 注册属性
    template <typename ClassType, typename PropertyType>
    void register_property(std::string_view class_name, std::string_view property_name, 
                           const PropertyType& default_value, 
                           PropertyType ClassType::* member_ptr) {
        
        std::string cn(class_name);
        if (classes_.find(cn) == classes_.end()) {
            return;
        }
        auto& info = classes_[cn];
        
        PropertyInfo prop;
        prop.name = std::string(property_name);
        prop.type_name = typeid(PropertyType).name(); 
        prop.type_index = std::type_index(typeid(PropertyType));
        
        prop.default_value_str = ReflectScheme::serialize(default_value);

        // Getter String
        prop.getter = [member_ptr](const Component* obj) -> std::string {
            const auto* casted = dynamic_cast<const ClassType*>(obj);
            if (!casted) return "";
            return ReflectScheme::serialize(casted->*member_ptr);
        };

        // Setter String
        prop.setter = [member_ptr](Component* obj, std::string_view str_val) -> bool {
            auto* casted = dynamic_cast<ClassType*>(obj);
            if (!casted) return false;
            try {
                ReflectScheme::deserialize(std::string(str_val), casted->*member_ptr);
                return true;
            } catch (...) {
                return false;
            }
        };

        // Getter Any
        prop.getter_any = [member_ptr](const Component* obj) -> std::any {
            const auto* casted = dynamic_cast<const ClassType*>(obj);
            if (!casted) return {};
            return casted->*member_ptr;
        };

        // Setter Any
        prop.setter_any = [member_ptr](Component* obj, const std::any& val) {
            auto* casted = dynamic_cast<ClassType*>(obj);
            if (!casted) return;
            if (val.type() == typeid(PropertyType)) {
                casted->*member_ptr = std::any_cast<PropertyType>(val);
            }
        };

        info.property_map[prop.name] = info.properties.size();
        info.properties.push_back(prop);
    }

private:
    std::unordered_map<std::string, ClassInfo> classes_;

    // 递归查找的具体实现 (移至 cpp)
    void collect_properties_recursive(std::string_view class_name, std::vector<const PropertyInfo*>& out) const;

public:
    static std::any get(Component* obj, std::string_view property_name);
};

// --- 辅助 Traits (模板，保留在头文件) ---
template <typename T>
struct DefaultValueTrait {
    static T get(const T& val) { return val; }
};

// 特化版本 (需要保留在头文件以被看见)
template <> struct DefaultValueTrait<Vec2> { static Vec2 get(const Vec2& v) { return v.allFinite() ? v : Vec2::Zero(); } };
template <> struct DefaultValueTrait<Vec3> { static Vec3 get(const Vec3& v) { return v.allFinite() ? v : Vec3::Zero(); } };
template <> struct DefaultValueTrait<Vec4> { static Vec4 get(const Vec4& v) { return v.allFinite() ? v : Vec4::Zero(); } };
template <> struct DefaultValueTrait<Quaternion> { static Quaternion get(const Quaternion& q) { return std::abs(q.norm() - 1.0f) < 0.001f ? q : Quaternion::Identity(); } };

// --- 注册辅助类 (模板，保留在头文件) ---
template <typename T>
class ClassDefinitionHelper {
public:
    ClassDefinitionHelper(const std::string& name) : class_name_(name) {
        ClassDB::get().register_class(class_name_, "Component", []() -> std::unique_ptr<Component> {
            return std::make_unique<T>();
        });
    }

    template <typename Base>
    ClassDefinitionHelper& base(const std::string& parent_name) {
        // Re-register with parent name, keeping the creator
        ClassDB::get().register_class(class_name_, parent_name, []() -> std::unique_ptr<Component> {
            return std::make_unique<T>();
        });
        return *this;
    }

    template <typename PropertyType>
    ClassDefinitionHelper& member(const std::string& property_name, PropertyType T::* member_ptr) {
        T default_obj{};
        PropertyType def_val = DefaultValueTrait<PropertyType>::get(default_obj.*member_ptr);
        
        ClassDB::get().register_property<T, PropertyType>(class_name_, property_name, def_val, member_ptr);
        return *this;
    }

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

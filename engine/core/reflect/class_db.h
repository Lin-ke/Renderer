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

DECLARE_LOG_TAG(LogClassDB);

/**
 * @brief Helper struct for generic serialization using Cereal.
 */
struct Serializer {
    /**
     * @brief Serializes a value to a string using the specified OutputArchive.
     * @tparam T The type of the value to serialize.
     * @tparam OutputArchive The Cereal output archive type (e.g., JSONOutputArchive).
     * @param value The value to serialize.
     * @return The serialized string.
     */
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

    /**
     * @brief Deserializes a string to a value using the specified InputArchive.
     * @tparam T The type of the value to deserialize.
     * @tparam InputArchive The Cereal input archive type.
     * @param str The string to deserialize.
     * @param value [out] The value to populate.
     */
    template<typename T, typename InputArchive>
    static void deserialize(const std::string& str, T& value) {
        std::stringstream ss(str);
        InputArchive ar(ss);
        // Deserialize value directly
        ar(value);
    }
    
    /**
     * @brief Deserializes a string wrapping the value in a named NVP.
     * Useful for legacy formats or specific Cereal archive requirements.
     */
    template<typename T, typename InputArchive>
    static void deserialize_wrapped(const std::string& str, T& value, const char* name) {
        std::stringstream ss(str);
        InputArchive ar(ss);
        ar(cereal::make_nvp(name, value));
    }
};

/**
 * @brief Specific serializer implementation for JSON format.
 */
struct JsonSerializer {
    using OutputArchive = cereal::JSONOutputArchive;
    using InputArchive = cereal::JSONInputArchive;

    template<typename T>
    static std::string serialize(const T& value) {
        return Serializer::serialize<T, OutputArchive>(value);
    }

    template<typename T>
    static void deserialize(const std::string& str, T& value) {
        if (str.empty()) {
            return;
        }
        size_t first = str.find_first_not_of(" \t\n\r");
        
        bool looks_like_json = (first != std::string::npos && (str[first] == '{' || str[first] == '['));
        
        if (!looks_like_json) {
             // Case 1: Raw primitive "500"
             // Wrap it and read as NVP "v" so Cereal has a valid root object context
             std::string wrapped = "{\"v\": " + str + "}";
             Serializer::deserialize_wrapped<T, InputArchive>(wrapped, value, "v");
             return;
        }

        // Case 2: Object "{...}" or Array "[...]"
        // Could be a Class object OR a legacy wrapped primitive {"v": 500}
        // Try direct read first (Preferred for Class objects / new format)
        Serializer::deserialize<T, InputArchive>(str, value);
    }
};

using ReflectScheme = JsonSerializer;
class Component;

/**
 * @brief Metadata describing a property of a reflected class.
 */
struct PropertyInfo {
    std::string name;               ///< Name of the property.
    std::string type_name;          ///< Type name string (implementation defined).
    std::type_index type_index;     ///< Unique type identifier.
    std::string default_value_str;  ///< serialized default value.
    
    // Serialization accessors (String-based)
    std::function<std::string(const Component*)> getter;
    std::function<bool(Component*, std::string_view)> setter; 

    // Runtime accessors (Any-based)
    std::function<std::any(const Component*)> getter_any; 
    std::function<void(Component*, const std::any&)> setter_any;
    
    PropertyInfo() : type_index(typeid(void)) {}
};

/**
 * @brief Metadata describing a reflected class.
 */
struct ClassInfo {
    std::string class_name;
    std::string parent_class_name;
    std::vector<PropertyInfo> properties;
    std::unordered_map<std::string, size_t> property_map; 
    std::function<std::unique_ptr<Component>()> creator; ///< Factory function to create instances.
};

/**
 * @brief The central database for the Reflection System.
 * 
 * Manages class registration, property introspection, and object creation.
 * It allows querying class metadata by name at runtime.
 */
class ClassDB {
public:
    /**
     * @brief Gets the singleton instance of ClassDB.
     */
    static ClassDB& get();

    /**
     * @brief Registers a new class with the reflection system.
     * @param class_name The name of the class.
     * @param parent_class_name The name of the parent class.
     * @param creator A factory function to create instances of this class.
     */
    void register_class(std::string_view class_name, std::string_view parent_class_name, std::function<std::unique_ptr<Component>()> creator = nullptr);

    /**
     * @brief Creates an instance of a registered component class by name.
     * @param class_name The name of the class to instantiate.
     * @return A unique_ptr to the new component, or nullptr if not found.
     */
    std::unique_ptr<Component> create_component(std::string_view class_name) const;

    /**
     * @brief Retrieves metadata for a specific class.
     */
    const ClassInfo* get_class_info(std::string_view class_name) const;

    /**
     * @brief Retrieves all properties for a class, including inherited ones.
     */
    std::vector<const PropertyInfo*> get_all_properties(std::string_view class_name) const;

    /**
     * @brief Traverses the inheritance chain from child to parent.
     * @tparam Visitor A callable object with signature `bool(const ClassInfo*)`.
     * @param start_class_name The class to start traversing from.
     * @param visitor The callback to invoke for each class in the chain. Return true to stop traversal.
     */
    template <typename Visitor>
    void visit_class_chain(std::string_view start_class_name, Visitor visitor) const {
        std::string current_name(start_class_name);
        
        while (!current_name.empty()) {
            const auto* info = get_class_info(current_name);
            if (!info) break;

            // Execute visitor, stop if it returns true
            if (visitor(info)) return;

            // Inheritance chain traversal logic
            if (info->parent_class_name.empty() || info->parent_class_name == "Component") {
                 // Special handling to ensure "Component" is visited last if it's the base
                 if (info->parent_class_name == "Component" && current_name != "Component") {
                     current_name = "Component";
                     continue;
                 }
                 break; // End of chain
            }
            current_name = info->parent_class_name;
        }
    }

    /**
     * @brief Registers a member property for a class.
     * 
     * @tparam ClassType The class type.
     * @tparam PropertyType The type of the property.
     * @param class_name The name of the class.
     * @param property_name The name of the property.
     * @param default_value The default value for this property.
     * @param member_ptr Pointer to the member variable.
     */
    template <typename ClassType, typename PropertyType>
    void register_property(std::string_view class_name, std::string_view property_name, 
                           const PropertyType& default_value, 
                           PropertyType ClassType::* member_ptr) {
        
        std::string cn(class_name);
        if (classes_.find(cn) == classes_.end()) {
            FATAL(LogClassDB, "Cannot register property '{}' for unregistered class '{}'", std::string(property_name), cn);
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

    // Recursive helper to collect properties from the hierarchy
    void collect_properties_recursive(std::string_view class_name, std::vector<const PropertyInfo*>& out) const;

public:
    /**
     * @brief Generic property getter using std::any.
     */
    static std::any get(Component* obj, std::string_view property_name);
};

// --- Helper Traits (Templates, must remain in header) ---

/**
 * @brief Trait to handle default value normalization (e.g., NaNs in vectors).
 */
template <typename T>
struct DefaultValueTrait {
    static T get(const T& val) { return val; }
};

// Specializations
template <> struct DefaultValueTrait<Vec2> { static Vec2 get(const Vec2& v) { return v.allFinite() ? v : Vec2::Zero(); } };
template <> struct DefaultValueTrait<Vec3> { static Vec3 get(const Vec3& v) { return v.allFinite() ? v : Vec3::Zero(); } };
template <> struct DefaultValueTrait<Vec4> { static Vec4 get(const Vec4& v) { return v.allFinite() ? v : Vec4::Zero(); } };
template <> struct DefaultValueTrait<Quaternion> { static Quaternion get(const Quaternion& q) { return std::abs(q.norm() - 1.0f) < 0.001f ? q : Quaternion::Identity(); } };

// --- Registration Helper (Template, must remain in header) ---

/**
 * @brief Fluent helper class for registering classes and their members.
 * 
 * @tparam T The class type being registered.
 */
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

/**
 * @brief Macro to register a class. Should be called inside the cpp file of the class.
 * Uses a static boolean to ensure registration happens at program startup (dynamic initialization).
 */
#define REGISTER_CLASS_IMPL(Class) \
    static bool Class##_registered = [](){ Class::register_class(); return true; }();

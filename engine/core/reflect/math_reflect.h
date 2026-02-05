#pragma once

#include "engine/core/math/math.h"
#include "engine/core/math/extent.h"
#include "engine/core/math/transform.h"
#include "serialize.h"
#include <sstream>
#include <string>
#include <iomanip>

// 辅助函数：将流式数据转为字符串或从字符串解析
namespace Utils {
    template<typename T>
    std::string to_compact_string(const T& v, int components) {
        std::stringstream ss;
        // 保证足够的精度
        ss << std::fixed << std::setprecision(6);
        for (int i = 0; i < components; ++i) {
            ss << v[i] << (i < components - 1 ? " " : "");
        }
        return ss.str();
    }

    template<typename T>
    void from_compact_string(const std::string& s, T& v, int components) {
        std::stringstream ss(s);
        for (int i = 0; i < components; ++i) {
            ss >> v[i];
        }
    }
}

namespace cereal {

    // =============================================================
    // Extent (2D/3D) - 紧凑化为 "width height [depth]"
    // =============================================================
    template <class Archive>
    std::string save_minimal(const Archive&, const Extent2D& e) {
        std::stringstream ss;
        ss << e.width << " " << e.height;
        return ss.str();
    }
    template <class Archive>
    void load_minimal(const Archive&, Extent2D& e, const std::string& s) {
        std::stringstream ss(s);
        ss >> e.width >> e.height;
    }

    template <class Archive>
    std::string save_minimal(const Archive&, const Extent3D& e) {
        std::stringstream ss;
        ss << e.width << " " << e.height << " " << e.depth;
        return ss.str();
    }
    template <class Archive>
    void load_minimal(const Archive&, Extent3D& e, const std::string& s) {
        std::stringstream ss(s);
        ss >> e.width >> e.height >> e.depth;
    }

    #define IMPLEMENT_COMPACT_SERIALIZE(TYPE, COMPONENT_COUNT) \
    template <class Archive> \
    std::string save_minimal(const Archive&, const TYPE& v) { \
        return Utils::to_compact_string(v, COMPONENT_COUNT); \
    } \
    template <class Archive> \
    void load_minimal(const Archive&, TYPE& v, const std::string& s) { \
        Utils::from_compact_string(s, v, COMPONENT_COUNT); \
    }

    IMPLEMENT_COMPACT_SERIALIZE(Vec2, 2)
    IMPLEMENT_COMPACT_SERIALIZE(Vec3, 3)
    IMPLEMENT_COMPACT_SERIALIZE(Vec4, 4)
    
    IMPLEMENT_COMPACT_SERIALIZE(IVec2, 2)
    IMPLEMENT_COMPACT_SERIALIZE(IVec3, 3)
    IMPLEMENT_COMPACT_SERIALIZE(IVec4, 4)
    
    IMPLEMENT_COMPACT_SERIALIZE(UVec2, 2)
    IMPLEMENT_COMPACT_SERIALIZE(UVec3, 3)
    IMPLEMENT_COMPACT_SERIALIZE(UVec4, 4)

    // Quaternion 包含 x,y,z,w (Eigen内部存储顺序可能不同，这里统一用 x y z w 接口)
    template <class Archive>
    std::string save_minimal(const Archive&, const Quaternion& q) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(6);
        ss << q.x() << " " << q.y() << " " << q.z() << " " << q.w();
        return ss.str();
    }
    template <class Archive>
    void load_minimal(const Archive&, Quaternion& q, const std::string& s) {
        std::stringstream ss(s);
        float x, y, z, w;
        ss >> x >> y >> z >> w;
        q = Quaternion(w, x, y, z); // 注意 Eigen 构造顺序通常是 w, x, y, z
    }

    // =============================================================
    // Matrices - 每一列序列化为一个字符串
    // =============================================================
    // 矩阵保持结构，但内部的 Vector 紧凑化
    template <class Archive>
    void serialize(Archive &ar, Mat2 &m) {
        Vec2 c0 = m.col(0); Vec2 c1 = m.col(1);
        ar(cereal::make_nvp("col0", c0), cereal::make_nvp("col1", c1));
        if constexpr (Archive::is_loading::value) {
            m.col(0) = c0; m.col(1) = c1;
        }
    }

    template <class Archive>
    void serialize(Archive &ar, Mat3 &m) {
        Vec3 c0 = m.col(0); Vec3 c1 = m.col(1); Vec3 c2 = m.col(2);
        ar(cereal::make_nvp("col0", c0), cereal::make_nvp("col1", c1), cereal::make_nvp("col2", c2));
        if constexpr (Archive::is_loading::value) {
            m.col(0) = c0; m.col(1) = c1; m.col(2) = c2;
        }
    }

    template <class Archive>
    void serialize(Archive &ar, Mat4 &m) {
        Vec4 c0 = m.col(0); Vec4 c1 = m.col(1); Vec4 c2 = m.col(2); Vec4 c3 = m.col(3);
        ar(cereal::make_nvp("col0", c0), cereal::make_nvp("col1", c1), 
           cereal::make_nvp("col2", c2), cereal::make_nvp("col3", c3));
        if constexpr (Archive::is_loading::value) {
            m.col(0) = c0; m.col(1) = c1; m.col(2) = c2; m.col(3) = c3;
        }
    }

    // =============================================================
    // Transform - 拆分 Save/Load 以正确处理 Getters/Setters
    // =============================================================
    template<class Archive>
    void save(Archive &ar, const Transform &t) {
        // 保存时，获取值并委托给上面的 Vec3/Quat 序列化函数
        // 它们会自动转为字符串
        ar(cereal::make_nvp("position", t.get_position()),
           cereal::make_nvp("scale",    t.get_scale()),
           cereal::make_nvp("rotation", t.get_rotation()));
    }

    template<class Archive>
    void load(Archive &ar, Transform &t) {
        Vec3 p, s;
        Quaternion q;
        // 加载到临时变量
        ar(cereal::make_nvp("position", p),
           cereal::make_nvp("scale",    s),
           cereal::make_nvp("rotation", q));
        
        // 通过 Setter 设置回去
        t.set_position(p);
        t.set_scale(s);
        t.set_rotation(q);
    }

} // namespace cereal
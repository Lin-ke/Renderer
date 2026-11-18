#pragma once

#include "engine/core/math/math.h"
#include "engine/core/math/extent.h"
#include "engine/core/math/transform.h"
#include "serialize.h"
#include <sstream>
#include <string>
#include <iomanip>
#include <array>
#include <charconv>
#include <optional>
#include <string_view>
#include <type_traits>


namespace Utils {
    // Existing JSON and binary archives both store compact math values as text.
    // Parse that text without constructing a stream or consulting the locale.
    template<typename Scalar>
    std::optional<std::array<Scalar, 4>> parse_compact_components(std::string_view text, int components) {
        if (text.empty() || components < 1 || components > 4) return std::nullopt;
        std::array<Scalar, 4> values{};
        auto cursor = text.data();
        const auto end = cursor + text.size();
        for (int i = 0; i < components; ++i) {
            while (cursor != end && (*cursor == ' ' || (*cursor >= '\t' && *cursor <= '\r'))) ++cursor;
            // from_chars does not accept the leading '+' supported by the old reader.
            if (cursor != end && *cursor == '+') {
                ++cursor;
                if (cursor == end || *cursor == '+' || *cursor == '-') return std::nullopt;
            }
            const auto result = std::from_chars(cursor, end, values[i]);
            if (result.ec != std::errc{}) return std::nullopt;
            cursor = result.ptr;
        }
        while (cursor != end && (*cursor == ' ' || (*cursor >= '\t' && *cursor <= '\r'))) ++cursor;
        if (cursor != end) return std::nullopt;
        return values;
    }

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
    void from_compact_string(std::string_view s, T& v, int components) {
        using Scalar = std::remove_cvref_t<decltype(v[0])>;
        if (auto values = parse_compact_components<Scalar>(s, components)) {
            for (int i = 0; i < components; ++i) v[i] = (*values)[i];
            return;
        }
        // Retain the previous reader for unusual legacy text (including its
        // locale and integer-overflow behavior), without changing archive bytes.
        std::stringstream ss{std::string(s)};
        for (int i = 0; i < components; ++i) {
            ss >> v[i];
        }
    }

    // Match the existing BinaryOutputArchive vector/minimal-string wire format:
    // uint64 element count, then uint64 byte count and text for each element.
    // Reading this directly avoids per-element archive dispatch and temporary
    // string allocations; JSON and all saved archive bytes remain unchanged.
    template<typename T>
    void load_compact_vector(cereal::BinaryInputArchive& ar, std::vector<T>& values, int components) {
        cereal::size_type count = 0;
        ar.loadBinary(&count, sizeof(count));
        values.resize(static_cast<size_t>(count));
        std::array<char, 256> compact;
        std::string extended;
        for (auto& value : values) {
            cereal::size_type size = 0;
            ar.loadBinary(&size, sizeof(size));
            if (size <= compact.size()) {
                ar.loadBinary(compact.data(), static_cast<std::streamsize>(size));
                from_compact_string(std::string_view(compact.data(), static_cast<size_t>(size)), value, components);
            } else {
                extended.resize(static_cast<size_t>(size));
                ar.loadBinary(extended.data(), static_cast<std::streamsize>(size));
                from_compact_string(extended, value, components);
            }
        }
    }
}

namespace cereal {

    // Extent (2D/3D) - compact as "width height [depth]"
    template <class Archive>
    std::string save_minimal(const Archive&, const Extent2D& e) {
        std::stringstream ss;
        ss << e.width << " " << e.height;
        return ss.str();
    }
    template <class Archive>
    void load_minimal(const Archive&, Extent2D& e, const std::string& s) {
        if (auto values = Utils::parse_compact_components<uint32_t>(s, 2)) {
            e.width = (*values)[0]; e.height = (*values)[1];
            return;
        }
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
        if (auto values = Utils::parse_compact_components<uint32_t>(s, 3)) {
            e.width = (*values)[0]; e.height = (*values)[1]; e.depth = (*values)[2];
            return;
        }
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

    #define IMPLEMENT_BINARY_COMPACT_VECTOR_LOAD(TYPE, COMPONENT_COUNT) \
    inline void load(BinaryInputArchive& ar, std::vector<TYPE>& values) { \
        Utils::load_compact_vector(ar, values, COMPONENT_COUNT); \
    }

    IMPLEMENT_BINARY_COMPACT_VECTOR_LOAD(Vec2, 2)
    IMPLEMENT_BINARY_COMPACT_VECTOR_LOAD(Vec3, 3)
    IMPLEMENT_BINARY_COMPACT_VECTOR_LOAD(Vec4, 4)
    IMPLEMENT_BINARY_COMPACT_VECTOR_LOAD(IVec2, 2)
    IMPLEMENT_BINARY_COMPACT_VECTOR_LOAD(IVec3, 3)
    IMPLEMENT_BINARY_COMPACT_VECTOR_LOAD(IVec4, 4)
    IMPLEMENT_BINARY_COMPACT_VECTOR_LOAD(UVec2, 2)
    IMPLEMENT_BINARY_COMPACT_VECTOR_LOAD(UVec3, 3)
    IMPLEMENT_BINARY_COMPACT_VECTOR_LOAD(UVec4, 4)

    #undef IMPLEMENT_BINARY_COMPACT_VECTOR_LOAD
    #undef IMPLEMENT_COMPACT_SERIALIZE

    // Quaternion 包含 x,y,z,w
    template <class Archive>
    std::string save_minimal(const Archive&, const Quaternion& q) {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(6);
        ss << q.x << " " << q.y << " " << q.z << " " << q.w;
        return ss.str();
    }
    template <class Archive>
    void load_minimal(const Archive&, Quaternion& q, const std::string& s) {
        if (auto values = Utils::parse_compact_components<float>(s, 4)) {
            q = Quaternion((*values)[0], (*values)[1], (*values)[2], (*values)[3]);
            return;
        }
        std::stringstream ss(s);
        float x = q.x, y = q.y, z = q.z, w = q.w;
        ss >> x >> y >> z >> w;
        q = Quaternion(x, y, z, w);
    }

    // Matrices - each column as separate string
    template <class Archive>
    void serialize(Archive &ar, Mat2 &m) {
        Vec2 c0 = m.col(0); Vec2 c1 = m.col(1);
        ar(cereal::make_nvp("col0", c0), cereal::make_nvp("col1", c1));
        if constexpr (Archive::is_loading::value) {
            m.set_col(0, c0); m.set_col(1, c1);
        }
    }

    template <class Archive>
    void serialize(Archive &ar, Mat3 &m) {
        Vec3 c0 = m.col(0); Vec3 c1 = m.col(1); Vec3 c2 = m.col(2);
        ar(cereal::make_nvp("col0", c0), cereal::make_nvp("col1", c1), cereal::make_nvp("col2", c2));
        if constexpr (Archive::is_loading::value) {
            m.set_col(0, c0); m.set_col(1, c1); m.set_col(2, c2);
        }
    }

    template <class Archive>
    void serialize(Archive &ar, Mat4 &m) {
        Vec4 c0 = m.col(0); Vec4 c1 = m.col(1); Vec4 c2 = m.col(2); Vec4 c3 = m.col(3);
        ar(cereal::make_nvp("col0", c0), cereal::make_nvp("col1", c1), 
           cereal::make_nvp("col2", c2), cereal::make_nvp("col3", c3));
        if constexpr (Archive::is_loading::value) {
            m.set_col(0, c0); m.set_col(1, c1); m.set_col(2, c2); m.set_col(3, c3);
        }
    }

    // Transform - split Save/Load for Getters/Setters
    template<class Archive>
    void save(Archive &ar, const Transform &t) {
        
        ar(cereal::make_nvp("position", t.get_position()),
           cereal::make_nvp("scale",    t.get_scale()),
           cereal::make_nvp("rotation", t.get_rotation()));
    }

    template<class Archive>
    void load(Archive &ar, Transform &t) {
        Vec3 p, s;
        Quaternion q;
        
        ar(cereal::make_nvp("position", p),
           cereal::make_nvp("scale",    s),
           cereal::make_nvp("rotation", q));
        
        
        t.set_position(p);
        t.set_scale(s);
        t.set_rotation(q);
    }

} // namespace cereal

#ifndef ENTITY_H
#define ENTITY_H
#include "engine/core/math/extent.h"
#include "engine/core/math/math.h"
#include <cereal/access.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/list.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/polymorphic.hpp>
#include <cereal/types/queue.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/utility.hpp>
#include <cereal/types/vector.hpp>


namespace cereal {
template <class Archive>
void serialize(Archive &ar, Extent2D &e) {
	ar(cereal::make_nvp("width", e.width), cereal::make_nvp("height", e.height));
}
template <class Archive>
void serialize(Archive &ar, Extent3D &e) {
	ar(cereal::make_nvp("width", e.width), cereal::make_nvp("height", e.height), cereal::make_nvp("depth", e.depth));
}

template <class Archive>
void serialize(Archive &ar, Vec2 &v) {
	ar(cereal::make_nvp("x", v.x()), cereal::make_nvp("y", v.y()));
}
template <class Archive>
void serialize(Archive &ar, Vec3 &v) {
	ar(cereal::make_nvp("x", v.x()), cereal::make_nvp("y", v.y()), cereal::make_nvp("z", v.z()));
}
template <class Archive>
void serialize(Archive &ar, Vec4 &v) {
	ar(cereal::make_nvp("x", v.x()), cereal::make_nvp("y", v.y()), cereal::make_nvp("z", v.z()), cereal::make_nvp("w", v.w()));
}
template <class Archive>
void serialize(Archive &ar, IVec2 &v) {
	ar(cereal::make_nvp("x", v.x()), cereal::make_nvp("y", v.y()));
}
template <class Archive>
void serialize(Archive &ar, IVec3 &v) {
	ar(cereal::make_nvp("x", v.x()), cereal::make_nvp("y", v.y()), cereal::make_nvp("z", v.z()));
}
template <class Archive>
void serialize(Archive &ar, IVec4 &v) {
	ar(cereal::make_nvp("x", v.x()), cereal::make_nvp("y", v.y()), cereal::make_nvp("z", v.z()), cereal::make_nvp("w", v.w()));
}
template <class Archive>
void serialize(Archive &ar, UVec2 &v) {
	ar(cereal::make_nvp("x", v.x()), cereal::make_nvp("y", v.y()));
}
template <class Archive>
void serialize(Archive &ar, UVec3 &v) {
	ar(cereal::make_nvp("x", v.x()), cereal::make_nvp("y", v.y()), cereal::make_nvp("z", v.z()));
}
template <class Archive>
void serialize(Archive &ar, UVec4 &v) {
	ar(cereal::make_nvp("x", v.x()), cereal::make_nvp("y", v.y()), cereal::make_nvp("z", v.z()), cereal::make_nvp("w", v.w()));
}
template <class Archive>
void serialize(Archive &ar, Quaternion &q) {
	ar(cereal::make_nvp("x", q.x()), cereal::make_nvp("y", q.y()), cereal::make_nvp("z", q.z()), cereal::make_nvp("w", q.w()));
}
template <class Archive>
void serialize(Archive &ar, Mat2 &m) {
	ar(cereal::make_nvp("col0", (Vec2)m.col(0)),
			cereal::make_nvp("col1", (Vec2)m.col(1)));
}
template <class Archive>
void serialize(Archive &ar, Mat3 &m) {
	ar(cereal::make_nvp("col0", (Vec3)m.col(0)),
			cereal::make_nvp("col1", (Vec3)m.col(1)),
			cereal::make_nvp("col2", (Vec3)m.col(2)));
}
template <class Archive>
void serialize(Archive &ar, Mat4 &m) {
	ar(cereal::make_nvp("col0", (Vec4)m.col(0)),
			cereal::make_nvp("col1", (Vec4)m.col(1)),
			cereal::make_nvp("col2", (Vec4)m.col(2)),
			cereal::make_nvp("col3", (Vec4)m.col(3)));
}


} //namespace cereal

#endif
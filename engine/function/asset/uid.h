#ifndef UUID_H
#define UUID_H

#include <string>
#include <uuid.h>

class UID {
public:
    UID();
    explicit UID(const std::string& str);
    static UID empty();
    const std::string& to_string() const { return str; }
    const uuids::uuid& get_raw() const { return id; }
    bool operator==(const UID& other) const { return id == other.id; }
    bool operator!=(const UID& other) const { return id != other.id; }
    bool is_empty() const { return id.is_nil(); }

private:
    // 私有构造：用于 Empty()，避免不必要的随机生成
    struct NullTag {};
    explicit UID(NullTag);

private:
    uuids::uuid id;
    std::string str;
    template<class Archive>
    void serialize(Archive& archive) {
        archive(str);
        if constexpr (Archive::is_loading::value) {
            auto parsed = uuids::uuid::from_string(str);
            if (parsed) {
                id = *parsed;
            }
        }
    }
};

namespace std {
    template <>
    struct hash<UID> {
        size_t operator()(const UID& uid) const {
            return std::hash<uuids::uuid>{}(uid.get_raw());
        }
    };
}

#endif
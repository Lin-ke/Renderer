#ifndef UUID_H
#define UUID_H

#include <string>
#include <boost/uuid/uuid.hpp>           
#include <boost/uuid/uuid_generators.hpp> 
#include <boost/uuid/uuid_io.hpp>        
#include <cereal/types/memory.hpp>
#include <cereal/types/base_class.hpp>
#include <cereal/access.hpp>

class UID {
public:
    UID();
    explicit UID(const std::string& str);
    static UID empty();
    const std::string& to_string() const { return str; }
    const boost::uuids::uuid& get_raw() const { return id; }
    bool operator==(const UID& other) const { return id == other.id; }
    bool operator!=(const UID& other) const { return id != other.id; }
    bool is_empty() const { return id.is_nil(); }

private:
    // 私有构造：用于 Empty()，避免不必要的随机生成
    struct NullTag {};
    explicit UID(NullTag);

private:
    boost::uuids::uuid id;
    std::string str;
    friend class cereal::access;
    template<class Archive>
    void serialize(Archive& archive) {
        archive(str);
        if constexpr (Archive::is_loading::value) {
            boost::uuids::string_generator gen;
            id = gen(str);
        }
    }
    
};

// hash for unordered_map
template<>
struct std::hash<UID> {
    std::size_t operator()(const UID& uid) const noexcept {
        return boost::uuids::hash_value(uid.get_raw());
    }
};
#endif
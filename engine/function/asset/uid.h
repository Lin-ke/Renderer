#ifndef UUID_H
#define UUID_H

#include <string>
#include <boost/uuid/uuid.hpp>            // 核心 uuid 类
#include <boost/uuid/uuid_generators.hpp> // 生成器
#include <boost/uuid/uuid_io.hpp>         // 输入输出流 (to_string)

class UID {
public:
    // 1. 默认构造：生成随机 UUID
    UID();

    // 2. 字符串构造：解析 UUID 字符串
    explicit UID(const std::string& str);

    // 3. 静态方法：获取空 UUID
    static UID Empty();

    // Getter
    const std::string& ToString() const { return str; }
    const boost::uuids::uuid& GetRaw() const { return id; }

    // 运算符重载（可选，方便比较）
    bool operator==(const UID& other) const { return id == other.id; }
    bool operator!=(const UID& other) const { return id != other.id; }

private:
    // 私有构造：用于 Empty()，避免不必要的随机生成
    struct NullTag {};
    explicit UID(NullTag);

private:
    boost::uuids::uuid id;
    std::string str;
};

#endif
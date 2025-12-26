#include "uid.h"
#include <boost/uuid/string_generator.hpp>
UID::UID()
{
    static thread_local boost::uuids::random_generator generator;
    this->id = generator();
    this->str = boost::uuids::to_string(this->id);
}

UID::UID(const std::string& input_str)
{
    boost::uuids::string_generator gen;
    this->id = gen(input_str);
    this->str = input_str; // 或者重新 to_string 格式化以保证标准格式
}

// 私有构造：专门用于创建空对象
UID::UID(NullTag)
{
    this->id = boost::uuids::nil_uuid(); // boost 提供的“零”UUID (0000...)
    this->str = boost::uuids::to_string(this->id);
}

// 静态 Empty 方法
UID UID::empty()
{
    return UID(NullTag{});
}
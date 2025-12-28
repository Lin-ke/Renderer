#include "uid.h"
#include <random>

UID::UID()
{
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local uuids::uuid_random_generator generator(gen);
    
    this->id = generator();
    this->str = uuids::to_string(this->id);
}

UID::UID(const std::string& input_str)
{
    auto parsed = uuids::uuid::from_string(input_str);
    if (parsed) {
        this->id = *parsed;
    } else {
        this->id = uuids::uuid(); // nil
    }
    this->str = input_str; 
}

// 私有构造：专门用于创建空对象
UID::UID(NullTag)
{
    this->id = uuids::uuid(); // nil
    this->str = uuids::to_string(this->id);
}

// 静态 Empty 方法
UID UID::empty()
{
    return UID(NullTag{});
}

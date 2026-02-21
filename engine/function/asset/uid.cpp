#include "uid.h"
#include <random>
#include <array>
#include <cstring>
#include <limits>

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

UID UID::from_hash(const std::string& input_str) {
    std::hash<std::string> hasher;
    size_t seed = hasher(input_str);
    
    std::mt19937_64 gen(seed);
    std::uniform_int_distribution<uint64_t> dist(0, std::numeric_limits<uint64_t>::max());
    
    uint64_t high = dist(gen);
    uint64_t low = dist(gen);
    
    std::array<uint8_t, 16> bytes;
    std::memcpy(bytes.data(), &high, 8);
    std::memcpy(bytes.data() + 8, &low, 8);
    
    UID uid;
    uid.id = uuids::uuid(bytes.begin(), bytes.end());
    uid.str = uuids::to_string(uid.id);
    return uid;
    return uid;
}

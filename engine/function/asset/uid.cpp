#include "uid.h"
#include <random>
#include <array>
#include <cstring>
#include <limits>

UID::UID()
{
    this->id_ = uuids::uuid(); // nil by default
    this->str_ = uuids::to_string(this->id_);
}

UID UID::generate()
{
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local uuids::uuid_random_generator generator(gen);
    
    UID uid;
    uid.id_ = generator();
    uid.str_ = uuids::to_string(uid.id_);
    return uid;
}

UID::UID(const std::string& input_str)
{
    auto parsed = uuids::uuid::from_string(input_str);
    if (parsed) {
        this->id_ = *parsed;
    } else {
        this->id_ = uuids::uuid(); // nil
    }
    this->str_ = input_str; 
}

UID::UID(NullTag)
{
    this->id_ = uuids::uuid(); // nil
    this->str_ = uuids::to_string(this->id_);
}

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
    
    UID uid; // default is nil, override below
    uid.id_ = uuids::uuid(bytes.begin(), bytes.end());
    uid.str_ = uuids::to_string(uid.id_);
    return uid;
}

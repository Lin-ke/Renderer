#include "uid.h"
#include <iostream>

// 默认构造函数：生成随机 UUID (Version 4)
UID::UID()
{
    // 使用 thread_local 保证线程安全且高性能
    // boost::uuids::random_generator 内部会自动寻找系统熵源进行初始化
    static thread_local boost::uuids::random_generator generator;

    this->id = generator();
    this->str = boost::uuids::to_string(this->id);
}

// 从字符串解析 UUID
UID::UID(const std::string& inputStr)
{
    // string_generator 用于将字符串解析为 uuid 对象
    boost::uuids::string_generator gen;
    
    try {
        this->id = gen(inputStr);
        this->str = inputStr; // 或者重新 to_string 格式化以保证标准格式
    }
    catch (const std::runtime_error& e) {
        // 解析失败处理：回退到空 UUID 或抛出异常
        // 这里演示回退到 Empty
        this->id = boost::uuids::nil_uuid();
        this->str = boost::uuids::to_string(this->id);
        std::cerr << "Invalid UUID string: " << inputStr << std::endl;
    }
}

// 私有构造：专门用于创建空对象
UID::UID(NullTag)
{
    this->id = boost::uuids::nil_uuid(); // boost 提供的“零”UUID (0000...)
    this->str = boost::uuids::to_string(this->id);
}

// 静态 Empty 方法
UID UID::Empty()
{
    return UID(NullTag{});
}
#include "uid.h"
#include <random>
#include <charconv>
#include <iomanip>

// 辅助：Hex 字符转数字
static constexpr uint8_t hex_to_bin(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

UID::UID() {
    // 使用 thread_local 避免锁竞争，大幅提升并发性能
    static thread_local std::mt19937_64 gen(std::random_device{}());
    static thread_local std::uniform_int_distribution<uint64_t> dist;

    // 一次生成两个 64 位整数填满 16 字节
    uint64_t* p = reinterpret_cast<uint64_t*>(data_.data());
    p[0] = dist(gen);
    p[1] = dist(gen);

    // 设置 UUID v4 版本位和变体位
    // Variant: 10xxxxxx (Byte 8)
    data_[8] = (data_[8] & 0x3f) | 0x80;
    // Version: 0100xxxx (Byte 6)
    data_[6] = (data_[6] & 0x0f) | 0x40;
}

UID::UID(std::string_view str) : data_{0} {
    // 简单解析：跳过 '-'，读取 hex
    // 格式: 8-4-4-4-12 (36 chars)
    // 如果长度不够或解析失败，保持为 0 (empty)
    
    // 快速路径：如果不含 '-' 且长度为 32
    // 完整路径：过滤 '-'
    size_t byte_idx = 0;
    bool high_nibble = true;

    for (char c : str) {
        if (c == '-') continue;
        if (byte_idx >= 16) break;

        uint8_t val = hex_to_bin(c);
        if (high_nibble) {
            data_[byte_idx] = static_cast<uint8_t>(val << 4);
        } else {
            data_[byte_idx] |= val;
            byte_idx++;
        }
        high_nibble = !high_nibble;
    }
}

bool UID::is_empty() const noexcept {
    // 检查是否全 0
    // 利用 64 位整型比较加速
    const uint64_t* p = reinterpret_cast<const uint64_t*>(data_.data());
    return p[0] == 0 && p[1] == 0;
}

std::string UID::to_string() const {
    // 使用 std::format 格式化 (比 stringstream 快得多)
    return std::format("{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
        data_[0], data_[1], data_[2], data_[3],
        data_[4], data_[5],
        data_[6], data_[7],
        data_[8], data_[9],
        data_[10], data_[11], data_[12], data_[13], data_[14], data_[15]);
}

void UID::write(std::ostream &os, bool is_binary) const {
    if (is_binary) {
        os.write(reinterpret_cast<const char*>(data_.data()), data_.size());
    } else {
        std::string s = to_string();
        os.write(s.c_str(), s.size());
    }
}

void UID::read(std::istream &is, bool is_binary) {
    if (is_binary) {
        is.read(reinterpret_cast<char*>(data_.data()), data_.size());
    } else {
        // 读取 36 字节的 UUID 字符串
        // buffer 大小 37 确保有位置放 \0 (虽然 parse 不需要，但为了安全)
        char buffer[37] = {0}; 
        is.read(buffer, 36);
        if (is.gcount() > 0) {
            *this = UID(std::string_view(buffer, is.gcount()));
        } else {
            *this = UID(NullTag{});
        }
    }
}
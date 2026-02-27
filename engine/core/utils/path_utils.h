#pragma once

#include <filesystem>
#include <optional>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace utils {

/**
 * @brief 获取当前可执行文件所在目录
 */
inline std::filesystem::path get_executable_directory() {
    namespace fs = std::filesystem;
    
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return fs::current_path();
    }
    return fs::path(buffer).parent_path();
#else
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len == -1) {
        return fs::current_path();
    }
    buffer[len] = '\0';
    return fs::path(buffer).parent_path();
#endif
}

/**
 * @brief 查找引擎根目录
 * 
 * 从可执行文件目录向上查找，找到包含 "assets" 和 "engine" 子目录的位置
 * 引擎根目录必须同时包含：assets/ 和 engine/ 目录
 */
inline std::filesystem::path find_engine_root() {
    namespace fs = std::filesystem;
    
    fs::path exe_dir = get_executable_directory();
    fs::path search_dir = exe_dir;
    
    // 向上搜索最多5层，防止无限循环
    for (int i = 0; i < 5; ++i) {
        // 引擎根目录必须同时包含 assets/ 和 engine/ 目录
        // engine/ 目录包含引擎源代码，是区分引擎根目录和 build 目录的关键
        if (fs::exists(search_dir / "assets") && 
            fs::exists(search_dir / "engine") &&
            fs::is_directory(search_dir / "engine")) {
            return search_dir;
        }
        
        fs::path parent = search_dir.parent_path();
        if (parent == search_dir) {
            // 已到达根目录
            break;
        }
        search_dir = parent;
    }
    
    // 如果找不到，尝试从当前工作目录查找（兼容 xmake run 的 set_rundir 模式）
    search_dir = fs::current_path();
    for (int i = 0; i < 3; ++i) {
        if (fs::exists(search_dir / "assets") && 
            fs::exists(search_dir / "engine") &&
            fs::is_directory(search_dir / "engine")) {
            return search_dir;
        }
        fs::path parent = search_dir.parent_path();
        if (parent == search_dir) {
            break;
        }
        search_dir = parent;
    }
    
    // 默认返回当前工作目录
    return fs::current_path();
}

/**
 * @brief 获取引擎路径（替代 ENGINE_PATH 宏）
 */
inline std::filesystem::path get_engine_path() {
    return find_engine_root();
}

} // namespace utils

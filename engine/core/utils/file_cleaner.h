#ifndef FILE_CLEANER_H
#define FILE_CLEANER_H

#include <filesystem>
#include <vector>
#include <algorithm>
#include <string>
namespace fs = std::filesystem;

namespace utils{


static void clean_old_files(const std::filesystem::path& directory_path, size_t max_keep_count) {
    namespace fs = std::filesystem;

    // 1. 检查目录是否存在
    if (!fs::exists(directory_path) || !fs::is_directory(directory_path)) {
        return;
    }

    // 2. 收集所有普通文件
    std::vector<fs::directory_entry> files;
    try {
        for (const auto& entry : fs::directory_iterator(directory_path)) {
            if (entry.is_regular_file()) {
                files.push_back(entry);
            }
        }
    } catch (const fs::filesystem_error& e) {
        // Directory iteration error - silently return
        return;
    }

    // 3. 如果文件数量未超过阈值，直接返回
    if (files.size() <= max_keep_count) {
        return;
    }

    // 4. 按“修改时间”排序：旧文件在前，新文件在后
    // Ascending order: oldest -> newest
    std::sort(files.begin(), files.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
        return a.last_write_time() < b.last_write_time();
    });

    // 5. 计算需要删除的数量
    size_t files_to_delete = files.size() - max_keep_count;

    // 6. 删除最旧的文件（静默处理错误）
    for (size_t i = 0; i < files_to_delete; ++i) {
        try {
            fs::remove(files[i].path());
        } catch (const fs::filesystem_error& e) {
            // Deletion error - silently continue
        }
    }
}

}

#endif

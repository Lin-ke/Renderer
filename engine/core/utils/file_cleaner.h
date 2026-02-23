#pragma once

#include <filesystem>
#include <vector>
#include <algorithm>
#include <string>

namespace utils {

static void clean_old_files(const std::filesystem::path& directory_path, size_t max_keep_count) {
    namespace fs = std::filesystem;

    
    if (!fs::exists(directory_path) || !fs::is_directory(directory_path)) {
        return;
    }

    
    std::vector<fs::directory_entry> files;
    try {
        for (const auto& entry : fs::directory_iterator(directory_path)) {
            if (entry.is_regular_file()) {
                files.push_back(entry);
            }
        }
    } catch (const fs::filesystem_error& e) {
        
        return;
    }

    
    if (files.size() <= max_keep_count) {
        return;
    }

    
    std::sort(files.begin(), files.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
        return a.last_write_time() < b.last_write_time();
    });

    
    size_t files_to_delete = files.size() - max_keep_count;

    
    for (size_t i = 0; i < files_to_delete; ++i) {
        try {
            fs::remove(files[i].path());
        } catch (const fs::filesystem_error& e) {
            
        }
    }
}

} // namespace utils

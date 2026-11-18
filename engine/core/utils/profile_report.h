#pragma once

#include <filesystem>
#include <string_view>

// Embeds a complete JSON capture in an offline, self-contained report.
bool write_profile_html(const std::filesystem::path& path, std::string_view report);

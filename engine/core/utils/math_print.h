#pragma once

#include <string>
#include <cereal/cereal.hpp>
#include <cereal/archives/json.hpp>

namespace cereal {

template <typename T>
std::string to_json_string(const T& obj, const std::string& root_name = "value") {
    std::stringstream ss;
    {
        cereal::JSONOutputArchive archive(ss);
        archive(cereal::make_nvp(root_name, obj));
    } 
    return ss.str();
}

} // namespace cereal

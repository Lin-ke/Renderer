#pragma once

#include "engine/function/render/rhi/rhi_structs.h"
#include "engine/function/render/rhi/rhi.h"
#include "engine/function/asset/asset.h"
#include <string>
#include <memory>
#include <cereal/access.hpp>
#include <cereal/types/string.hpp>

/**
 * @brief Represents a shader asset in the engine.
 * Handles loading of shader binary data and creation of RHI shader objects.
 */
class Shader : public Asset {
public:
    Shader() = default;

    /**
     * @brief Constructs a shader asset.
     * @param path The path to the shader binary file.
     * @param frequency The shader stage (vertex, fragment, compute, etc.).
     * @param entry The entry point function name (default is "main").
     */
    Shader(const std::string& path, ShaderFrequency frequency, const std::string& entry = "main");
    
    virtual std::string_view get_asset_type_name() const override { return "Shader Asset"; }
    virtual AssetType get_asset_type() const override { return AssetType::Shader; }

    virtual void on_load() override;

    inline std::string get_file_path() const { return path_; }
    inline ShaderFrequency get_frequency() const { return frequency_; }
    inline std::string get_entry() const { return entry_; }

    RHIShaderRef shader_;

private:
    std::string path_;
    ShaderFrequency frequency_ = SHADER_FREQUENCY_VERTEX;
    std::string entry_ = "main";

    friend class cereal::access;
    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::base_class<Asset>(this));
        ar(cereal::make_nvp("path", path_));
        ar(cereal::make_nvp("frequency", frequency_));
        ar(cereal::make_nvp("entry", entry_));
    }
};
using ShaderRef = std::shared_ptr<Shader>;

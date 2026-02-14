#include "engine/function/render/render_resource/shader.h"
#include "engine/main/engine_context.h"
#include "engine/function/asset/asset_manager.h"
#include "engine/core/log/Log.h"
#include <fstream>
#include <vector>

DECLARE_LOG_TAG(LogShader);
DEFINE_LOG_TAG(LogShader, "Shader");

Shader::Shader(const std::string& path, ShaderFrequency frequency, const std::string& entry)
    : path_(path), frequency_(frequency), entry_(entry) {
    on_load();
}

void Shader::on_load() {
    if (path_.empty()) {
        return;
    }

    std::string physical_path = path_;
    if (EngineContext::asset()) {
        auto path_opt = EngineContext::asset()->get_physical_path(path_);
        if (path_opt) {
            physical_path = path_opt->string();
        }
    }

    std::ifstream file(physical_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        ERR(LogShader, "Failed to open shader file: {}", physical_path);
        return;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(size);
    if (!file.read((char*)buffer.data(), size)) {
        ERR(LogShader, "Failed to read shader file: {}", physical_path);
        return;
    }

    RHIShaderInfo info = {};
    info.entry = entry_;
    info.frequency = frequency_;
    info.code = std::move(buffer);

    if (EngineContext::rhi()) {
        shader_ = EngineContext::rhi()->create_shader(info);
        if (!shader_) {
            ERR(LogShader, "Failed to create RHI shader from: {}", physical_path);
        }
    } else {
        WARN(LogShader, "RHI is not initialized, shader creation deferred.");
    }
}

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/types/polymorphic.hpp>

CEREAL_REGISTER_TYPE(Shader);
CEREAL_REGISTER_POLYMORPHIC_RELATION(Asset, Shader);

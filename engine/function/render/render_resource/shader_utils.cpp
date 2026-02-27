#include "engine/function/render/render_resource/shader_utils.h"
#include "engine/main/engine_context.h"
#include "engine/function/render/rhi/rhi.h"
#include "engine/core/log/Log.h"
#include "engine/core/utils/path_utils.h"

#include <fstream>
#include <filesystem>

DEFINE_LOG_TAG(LogShaderUtils, "ShaderUtils");

namespace render {

std::optional<std::vector<uint8_t>> ShaderUtils::load_compiled_shader(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return std::nullopt;
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return std::nullopt;
    }
    
    return buffer;
}

std::string ShaderUtils::get_shader_output_dir() {
    std::string engine_path = utils::get_engine_path().string();
    // Try different locations for the shader directory
    std::vector<std::string> possible_paths = {
        engine_path + "/shaders",
        engine_path + "/build/shaders",
        engine_path + "/build/windows/x64/debug/shaders",
        engine_path + "/build/windows/x64/release/shaders",
        "shaders",
    };
    
    for (const auto& path : possible_paths) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    
    return "shaders";
}

std::string ShaderUtils::get_shader_source_dir() {
    std::string engine_path = utils::get_engine_path().string();
    // Try different locations for the shader source directory
    std::vector<std::string> possible_paths = {
        engine_path + "/assets/shaders",
        "assets/shaders",
    };
    
    for (const auto& path : possible_paths) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    
    return "assets/shaders";
}

std::optional<std::string> ShaderUtils::load_shader_source(const std::string& hlsl_name) {
    std::string shader_dir = get_shader_source_dir();
    std::string hlsl_path = shader_dir + "/" + hlsl_name;
    
    std::ifstream file(hlsl_path);
    if (!file.is_open()) {
        return std::nullopt;
    }
    
    std::string source((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return source;
}

std::vector<uint8_t> ShaderUtils::load_or_compile(
    const std::string& cso_name,
    const char* source,
    const char* entry,
    const char* profile
) {
    // Try to load pre-compiled shader
    std::string shader_dir = get_shader_output_dir();
    std::string cso_path = shader_dir + "/" + cso_name;
    
    auto compiled = load_compiled_shader(cso_path);
    if (compiled.has_value()) {
        INFO(LogShaderUtils, "Loaded pre-compiled shader: {}", cso_path);
        return *compiled;
    }
    
    // Fallback to runtime compilation
    INFO(LogShaderUtils, "Pre-compiled shader not found ({}), falling back to runtime compilation", cso_path);
    
    auto backend = EngineContext::rhi();
    if (!backend) {
        ERR(LogShaderUtils, "RHI backend not available for shader compilation");
        return {};
    }
    
    // If no source provided, try to load from .hlsl file
    std::string shader_source;
    const char* compile_source = source;
    
    if (!compile_source) {
        // Derive hlsl filename from cso_name (e.g., "forward_pass_vs.cso" -> "forward_pass.hlsl")
        std::string base_name = cso_name;
        // Remove _vs.cso or _ps.cso suffix
        size_t suffix_pos = base_name.rfind("_vs.cso");
        if (suffix_pos == std::string::npos) {
            suffix_pos = base_name.rfind("_ps.cso");
        }
        if (suffix_pos != std::string::npos) {
            base_name = base_name.substr(0, suffix_pos) + ".hlsl";
        }
        
        auto hlsl_source = load_shader_source(base_name);
        if (hlsl_source.has_value()) {
            shader_source = *hlsl_source;
            compile_source = shader_source.c_str();
            INFO(LogShaderUtils, "Loaded shader source from: {}", base_name);
        } else {
            ERR(LogShaderUtils, "Failed to load shader source: {}", base_name);
            return {};
        }
    }
    
    return backend->compile_shader(compile_source, entry, profile);
}

} // namespace render

#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <optional>

namespace render {

/**
 * @brief Utility functions for shader loading and compilation
 */
class ShaderUtils {
public:
    /**
     * @brief Load pre-compiled shader bytecode from .cso file
     * @param path Path to the .cso file (relative to executable)
     * @return Shader bytecode if successful, empty optional otherwise
     */
    static std::optional<std::vector<uint8_t>> load_compiled_shader(const std::string& path);
    
    /**
     * @brief Get the default shader output directory
     * @return Path to the shader output directory
     */
    static std::string get_shader_output_dir();
    
    /**
     * @brief Get the default shader source directory
     * @return Path to the shader source directory
     */
    static std::string get_shader_source_dir();
    
    /**
     * @brief Load shader source from .hlsl file
     * @param hlsl_name Name of the .hlsl file (e.g., "forward_pass.hlsl")
     * @return Shader source code if successful, empty optional otherwise
     */
    static std::optional<std::string> load_shader_source(const std::string& hlsl_name);
    
    /**
     * @brief Try to load compiled shader, fallback to source compilation
     * @param cso_name Name of the .cso file (e.g., "pbr_forward_vs.cso")
     * @param source HLSL source code for fallback compilation (can be nullptr to auto-load from file)
     * @param entry Entry point name (e.g., "VSMain")
     * @param profile Shader profile (e.g., "vs_5_0")
     * @return Compiled shader bytecode
     */
    static std::vector<uint8_t> load_or_compile(
        const std::string& cso_name,
        const char* source,
        const char* entry,
        const char* profile
    );
};

} // namespace render

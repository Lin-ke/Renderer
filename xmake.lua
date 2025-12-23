add_rules("mode.debug", "mode.release")
add_requires( "glfw", "imgui", "stb", "assimp", "cereal", "spdlog", "eventpp", )
set_encodings("utf-8")

if is_os("windows") then 
    add_defines("UNICODE")
    add_defines("_UNICODE")
end

function add_dx_sdk_options()
    add_syslinks("d3d11","dxgi","dxguid","D3DCompiler","d2d1","dwrite","winmm","user32","gdi32","ole32")
end


target("renderer")
    set_languages("c++20")
    set_kind("binary")
    add_files("src/*.cpp")
    add_packages("glfw", "imgui", "stb", "assimp", "cereal", "spdlog", "eventpp")
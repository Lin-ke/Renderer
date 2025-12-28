add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode"})

add_requires("glfw", "imgui", "stb", "assimp", "cereal", "eventpp", "eigen", "glog", "stduuid")

set_encodings("utf-8")
add_defines("UNICODE", "_UNICODE")

target("engine")
    set_kind("static")
    set_languages("c++20")
    add_includedirs(".", {public = true})
    add_files("engine/**.cpp")
    add_packages("glfw", "imgui", "stb", "assimp", "cereal", "boost", "eventpp",  "eigen", "glog", "stduuid", {public = true} )
    add_syslinks("d3d11", "dxgi", "dxguid", "D3DCompiler", "d2d1", "dwrite", "winmm", "user32", "gdi32", "ole32")

target("game")
    set_kind("binary")
    set_languages("c++20")
    set_rundir("$(projectdir)")
    add_files("game/**.cpp")
    add_deps("engine")

-- xmake f -c --vs=2022 --vs_toolset=14.3
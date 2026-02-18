add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode"})


add_requires("glog", {configs = {gflags = false}})
add_requires("glfw", "stb", "assimp", "cereal", "eventpp", "eigen",  "stduuid", "catch2")
add_requires("imgui", {configs = {win32 = true, dx11 = true, glfw = true}})
add_requires("imguizmo", {configs = {cxflags = "-DIMGUI_DEFINE_MATH_OPERATORS"}})

set_encodings("utf-8")
add_defines("UNICODE", "_UNICODE")
on_load(function (target)
        local engine_path = os.projectdir()
        engine_path = engine_path:gsub("\\", "/")
        target:add("defines", 'ENGINE_PATH="' .. engine_path .. '"')
    end)
-- clangd目前不支持cpp23：(看.clangd可以调整)
if is_mode("debug") then
    add_defines("_DEBUG_")
end
if is_mode("release") then
    set_options("optimize", "O3")
end
target("engine")
    set_kind("static")
    set_languages("c++20")
    add_defines("IMGUI_DEFINE_MATH_OPERATORS", {public = true})
    add_includedirs(".", {public = true})
    add_files("engine/**.cpp")
    remove_files("RD/**.cpp")

    add_packages("glfw", "imgui", "imguizmo", "stb", "assimp", "cereal", "boost", "eventpp",  "eigen", "glog", "stduuid", {public = true} )
    add_syslinks("d3d11", "dxgi", "dxguid", "D3DCompiler", "d2d1", "dwrite", "winmm", "user32", "gdi32", "ole32")

target("game")
    set_kind("binary")
    set_languages("c++20")
    set_rundir("$(projectdir)")
    add_files("game/**.cpp")
    add_deps("engine")

target("utest")
    set_kind("binary")
    set_languages("c++20")
    add_files("test/**.cpp")
    add_deps("engine")
    add_packages("catch2", "stb")

-- xmake f -c --vs=2022 --mode=debug
-- xmake require --info gflags
add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode"})


add_requires("glog", {configs = {gflags = false}})
add_requires("stb", "assimp", "cereal", "stduuid", "catch2")
add_requires("imgui", {configs = {win32 = true, dx11 = true}})
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

    add_packages("imgui", "imguizmo", "stb", "assimp", "cereal", "glog", "stduuid", {public = true} )
    add_syslinks("d3d11", "dxgi", "dxguid", "D3DCompiler", "d2d1", "dwrite", "winmm", "user32", "gdi32", "ole32")

    -- Compile shaders before building the engine
    before_build(function (target)
        
        local shader_dir = path.join(os.projectdir(), "assets", "shaders")
        local output_dir = path.join(target:targetdir(), "shaders")
        
        if not os.exists(shader_dir) then
            print("No shader directory found at " .. shader_dir)
            return
        end
        
        -- Create output directory
        os.mkdir(output_dir)
        
        -- Find fxc compiler
        local windows_sdk_path = os.getenv("WindowsSdkDir")
        if not windows_sdk_path then
            windows_sdk_path ="C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.26100.0\\x64"
        end
        local fxc_path = path.join(windows_sdk_path, "fxc.exe")
        if os.exists(fxc_path) then
            fxc = {program = fxc_path}
        end
        
        if not fxc then
            print("Warning: fxc not found, skipping shader compilation")
            print("  Shaders will be compiled at runtime (slower startup)")
            return
        end
        
        local compiled_count = 0
        local skipped_count = 0
        
        -- Compile all .hlsl files
        for _, hlsl_file in ipairs(os.files(path.join(shader_dir, "*.hlsl"))) do
            local filename = path.basename(hlsl_file)
            local hlsl_mtime = os.mtime(hlsl_file)
            
            -- Check if it's a compute shader (filename ends with _cs)
            if filename:match("_cs$") then
                -- Compile compute shader
                local cs_output = path.join(output_dir, filename .. ".cso")
                local needs_cs_compile = true
                if os.exists(cs_output) then
                    local cso_mtime = os.mtime(cs_output)
                    if cso_mtime >= hlsl_mtime then
                        needs_cs_compile = false
                    end
                end
                
                if needs_cs_compile then
                    local cs_cmd = string.format('"%s" /T cs_5_0 /E CSMain /Fo "%s" /O3 "%s"', fxc.program, cs_output, hlsl_file)
                    print("Compiling CS: " .. filename)
                    local out = os.iorun(cs_cmd)
                    if out and #out > 0 then
                        print(out)
                    end
                    compiled_count = compiled_count + 1
                else
                    skipped_count = skipped_count + 1
                end
            else
                -- Compile vertex shader
                local vs_output = path.join(output_dir, filename .. "_vs.cso")
                local needs_vs_compile = true
                if os.exists(vs_output) then
                    local cso_mtime = os.mtime(vs_output)
                    if cso_mtime >= hlsl_mtime then
                        needs_vs_compile = false
                    end
                end
                
                if needs_vs_compile then
                    local vs_cmd = string.format('"%s" /T vs_5_0 /E VSMain /Fo "%s" /O3 "%s"', fxc.program, vs_output, hlsl_file)
                    print("Compiling VS: " .. filename)
                    local out = os.iorun(vs_cmd)
                    if out and #out > 0 then
                        print(out)
                    end
                    compiled_count = compiled_count + 1
                else
                    skipped_count = skipped_count + 1
                end
                
                -- Compile pixel shader
                local ps_output = path.join(output_dir, filename .. "_ps.cso")
                local needs_ps_compile = true
                if os.exists(ps_output) then
                    local cso_mtime = os.mtime(ps_output)
                    if cso_mtime >= hlsl_mtime then
                        needs_ps_compile = false
                    end
                end
                
                if needs_ps_compile then
                    local ps_cmd = string.format('"%s" /T ps_5_0 /E PSMain /Fo "%s" /O3 "%s"', fxc.program, ps_output, hlsl_file)
                    print("Compiling PS: " .. filename)
                    local out = os.iorun(ps_cmd)
                    if out and #out > 0 then
                        print(out)
                    end
                    compiled_count = compiled_count + 1
                else
                    skipped_count = skipped_count + 1
                end
            end
        end
        
        if compiled_count > 0 then
            print(string.format("Shader compilation complete (%d compiled, %d up-to-date)", compiled_count, skipped_count))
        end
    end)

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

-- xmake require --info gflags
-- xmake project -k vsxmake -m "debug,release"
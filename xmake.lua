set_project("AbsoluteHOTAS")
set_version("3.0.2")
set_languages("c++23")
set_warnings("all")

add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "WINVER=0x0A00", "_WIN32_WINNT=0x0A00")

add_rules("mode.debug", "mode.releasedbg")

-- Pin to 1.91.0: the version the old vcpkg build used. The DX12 backend changed
-- incompatibly after this (1.92 allocates multiple SRV descriptors at runtime),
-- which corrupts memory against UIHook's single-descriptor heap. Do not float.
add_requires("imgui v1.91.0", {configs = {glfw = false, opengl = false, win32 = true, dx12 = true}})
add_requires("spdlog v1.16.0", { configs = { header_only = false, wchar = true, std_format = true } })
add_requires("minhook")
add_requires("simpleini")

add_ldflags("/MAP")

-- AbsoluteHOTAS is a standalone SFSE plugin. It uses no CommonLibSF runtime: the
-- SFSE load/version ABI is vendored in shared/include/SFSEInterface.h, and the
-- plugin does its own HID input, signature scanning, and trampoline hooks.
target("AbsoluteHOTAS")
    set_kind("shared")

    add_packages("imgui", "spdlog", "minhook", "simpleini")

    add_includedirs("shared/include", "projects/AbsoluteHOTAS/include", ".")

    add_files("shared/src/**.cpp")
    add_files("projects/AbsoluteHOTAS/src/**.cpp")

    set_pcxxheader("shared/include/PCH.h")

    add_defines("PLUGIN_VERSION_MAJOR=3", "PLUGIN_VERSION_MINOR=0", "PLUGIN_VERSION_PATCH=2")

    -- shell32/ole32: SHGetKnownFolderPath + CoTaskMemFree (ControlMap path lookup).
    -- These were previously pulled in transitively by CommonLibSF.
    add_syslinks("dinput8", "dxguid", "d3d12", "dxgi", "version", "shell32", "ole32")

    -- Deploy next to the game's SFSE plugins for a fast edit/build/test loop.
    after_build(function (target)
        local dst = os.getenv("ABSOLUTEHOTAS_DEPLOY_DIR")
        if dst then
            os.cp(target:targetfile(), path.join(dst, "AbsoluteHOTAS.dll"))
            print("Deployed AbsoluteHOTAS.dll -> " .. dst)
        end
    end)

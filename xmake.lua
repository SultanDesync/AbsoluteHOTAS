set_project("AbsoluteHOTAS")
set_version("3.1.0")
set_languages("c++23")
set_warnings("all")

add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "WINVER=0x0A00", "_WIN32_WINNT=0x0A00")

add_rules("mode.debug", "mode.releasedbg")

-- Pin to 1.91.0: the version the old vcpkg build used. The DX12 backend changed
-- incompatibly after this (1.92 allocates multiple SRV descriptors at runtime),
-- which corrupts memory against UIHook's single-descriptor heap. Do not float.
add_requires("imgui v1.91.0", {configs = {glfw = false, opengl = false, win32 = true, dx12 = true}})
add_requires("minhook")
add_requires("simpleini")

add_ldflags("/MAP")

-- Live-test deploy target. Set once (persists in the gitignored .xmake/ config):
--   xmake f --deploydir="D:\Modlists\Starfield Testing Baseline\mods\AbsoluteHOTAS\SFSE\Plugins"
-- Left empty by default so fresh clones and CI build without touching any machine-
-- specific path. The ABSOLUTEHOTAS_DEPLOY_DIR env var is honored as a fallback.
option("deploydir")
    set_default("")
    set_showmenu(true)
    set_description("SFSE/Plugins folder to copy the built DLL into after each build (live testing)")
option_end()

-- AbsoluteHOTAS is a standalone SFSE plugin. It uses no CommonLibSF runtime: the
-- SFSE load/version ABI is vendored in include/SFSEInterface.h, and the plugin
-- does its own HID input, signature scanning, and trampoline hooks.
target("AbsoluteHOTAS")
    set_kind("shared")

    add_packages("imgui", "minhook", "simpleini")

    add_includedirs("include")

    add_files("src/**.cpp")

    set_pcxxheader("include/PCH.h")

    add_defines("PLUGIN_VERSION_MAJOR=3", "PLUGIN_VERSION_MINOR=1", "PLUGIN_VERSION_PATCH=0")

    -- shell32/ole32: SHGetKnownFolderPath + CoTaskMemFree (ControlMap path lookup).
    -- These were previously pulled in transitively by CommonLibSF.
    add_syslinks("dinput8", "dxguid", "d3d12", "dxgi", "version", "shell32", "ole32")

    -- Post-build: stage into the in-repo release layout, then optionally deploy
    -- straight into a test mod's SFSE/Plugins folder for a fast build/test loop.
    after_build(function (target)
        import("core.project.config")

        local dllsrc = target:targetfile()
        local inisrc = path.join(os.projectdir(), "config", "AbsoluteHOTAS.ini")

        -- 1) Stage DLL + INI into contrib/Plugin{Release,Debug}/Data/SFSE/Plugins.
        --    This is the MO2-shaped layout the release zip is built from, so the
        --    staged INI is refreshed every build to track the repo default.
        local layout = (config.get("mode") == "debug") and "PluginDebug" or "PluginRelease"
        local stage  = path.join(os.projectdir(), "contrib", layout, "Data", "SFSE", "Plugins")
        os.mkdir(stage)
        os.cp(dllsrc, path.join(stage, "AbsoluteHOTAS.dll"))
        os.cp(inisrc, path.join(stage, "AbsoluteHOTAS.ini"))
        print("Staged AbsoluteHOTAS.dll + .ini -> " .. stage)

        -- 2) Optional live deploy (see the `deploydir` option above). Guarded so a
        --    fresh clone / CI without a deploydir still builds cleanly.
        local dst = config.get("deploydir")
        if not dst or dst == "" then dst = os.getenv("ABSOLUTEHOTAS_DEPLOY_DIR") end
        if dst and dst ~= "" then
            os.mkdir(dst)
            os.cp(dllsrc, path.join(dst, "AbsoluteHOTAS.dll"))
            -- Seed the INI only if absent so tuned bindings survive rebuilds; delete
            -- the deployed INI to re-seed the default on the next build.
            local inidst = path.join(dst, "AbsoluteHOTAS.ini")
            if not os.isfile(inidst) then
                os.cp(inisrc, inidst)
                print("Seeded AbsoluteHOTAS.ini -> " .. inidst)
            end
            print("Deployed AbsoluteHOTAS.dll -> " .. dst)
        end
    end)

-- Standalone unit test for ControlMapReader. Not built by default; opt in with:
--   xmake build control_map_reader_test
--   xmake run   control_map_reader_test tests/fixtures
-- ControlMapReader.cpp is compiled directly here (no PCH) so the test stays
-- independent of the plugin target and its precompiled header.
target("control_map_reader_test")
    set_kind("binary")
    set_default(false)

    add_includedirs("include")

    add_files("tests/control_map_reader_test.cpp")
    add_files("src/ControlMapReader.cpp")

    -- MapVirtualKeyW (VK -> scancode) in TokenToOutput.
    add_syslinks("user32")

-- Standalone unit test for ConfigMigration::SplitUserConfig (the pure monolith->
-- split transform). Not built by default; opt in with:
--   xmake build config_migration_test
--   xmake run   config_migration_test
-- Compiles ConfigMigration.cpp + RuntimePaths.cpp (its only link dependency) with
-- no PCH, so it stays independent of the plugin target.
target("config_migration_test")
    set_kind("binary")
    set_default(false)

    add_packages("simpleini")
    add_includedirs("include")

    add_files("tests/config_migration_test.cpp")
    add_files("src/ConfigMigration.cpp")
    add_files("src/RuntimePaths.cpp")

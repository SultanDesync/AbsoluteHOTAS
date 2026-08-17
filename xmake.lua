set_project("AbsoluteHOTAS")
set_version("5.0.1")
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
--   xmake f --deploydir="C:\Path\To\Mod\Data\SFSE\Plugins"
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

    add_defines(
        "PLUGIN_VERSION_MAJOR=5",
        "PLUGIN_VERSION_MINOR=0",
        "PLUGIN_VERSION_PATCH=1",
        "PLUGIN_VERSION_STABLE"
    )

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
        --
        --    Both the DLL and the INI are overwritten every build. The INI used to be
        --    seeded only-if-absent to keep tuned bindings from being clobbered, but
        --    since the config split it is mod-owned and holds no user data: bindings,
        --    calibration, macros, and profile routing live in
        --    AbsoluteHOTAS_Custom.ini, which nothing here writes. Overwriting the main
        --    INI is therefore what a real 4.0+ update does, and testing that path on
        --    every build is the point.
        --
        --    Under MO2 the custom/profile files are written to `overwrite/`, not
        --    to the mod folder this deploys into, so a rebuild cannot reach them.
        local dst = config.get("deploydir")
        if not dst or dst == "" then dst = os.getenv("ABSOLUTEHOTAS_DEPLOY_DIR") end
        if dst and dst ~= "" then
            os.mkdir(dst)
            -- The DLL is locked while the game is running; fail loudly rather than
            -- leaving a stale DLL behind a "build ok" that invites testing it.
            os.cp(dllsrc, path.join(dst, "AbsoluteHOTAS.dll"))
            os.cp(inisrc, path.join(dst, "AbsoluteHOTAS.ini"))
            print("Deployed AbsoluteHOTAS.dll + .ini -> " .. dst)
        end
    end)

-- Standalone unit tests are not built by default. Run all of them with:
--   xmake test
-- Or run the ControlMapReader test alone with:
--   xmake test control_map_reader_test/*
-- ControlMapReader.cpp is compiled directly here (no PCH) so the test stays
-- independent of the plugin target and its precompiled header.
target("control_map_reader_test")
    set_kind("binary")
    set_default(false)
    set_rundir(os.projectdir())
    set_runargs("tests/fixtures")
    add_tests("fixtures")

    add_includedirs("include")

    add_files("tests/control_map_reader_test.cpp")
    add_files("src/ControlMapReader.cpp")

    -- MapVirtualKeyW (VK -> scancode) in TokenToOutput.
    add_syslinks("user32")

-- Standalone unit test for ProfileOverlay::ComputeDiff (the sparse profile-overlay
-- diff). Run it alone with:
--   xmake test config_overlay_test/*
-- Compiles ProfileOverlay.cpp (SimpleIni-only) with no PCH.
target("config_overlay_test")
    set_kind("binary")
    set_default(false)
    add_tests("sparse_overlay")

    add_packages("simpleini")
    add_includedirs("include")

    add_files("tests/config_overlay_test.cpp")
    add_files("src/ProfileOverlay.cpp")

-- Header-only BindingRef parser regression tests. These protect the configuration
-- boundary without pulling the DirectInput runtime into the test process.
target("binding_ref_test")
    set_kind("binary")
    set_default(false)
    add_tests("parser")

    add_includedirs("include")
    add_files("tests/binding_ref_test.cpp")

target("absolute_power_api_test")
    set_kind("binary")
    set_default(false)
    add_tests("power_abi_tail")
    add_includedirs("include")
    add_files("tests/absolute_power_api_test.cpp")

-- Header-only pilot-context policy tests. These lock the distinction between
-- piloting, on-foot, and suspended/menu states without loading the game runtime.
target("pilot_state_test")
    set_kind("binary")
    set_default(false)
    add_tests("freshness_policy")

    add_includedirs("include")
    add_files("tests/pilot_state_test.cpp")

-- Header-only compatibility-map tests for the fixed vanilla context inputs.
-- These protect profile-stable action IDs and their exact scan-code outputs.
target("universal_context_input_test")
    set_kind("binary")
    set_default(false)
    add_tests("compatibility_map")

    add_includedirs("include")
    add_files("tests/universal_context_input_test.cpp")

-- Header-only ship-control catalog and routing-policy tests. These lock the
-- reviewed defaults, override eligibility, no-silent-fallback rule, and
-- diagnostic precedence without loading the game runtime.
target("ship_action_catalog_test")
    set_kind("binary")
    set_default(false)
    add_tests("routing_policy")

    add_includedirs("include")
    add_files("tests/ship_action_catalog_test.cpp")

-- Header-only safety-policy tests for optional axis/button reuse in menus.
target("menu_control_reuse_test")
    set_kind("binary")
    set_default(false)
    add_tests("neutral_arming")

    add_includedirs("include")
    add_files("tests/menu_control_reuse_test.cpp")

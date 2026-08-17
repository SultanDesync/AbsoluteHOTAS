#include "AbsoluteControlSubscriber.h"

#include "Plugin.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace {
using namespace AbsoluteControlPanelApi;

constexpr std::string_view kHotasModuleId = "absolute.hotas";

std::atomic<const ApiV1*> g_hostApi{};
std::atomic_bool g_registered{};
std::atomic_bool g_terminalRejection{};
std::atomic_bool g_forceReadException{};
std::mutex g_registrationMutex;

std::atomic_bool g_throttleHookInstalled{};
std::atomic_bool g_nativeControlsInitialized{};
std::atomic_bool g_controllerStarted{};
std::atomic_bool g_legacyWorkbenchConfigured{};
std::atomic_bool g_legacyWorkbenchInstalled{};

const ApiV1 g_incompatibleHostApi = [] {
    ApiV1 api;
    api.structSize = 0;
    api.abiVersion = 0;
    return api;
}();

template <std::size_t N>
void Copy(char (&target)[N], std::string_view source) noexcept
{
    const auto count = (std::min)(source.size(), N - 1);
    std::memcpy(target, source.data(), count);
    target[count] = '\0';
}

template <std::size_t N>
bool Terminated(const char (&value)[N]) noexcept
{
    return std::memchr(value, '\0', N) != nullptr;
}

ValueV1 StringValue(std::string_view value) noexcept
{
    ValueV1 result;
    result.kind = ValueKind::String;
    Copy(result.stringValue, value);
    return result;
}

ControlDescriptorV1 ReadOnlyStatus(std::string_view id, std::string_view label,
                                   std::string_view description) noexcept
{
    ControlDescriptorV1 control;
    control.kind = ControlKind::InputBinding;
    control.flags = kControlReadOnly;
    Copy(control.controlId, id);
    Copy(control.label, label);
    Copy(control.description, description);
    return control;
}

const ModuleDescriptorV1 g_module = [] {
    ModuleDescriptorV1 module;
    Copy(module.moduleId, kHotasModuleId);
    Copy(module.displayName, "AbsoluteHOTAS");
    Copy(module.description,
         "Standalone HOTAS/HOSAS flight controls, bindings, profiles, and runtime status.");
    return module;
}();

const std::array g_setupControls{
    ReadOnlyStatus("setup-control-host", "Absolute Control",
        "Optional native menu connection; flight controls do not depend on the host."),
    ReadOnlyStatus("setup-flight-runtime", "Flight runtime",
        "Startup state of the standalone injection hook and controller poller."),
    ReadOnlyStatus("setup-configuration", "Configuration owner",
        "AbsoluteHOTAS owns defaults, custom settings, profiles, validation, and reload."),
    ReadOnlyStatus("setup-legacy-workbench", "Legacy workbench",
        "The embedded workbench remains an optional transition and fallback frontend."),
    ReadOnlyStatus("setup-suite-modules", "Optional suite modules",
        "Head Tracking and Mouse Alignment remain separately installed, separately owned modules."),
};

const std::array g_diagnosticControls{
    ReadOnlyStatus("diagnostics-compatibility", "Compatibility",
        "AbsoluteHOTAS version, Control ABI, and exact-gated Starfield runtime family."),
    ReadOnlyStatus("diagnostics-native-controls", "Native control seams",
        "Startup result for exact-gated ship-control and flight-writer seams."),
    ReadOnlyStatus("diagnostics-controller", "Controller service",
        "Standalone DirectInput polling and flight-control execution state."),
    ReadOnlyStatus("diagnostics-frontends", "Frontends",
        "Absolute Control connection and embedded-workbench fallback state."),
    ReadOnlyStatus("diagnostics-coordination", "Suite coordination",
        "Current standalone ownership and the deferred optional headless runtime boundary."),
};

Result __cdecl ReadStatus(void*, const char* rawId, ValueV1* output) noexcept
{
    if (!rawId || !output || output->structSize < sizeof(ValueV1)) {
        return Result::InvalidArgument;
    }
    try {
        if (g_forceReadException.load(std::memory_order_acquire)) {
            throw std::runtime_error("forced provider callback failure");
        }

        const std::string_view id{ rawId };
        if (id == "setup-control-host") {
            *output = StringValue(
                "Connected through ABI 1; HOTAS gameplay remains independently initialized.");
        } else if (id == "setup-flight-runtime") {
            *output = StringValue(std::format(
                "Flight hook {} | controller {}",
                g_throttleHookInstalled.load(std::memory_order_acquire) ? "ready" : "unavailable",
                g_controllerStarted.load(std::memory_order_acquire) ? "running" : "not running"));
        } else if (id == "setup-configuration") {
            *output = StringValue(
                "AbsoluteHOTAS.ini -> AbsoluteHOTAS_Custom.ini -> active profile overlay");
        } else if (id == "setup-legacy-workbench") {
            const bool configured =
                g_legacyWorkbenchConfigured.load(std::memory_order_acquire);
            const bool installed =
                g_legacyWorkbenchInstalled.load(std::memory_order_acquire);
            *output = StringValue(!configured ?
                "Disabled by [UI] bEnableWorkbench; manual configuration remains available." :
                installed ? "Available as the supported transition/fallback frontend." :
                            "Configured but renderer hooks were unavailable; gameplay is unaffected.");
        } else if (id == "setup-suite-modules") {
            *output = StringValue(
                "Camera pose: Absolute Head Tracking | mouse centering: AbsoluteZero");
        } else if (id == "diagnostics-compatibility") {
            *output = StringValue(std::format(
                "AbsoluteHOTAS {} | Control ABI 1 | Starfield 1.16.242/1.16.244 exact gates",
                Plugin::VersionString));
        } else if (id == "diagnostics-native-controls") {
            *output = StringValue(
                g_nativeControlsInitialized.load(std::memory_order_acquire) ?
                    "At least one exact-gated native control seam initialized." :
                    "Native control seams unavailable; affected paths remain fail-closed.");
        } else if (id == "diagnostics-controller") {
            *output = StringValue(
                g_controllerStarted.load(std::memory_order_acquire) ?
                    "DirectInput poller running under AbsoluteHOTAS ownership." :
                    "Controller poller not running; configuration and diagnostics remain available.");
        } else if (id == "diagnostics-frontends") {
            *output = StringValue(std::format(
                "Absolute Control connected | embedded workbench {}",
                g_legacyWorkbenchInstalled.load(std::memory_order_acquire) ?
                    "available" : "unavailable or disabled"));
        } else if (id == "diagnostics-coordination") {
            *output = StringValue(
                "Standalone HOTAS seam ownership; optional Absolute Flight Runtime is deferred.");
        } else {
            return Result::NotFound;
        }
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

PageDescriptorV1 Page(std::string_view id, std::string_view name,
                      std::string_view description,
                      const ControlDescriptorV1* controls,
                      std::uint32_t controlCount) noexcept
{
    PageDescriptorV1 page;
    Copy(page.moduleId, kHotasModuleId);
    Copy(page.pageId, id);
    Copy(page.displayName, name);
    Copy(page.description, description);
    page.controlCount = controlCount;
    page.controls = controls;
    page.readValue = &ReadStatus;
    return page;
}

const std::array g_pages{
    Page("hotas-setup", "Setup Overview",
         "Read-only readiness and ownership summary for the standalone HOTAS runtime.",
         g_setupControls.data(), static_cast<std::uint32_t>(g_setupControls.size())),
    Page("hotas-diagnostics", "Plugin & Compatibility",
         "Read-only compatibility, frontend, and suite-ownership diagnostics.",
         g_diagnosticControls.data(),
         static_cast<std::uint32_t>(g_diagnosticControls.size())),
};

bool ValidApi(const ApiV1* api) noexcept
{
    constexpr auto required = offsetof(ApiV1, isInputCaptureActive) +
                              sizeof(((ApiV1*)nullptr)->isInputCaptureActive);
    return api && api->structSize >= required && api->abiVersion == kAbiVersion &&
           api->moduleId && std::string_view(api->moduleId) == kModuleId &&
           api->registerPage && api->unregisterModule && api->requestRefresh &&
           api->registerModule && api->isOpen && api->isInputCaptureActive;
}

using QueryApi = const ApiV1*(__cdecl*)(std::uint32_t) noexcept;

const ApiV1* __cdecl ResolveLoadedHost(const wchar_t* moduleName) noexcept
{
    if (!moduleName) return nullptr;
    const auto module = GetModuleHandleW(moduleName);
    if (!module) return nullptr;
    const auto address = GetProcAddress(module, "AbsoluteControlPanel_QueryApi");
    if (!address) return &g_incompatibleHostApi;
    const auto query = reinterpret_cast<QueryApi>(address);
    const auto* api = query(kAbiVersion);
    return api ? api : &g_incompatibleHostApi;
}

bool ValidIdentifier(std::string_view value) noexcept
{
    if (value.empty()) return false;
    return std::ranges::all_of(value, [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
               ch == '.' || ch == '_' || ch == '-';
    });
}

Result RegistrationFailure(Result result) noexcept
{
    if (result == Result::NotReady) return result;
    g_terminalRejection.store(true, std::memory_order_release);
    return Result::Rejected;
}
} // namespace

namespace AbsoluteControlSubscriber {

void SetRuntimeStatus(const RuntimeStatus& status) noexcept
{
    g_throttleHookInstalled.store(status.throttleHookInstalled, std::memory_order_release);
    g_nativeControlsInitialized.store(
        status.nativeControlsInitialized, std::memory_order_release);
    g_controllerStarted.store(status.controllerStarted, std::memory_order_release);
    g_legacyWorkbenchConfigured.store(
        status.legacyWorkbenchConfigured, std::memory_order_release);
    g_legacyWorkbenchInstalled.store(
        status.legacyWorkbenchInstalled, std::memory_order_release);
}

AbsoluteControlPanelApi::Result RegisterDiscoveredHost() noexcept
{
    return Testing::RegisterWithResolver(&ResolveLoadedHost);
}

bool IsHosted() noexcept
{
    return g_registered.load(std::memory_order_acquire);
}

namespace Testing {

AbsoluteControlPanelApi::Result ValidateDescriptors(
    const PageDescriptorV1* pages, std::size_t pageCount) noexcept
{
    if (!pages || pageCount == 0) return Result::InvalidArgument;
    try {
        std::unordered_set<std::string_view> pageIds;
        std::unordered_set<std::string_view> controlIds;
        for (std::size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
            const auto& page = pages[pageIndex];
            if (page.structSize < sizeof(PageDescriptorV1) ||
                !Terminated(page.moduleId) || !Terminated(page.pageId) ||
                !Terminated(page.displayName) || !Terminated(page.description) ||
                std::string_view(page.moduleId) != kHotasModuleId ||
                !ValidIdentifier(page.pageId) || page.displayName[0] == '\0' ||
                !page.controls || page.controlCount == 0 || !page.readValue ||
                page.writeDraft || page.invokeAction || page.apply || page.cancel) {
                return Result::InvalidArgument;
            }
            if (!pageIds.insert(page.pageId).second) return Result::Duplicate;

            for (std::uint32_t controlIndex = 0;
                 controlIndex < page.controlCount; ++controlIndex) {
                const auto& control = page.controls[controlIndex];
                if (control.structSize < sizeof(ControlDescriptorV1) ||
                    !Terminated(control.controlId) || !Terminated(control.label) ||
                    !Terminated(control.description) ||
                    !ValidIdentifier(control.controlId) || control.label[0] == '\0' ||
                    control.kind != ControlKind::InputBinding ||
                    control.flags != kControlReadOnly) {
                    return Result::InvalidArgument;
                }
                // This provider uses one shared callback/parser, so it adopts the
                // generated-SDK's stricter module-wide uniqueness rule.
                if (!controlIds.insert(control.controlId).second) {
                    return Result::Duplicate;
                }
            }
        }
        return Result::Ok;
    } catch (...) {
        return Result::Rejected;
    }
}

AbsoluteControlPanelApi::Result RegisterWithResolver(
    ResolveLoadedHostCallback resolver) noexcept
{
    if (!resolver) return Result::InvalidArgument;
    if (g_registered.load(std::memory_order_acquire)) return Result::Ok;
    if (g_terminalRejection.load(std::memory_order_acquire)) return Result::Rejected;

    try {
        std::scoped_lock lock(g_registrationMutex);
        if (g_registered.load(std::memory_order_relaxed)) return Result::Ok;
        if (g_terminalRejection.load(std::memory_order_relaxed)) return Result::Rejected;

        const auto descriptorResult = ValidateDescriptors(g_pages.data(), g_pages.size());
        if (descriptorResult != Result::Ok) {
            g_terminalRejection.store(true, std::memory_order_release);
            return Result::Rejected;
        }

        for (const wchar_t* moduleName : {
                 L"AbsoluteControlPanel.dll",
                 L"AbsoluteControlPanelResearchDev.dll",
             }) {
            const auto* api = resolver(moduleName);
            if (!api) continue;
            if (!ValidApi(api)) {
                g_terminalRejection.store(true, std::memory_order_release);
                return Result::Rejected;
            }

            const auto moduleResult = api->registerModule(&g_module);
            if (moduleResult != Result::Ok && moduleResult != Result::Duplicate) {
                return RegistrationFailure(moduleResult);
            }
            const bool ownsModuleRegistration = moduleResult == Result::Ok;
            for (const auto& page : g_pages) {
                const auto pageResult = api->registerPage(&page);
                if (pageResult != Result::Ok && pageResult != Result::Duplicate) {
                    if (ownsModuleRegistration) {
                        (void)api->unregisterModule(kHotasModuleId.data());
                    }
                    return RegistrationFailure(pageResult);
                }
            }

            g_hostApi.store(api, std::memory_order_release);
            g_registered.store(true, std::memory_order_release);
            return Result::Ok;
        }
        return Result::NotFound;
    } catch (...) {
        g_terminalRejection.store(true, std::memory_order_release);
        return Result::Rejected;
    }
}

const ModuleDescriptorV1& Module() noexcept
{
    return g_module;
}

const PageDescriptorV1* Pages(std::size_t& pageCount) noexcept
{
    pageCount = g_pages.size();
    return g_pages.data();
}

void ForceReadException(bool enabled) noexcept
{
    g_forceReadException.store(enabled, std::memory_order_release);
}

void Reset() noexcept
{
    std::scoped_lock lock(g_registrationMutex);
    g_hostApi.store(nullptr, std::memory_order_release);
    g_registered.store(false, std::memory_order_release);
    g_terminalRejection.store(false, std::memory_order_release);
    g_forceReadException.store(false, std::memory_order_release);
    SetRuntimeStatus({});
}

} // namespace Testing
} // namespace AbsoluteControlSubscriber

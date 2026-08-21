#include "PCH.h"

#include "AbsoluteControlSettings.h"

#include "BindingRef.h"
#include "RuntimePaths.h"
#include "ThrottleController.h"
#include "UIHook.h"
#include "WizardConfigInternal.h"

#include <SimpleIni.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace {

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

std::uint64_t HashFile(const std::filesystem::path& path,
                       std::uint64_t hash) noexcept
{
    try {
        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open()) {
            hash ^= 0xFFU;
            return hash * kFnvPrime;
        }
        char buffer[4096];
        while (stream.read(buffer, sizeof(buffer)) || stream.gcount() > 0) {
            const auto count = static_cast<std::size_t>(stream.gcount());
            for (std::size_t index = 0; index < count; ++index) {
                hash ^= static_cast<unsigned char>(buffer[index]);
                hash *= kFnvPrime;
            }
        }
        return hash;
    } catch (...) {
        return 0;
    }
}

std::uint64_t SourceFingerprint() noexcept
{
    auto hash = HashFile(RuntimePaths::IniPath(), kFnvOffset);
    if (hash == 0) return 0;
    hash = HashFile(RuntimePaths::CustomIniPath(), hash);
    if (hash == 0) return 0;
    const auto& editTarget = WizardConfig::GetEditProfile();
    for (const unsigned char ch : editTarget) {
        hash ^= ch;
        hash *= kFnvPrime;
    }
    if (!editTarget.empty()) {
        const auto profile = WizardConfig::Detail::FindProfilePath(editTarget);
        if (profile.empty()) return 0;
        hash = HashFile(profile, hash);
    }
    return hash;
}

bool LoadLayered(CSimpleIniA& ini, std::string& error)
{
    ini.SetUnicode(false);
    if (ini.LoadFile(RuntimePaths::IniPath().string().c_str()) != SI_OK) {
        error = "The shipped AbsoluteHOTAS configuration could not be read.";
        return false;
    }

    const auto customPath = RuntimePaths::CustomIniPath();
    std::error_code ec;
    if (!std::filesystem::exists(customPath, ec)) return true;

    CSimpleIniA custom;
    custom.SetUnicode(false);
    if (custom.LoadFile(customPath.string().c_str()) != SI_OK) {
        error = "The custom AbsoluteHOTAS configuration could not be read.";
        return false;
    }
    const long version = custom.GetLongValue("Meta", "iConfigVersion", -1);
    if (version < 1 || version > WizardConfig::kConfigVersion) {
        error = "The custom AbsoluteHOTAS configuration version is unsupported.";
        return false;
    }
    if (ini.LoadFile(customPath.string().c_str()) != SI_OK) {
        error = "The custom AbsoluteHOTAS configuration could not be layered.";
        return false;
    }
    return true;
}

AbsoluteControlSettings::ScalarState Decode(const CSimpleIniA& ini)
{
    using namespace AbsoluteControlSettings;
    auto state = DefaultState();
    for (const auto& definition : Definitions()) {
        const auto* section = definition.section.data();
        const auto* key = definition.key.data();
        switch (definition.storage) {
        case StorageFormat::Boolean:
            SetBoolean(state, definition.field,
                ini.GetBoolValue(section, key, definition.defaultValue != 0.0));
            break;
        case StorageFormat::Integer:
            SetInteger(state, definition.field, std::clamp<std::int64_t>(
                ini.GetLongValue(section, key,
                    static_cast<long>(definition.defaultValue)),
                static_cast<std::int64_t>(definition.minimum),
                static_cast<std::int64_t>(definition.maximum)));
            break;
        case StorageFormat::Float:
            SetFloat(state, definition.field, std::clamp(
                ini.GetDoubleValue(section, key, definition.defaultValue),
                definition.minimum, definition.maximum));
            break;
        case StorageFormat::PilotContextMode: {
            const auto* mode = ini.GetValue(section, key, "InjectionOnly");
            SetInteger(state, definition.field,
                _stricmp(mode, "Full") == 0 ? 2 :
                (_stricmp(mode, "InjectionOnly") == 0 ? 1 : 0));
            break;
        }
        case StorageFormat::PilotSignal:
            SetBoolean(state, definition.field,
                _stricmp(ini.GetValue(section, key, "Auto"), "Auto") == 0);
            break;
        case StorageFormat::HoldForBoostAlias:
            SetBoolean(state, definition.field,
                ini.GetBoolValue("DualStick", "bHoldForBoost",
                    ini.GetBoolValue("Injection", "bHoldForBoost",
                        definition.defaultValue != 0.0)));
            break;
        }
    }
    const auto engage = GetFloat(state, ScalarField::MenuEngageThreshold);
    const auto release = GetFloat(state, ScalarField::MenuReleaseThreshold);
    if (release > engage - 0.05) {
        SetFloat(state, ScalarField::MenuReleaseThreshold,
            (std::max)(0.05, engage - 0.05));
    }
    return state;
}

void Encode(const AbsoluteControlSettings::ScalarState& state, CSimpleIniA& ini)
{
    using namespace AbsoluteControlSettings;
    for (const auto& definition : Definitions()) {
        const auto* section = definition.section.data();
        const auto* key = definition.key.data();
        switch (definition.storage) {
        case StorageFormat::Boolean:
            ini.SetBoolValue(section, key,
                GetBoolean(state, definition.field));
            break;
        case StorageFormat::Integer:
            ini.SetLongValue(section, key,
                static_cast<long>(GetInteger(state, definition.field)));
            break;
        case StorageFormat::Float: {
            char value[32]{};
            std::snprintf(value, sizeof(value), "%.6g",
                GetFloat(state, definition.field));
            ini.SetValue(section, key, value);
            break;
        }
        case StorageFormat::PilotContextMode: {
            const auto mode = GetInteger(state, definition.field);
            ini.SetValue(section, key,
                mode == 2 ? "Full" : (mode == 1 ? "InjectionOnly" : "Off"));
            break;
        }
        case StorageFormat::PilotSignal:
            ini.SetValue(section, key,
                GetBoolean(state, definition.field) ? "Auto" : "Manual");
            break;
        case StorageFormat::HoldForBoostAlias:
            ini.SetBoolValue("DualStick", "bHoldForBoost",
                GetBoolean(state, definition.field));
            break;
        }
    }
    ini.SetLongValue("Meta", "iConfigVersion", WizardConfig::kConfigVersion);
}

HotasBindingCatalog::BindingState DecodeBindings(const CSimpleIniA& ini)
{
    HotasBindingCatalog::BindingState bindings{};
    for (std::size_t index = 0; index < HotasBindingCatalog::kTargets.size();
         ++index) {
        const auto& target = HotasBindingCatalog::kTargets[index];
        const auto* raw = ini.GetValue(target.iniSection.data(),
                                       target.iniKey.data(), "-1");
        bindings[index] = FormatBindingRef(
            ParseBindingRef(raw, -1),
            target.captureKind == HotasBindingCatalog::CaptureKind::Axis);
    }
    return bindings;
}

std::string NormalizeMethodToken(const char* raw)
{
    std::string token = raw ? raw : "";
    const auto first = std::find_if_not(token.begin(), token.end(),
        [](unsigned char value) { return std::isspace(value) != 0; });
    const auto last = std::find_if_not(token.rbegin(), token.rend(),
        [](unsigned char value) { return std::isspace(value) != 0; }).base();
    if (first >= last) return {};
    token = std::string(first, last);
    std::ranges::transform(token, token.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return token;
}

AbsoluteControlSettings::ShipRouteState DecodeShipRoutes(
    const CSimpleIniA& ini)
{
    auto routes = AbsoluteControlSettings::DefaultShipRoutes();
    for (std::size_t index = 0; index < routes.size(); ++index) {
        const auto& action = kShipActionCatalog[index];
        const auto token = NormalizeMethodToken(ini.GetValue(
            "ShipControlMethods", action.actionId.data(), ""));
        routes[index] = ResolveShipControlMethod(action, token).method;
    }
    return routes;
}

AbsoluteControlDevices::CalibrationMap DecodeCalibration(const CSimpleIniA& ini)
{
    AbsoluteControlDevices::CalibrationMap calibration;
    CSimpleIniA::TNamesDepend keys;
    ini.GetAllKeys("Calibration", keys);
    for (const auto& entry : keys) {
        int deviceIndex = -1;
        int usage = -1;
        long minimum = 0;
        long maximum = 0;
        const auto* value = ini.GetValue("Calibration", entry.pItem, "");
        if (entry.pItem &&
            std::sscanf(entry.pItem, "iCalib_%d_0x%x", &deviceIndex, &usage) == 2 &&
            std::sscanf(value, "%ld,%ld", &minimum, &maximum) == 2 &&
            deviceIndex >= 0 && usage >= 0x30 && usage <= 0x37 &&
            maximum > minimum) {
            calibration[(deviceIndex << 8) | usage] = {minimum, maximum};
        }
    }
    return calibration;
}

void EncodeCalibration(const AbsoluteControlDevices::CalibrationMap& calibration,
                       CSimpleIniA& ini)
{
    ini.Delete("Calibration", nullptr);
    for (const auto& [packed, range] : calibration) {
        const int deviceIndex = packed >> 8;
        const int usage = packed & 0xFF;
        if (deviceIndex < 0 || usage < 0x30 || usage > 0x37 ||
            range.second <= range.first) continue;
        char key[64]{};
        char value[64]{};
        std::snprintf(key, sizeof(key), "iCalib_%d_0x%02X", deviceIndex, usage);
        std::snprintf(value, sizeof(value), "%ld,%ld",
                      range.first, range.second);
        ini.SetValue("Calibration", key, value);
    }
}

void EncodeBindings(const HotasBindingCatalog::BindingState& bindings,
                    CSimpleIniA& ini)
{
    for (std::size_t index = 0; index < HotasBindingCatalog::kTargets.size();
         ++index) {
        const auto& target = HotasBindingCatalog::kTargets[index];
        const bool unbound = bindings[index].empty() ||
                             bindings[index] == "(unbound)" ||
                             bindings[index] == "-1";
        ini.SetValue(target.iniSection.data(), target.iniKey.data(),
            unbound && target.captureKind == HotasBindingCatalog::CaptureKind::Axis
                ? ""
                : unbound ? "-1" : bindings[index].c_str());
    }
}

void EncodeShipRoutes(const AbsoluteControlSettings::ShipRouteState& routes,
                      CSimpleIniA& ini)
{
    // The absence of a key means "use the catalog recommendation". Rebuild
    // the managed section so stale or unsupported route tokens cannot survive.
    ini.Delete("ShipControlMethods", nullptr);
    for (std::size_t index = 0; index < routes.size(); ++index) {
        const auto& action = kShipActionCatalog[index];
        const auto method = AllowsShipControlMethod(action, routes[index])
            ? routes[index] : action.recommendedMethod;
        if (method != action.recommendedMethod) {
            ini.SetValue("ShipControlMethods", action.actionId.data(),
                         ShipControlMethodToken(method).data());
        }
    }
}

void DecodeWizardSlice(const WizardState& source,
                       AbsoluteControlSettings::ScalarState& state,
                       HotasBindingCatalog::BindingState& bindings)
{
    CSimpleIniA ini;
    ini.SetUnicode(false);
    WizardConfig::Detail::SerializeUserOwnedState(source, ini);
    state = Decode(ini);
    bindings = DecodeBindings(ini);
}

void ApplyWizardSlice(const AbsoluteControlSettings::ScalarState& state,
                      const HotasBindingCatalog::BindingState& bindings,
                      WizardState& destination)
{
    CSimpleIniA ini;
    ini.SetUnicode(false);
    WizardConfig::Detail::SerializeUserOwnedState(destination, ini);
    Encode(state, ini);
    EncodeBindings(bindings, ini);
    WizardConfig::Detail::ApplyProfileScalars(ini, destination);
}

bool SaveShipRoutesOnly(const AbsoluteControlSettings::ShipRouteState& routes,
                        std::string& error)
{
    CSimpleIniA custom;
    custom.SetUnicode(false);
    const auto path = RuntimePaths::CustomIniPath();
    std::error_code ec;
    if (std::filesystem::exists(path, ec) &&
        custom.LoadFile(path.string().c_str()) != SI_OK) {
        error = "The custom AbsoluteHOTAS configuration could not be read.";
        return false;
    }
    EncodeShipRoutes(routes, custom);
    custom.SetLongValue("Meta", "iConfigVersion", WizardConfig::kConfigVersion);
    if (!WizardConfig::Detail::SaveIniAtomically(custom, path)) {
        error = "The HOTAS output methods could not be written.";
        return false;
    }
    return true;
}

} // namespace

namespace AbsoluteControlSettings {

Revision CurrentRevision() noexcept
{
    return {
        .runtimeGeneration = ThrottleController::ConfigGeneration(),
        .sourceFingerprint = SourceFingerprint(),
    };
}

bool CanEdit() noexcept
{
    return !UIHook::IsUIOpen();
}

bool Load(ScalarState& state, Revision& revision, std::string& error) noexcept
{
    try {
        error.clear();
        CSimpleIniA ini;
        if (!LoadLayered(ini, error)) return false;
        state = Decode(ini);
        revision = CurrentRevision();
        if (revision.sourceFingerprint == 0) {
            error = "AbsoluteHOTAS could not fingerprint its configuration files.";
            return false;
        }
        return true;
    } catch (...) {
        error = "AbsoluteHOTAS could not prepare a Control editing session.";
        return false;
    }
}

bool LoadWithBindings(ScalarState& state,
                      HotasBindingCatalog::BindingState& bindings,
                      Revision& revision, std::string& error) noexcept
{
    ShipRouteState routes;
    return LoadWithBindingsAndRoutes(
        state, bindings, routes, revision, error);
}

bool LoadWithBindingsAndRoutes(
    ScalarState& state, HotasBindingCatalog::BindingState& bindings,
    ShipRouteState& routes, Revision& revision, std::string& error) noexcept
{
    try {
        error.clear();
        CSimpleIniA ini;
        if (!LoadLayered(ini, error)) return false;
        state = Decode(ini);
        bindings = DecodeBindings(ini);
        routes = DecodeShipRoutes(ini);
        revision = CurrentRevision();
        if (revision.sourceFingerprint == 0) {
            error = "AbsoluteHOTAS could not fingerprint its configuration files.";
            return false;
        }
        return true;
    } catch (...) {
        error = "AbsoluteHOTAS could not prepare a Control binding session.";
        return false;
    }
}

bool LoadEditTargetWithBindingsAndRoutes(
    ScalarState& state, HotasBindingCatalog::BindingState& bindings,
    ShipRouteState& routes, std::string& editTarget,
    Revision& revision, std::string& error) noexcept
{
    try {
        error.clear();
        WizardConfig::LoadCurrentBindings();
        editTarget = WizardConfig::GetEditProfile();
        DecodeWizardSlice(WizardConfig::GetState(), state, bindings);
        CSimpleIniA main;
        if (!LoadLayered(main, error)) return false;
        routes = DecodeShipRoutes(main);
        revision = CurrentRevision();
        if (revision.sourceFingerprint == 0) {
            error = "AbsoluteHOTAS could not fingerprint the selected edit profile.";
            return false;
        }
        return true;
    } catch (...) {
        error = "AbsoluteHOTAS could not prepare the selected profile draft.";
        return false;
    }
}

bool LoadDeviceState(
    HotasBindingCatalog::BindingState& bindings,
    AbsoluteControlDevices::CalibrationMap& calibration,
    Revision& revision, std::string& error) noexcept
{
    try {
        error.clear();
        CSimpleIniA ini;
        if (!LoadLayered(ini, error)) return false;
        bindings = DecodeBindings(ini);
        calibration = DecodeCalibration(ini);
        revision = CurrentRevision();
        if (revision.sourceFingerprint == 0) {
            error = "AbsoluteHOTAS could not fingerprint its configuration files.";
            return false;
        }
        return true;
    } catch (...) {
        error = "AbsoluteHOTAS could not prepare the device configuration.";
        return false;
    }
}

bool Apply(const ScalarState& state, const Revision& expected,
           ScalarState& readBack, Revision& revision,
           std::string& error) noexcept
{
    try {
        error.clear();
        if (!Validate(state, error)) return false;
        if (!CanEdit()) {
            error = "Close the embedded HOTAS workbench before editing in Absolute Control.";
            return false;
        }
        if (CurrentRevision().sourceFingerprint != expected.sourceFingerprint) {
            error = "The HOTAS configuration changed after this draft was opened.";
            return false;
        }

        CSimpleIniA custom;
        custom.SetUnicode(false);
        const auto customPath = RuntimePaths::CustomIniPath();
        std::error_code ec;
        if (std::filesystem::exists(customPath, ec)) {
            if (custom.LoadFile(customPath.string().c_str()) != SI_OK) {
                error = "The custom AbsoluteHOTAS configuration could not be read.";
                return false;
            }
            const long version =
                custom.GetLongValue("Meta", "iConfigVersion", -1);
            if (version < 1 || version > WizardConfig::kConfigVersion) {
                error = "The custom AbsoluteHOTAS configuration version is unsupported.";
                return false;
            }
        }

        Encode(state, custom);
        if (!WizardConfig::Detail::SaveIniAtomically(custom, customPath)) {
            error = "The custom AbsoluteHOTAS configuration could not be written.";
            return false;
        }

        CSimpleIniA layered;
        if (!LoadLayered(layered, error)) return false;
        readBack = Decode(layered);
        if (!AbsoluteControlSettings::Equivalent(state, readBack)) {
            error = "AbsoluteHOTAS could not verify the saved settings.";
            return false;
        }

        ThrottleController::ReloadConfig();
        revision = CurrentRevision();
        return true;
    } catch (...) {
        error = "AbsoluteHOTAS could not apply the Control draft.";
        return false;
    }
}

bool ApplyWithBindings(
    const ScalarState& state,
    const HotasBindingCatalog::BindingState& bindings,
    const Revision& expected, ScalarState& readBack,
    HotasBindingCatalog::BindingState& bindingReadBack,
    Revision& revision, std::string& error) noexcept
{
    ScalarState currentState;
    HotasBindingCatalog::BindingState currentBindings;
    ShipRouteState routes;
    Revision currentRevision;
    if (!LoadWithBindingsAndRoutes(currentState, currentBindings, routes,
                                   currentRevision, error)) {
        return false;
    }
    ShipRouteState routeReadBack;
    return ApplyWithBindingsAndRoutes(
        state, bindings, routes, expected, readBack, bindingReadBack,
        routeReadBack, revision, error);
}

bool ApplyWithBindingsAndRoutes(
    const ScalarState& state,
    const HotasBindingCatalog::BindingState& bindings,
    const ShipRouteState& routes, const Revision& expected,
    ScalarState& readBack,
    HotasBindingCatalog::BindingState& bindingReadBack,
    ShipRouteState& routeReadBack, Revision& revision,
    std::string& error) noexcept
{
    try {
        error.clear();
        if (!Validate(state, error)) return false;
        if (!CanEdit()) {
            error = "Close the embedded HOTAS workbench before editing in Absolute Control.";
            return false;
        }
        if (CurrentRevision().sourceFingerprint != expected.sourceFingerprint) {
            error = "The HOTAS configuration changed after this draft was opened.";
            return false;
        }

        CSimpleIniA custom;
        custom.SetUnicode(false);
        const auto customPath = RuntimePaths::CustomIniPath();
        std::error_code ec;
        if (std::filesystem::exists(customPath, ec)) {
            if (custom.LoadFile(customPath.string().c_str()) != SI_OK) {
                error = "The custom AbsoluteHOTAS configuration could not be read.";
                return false;
            }
            const long version =
                custom.GetLongValue("Meta", "iConfigVersion", -1);
            if (version < 1 || version > WizardConfig::kConfigVersion) {
                error = "The custom AbsoluteHOTAS configuration version is unsupported.";
                return false;
            }
        }

        Encode(state, custom);
        EncodeBindings(bindings, custom);
        EncodeShipRoutes(routes, custom);
        if (!WizardConfig::Detail::SaveIniAtomically(custom, customPath)) {
            error = "The custom AbsoluteHOTAS configuration could not be written.";
            return false;
        }

        CSimpleIniA layered;
        if (!LoadLayered(layered, error)) return false;
        readBack = Decode(layered);
        bindingReadBack = DecodeBindings(layered);
        routeReadBack = DecodeShipRoutes(layered);
        if (!AbsoluteControlSettings::Equivalent(state, readBack) ||
            bindings != bindingReadBack || routes != routeReadBack) {
            error = "AbsoluteHOTAS could not verify the saved settings, bindings, and output methods.";
            return false;
        }

        ThrottleController::ReloadConfig();
        revision = CurrentRevision();
        return true;
    } catch (...) {
        error = "AbsoluteHOTAS could not apply the Control binding draft.";
        return false;
    }
}

bool ApplyEditTargetWithBindingsAndRoutes(
    const ScalarState& state,
    const HotasBindingCatalog::BindingState& bindings,
    const ShipRouteState& routes, std::string_view expectedEditTarget,
    const Revision& expected, ScalarState& readBack,
    HotasBindingCatalog::BindingState& bindingReadBack,
    ShipRouteState& routeReadBack, Revision& revision,
    std::string& error) noexcept
{
    try {
        error.clear();
        if (!Validate(state, error)) return false;
        if (!CanEdit()) {
            error = "Close the embedded HOTAS workbench before editing in Absolute Control.";
            return false;
        }
        if (WizardConfig::GetEditProfile() != expectedEditTarget) {
            error = "The selected HOTAS edit profile changed after this draft opened.";
            return false;
        }
        if (CurrentRevision().sourceFingerprint != expected.sourceFingerprint) {
            error = "The selected HOTAS profile changed after this draft opened.";
            return false;
        }

        auto candidate = WizardConfig::GetState();
        ApplyWizardSlice(state, bindings, candidate);
        if (expectedEditTarget.empty()) {
            if (!ApplyWithBindingsAndRoutes(state, bindings, routes, expected,
                    readBack, bindingReadBack, routeReadBack, revision, error)) {
                return false;
            }
            WizardConfig::GetState() = candidate;
            WizardConfig::Detail::BaseState() = candidate;
            WizardConfig::Detail::MarkStateSaved();
            return true;
        }

        const auto original = WizardConfig::GetState();
        WizardConfig::GetState() = candidate;
        if (!WizardConfig::SaveActiveProfile(error)) {
            WizardConfig::GetState() = original;
            return false;
        }
        if (!SaveShipRoutesOnly(routes, error)) return false;
        ThrottleController::ReloadConfig();
        DecodeWizardSlice(WizardConfig::GetState(), readBack, bindingReadBack);
        routeReadBack = routes;
        if (!Equivalent(state, readBack) || bindings != bindingReadBack) {
            error = "AbsoluteHOTAS could not verify the selected profile draft.";
            return false;
        }
        revision = CurrentRevision();
        return true;
    } catch (...) {
        error = "AbsoluteHOTAS could not apply the selected profile draft.";
        return false;
    }
}

bool ApplyDeviceState(
    const HotasBindingCatalog::BindingState& bindings,
    const AbsoluteControlDevices::CalibrationMap& calibration,
    const Revision& expected,
    HotasBindingCatalog::BindingState& bindingReadBack,
    AbsoluteControlDevices::CalibrationMap& calibrationReadBack,
    Revision& revision, std::string& error) noexcept
{
    try {
        error.clear();
        if (!CanEdit()) {
            error = "Close the embedded HOTAS workbench before editing in Absolute Control.";
            return false;
        }
        if (CurrentRevision().sourceFingerprint != expected.sourceFingerprint) {
            error = "The HOTAS configuration changed after this device action opened.";
            return false;
        }

        CSimpleIniA custom;
        custom.SetUnicode(false);
        const auto customPath = RuntimePaths::CustomIniPath();
        std::error_code ec;
        if (std::filesystem::exists(customPath, ec)) {
            if (custom.LoadFile(customPath.string().c_str()) != SI_OK) {
                error = "The custom AbsoluteHOTAS configuration could not be read.";
                return false;
            }
            const long version = custom.GetLongValue("Meta", "iConfigVersion", -1);
            if (version < 1 || version > WizardConfig::kConfigVersion) {
                error = "The custom AbsoluteHOTAS configuration version is unsupported.";
                return false;
            }
        }

        EncodeBindings(bindings, custom);
        EncodeCalibration(calibration, custom);
        custom.SetLongValue("Meta", "iConfigVersion", WizardConfig::kConfigVersion);
        if (!WizardConfig::Detail::SaveIniAtomically(custom, customPath)) {
            error = "The custom AbsoluteHOTAS configuration could not be written.";
            return false;
        }

        CSimpleIniA layered;
        if (!LoadLayered(layered, error)) return false;
        bindingReadBack = DecodeBindings(layered);
        calibrationReadBack = DecodeCalibration(layered);
        if (bindings != bindingReadBack || calibration != calibrationReadBack) {
            error = "AbsoluteHOTAS could not verify the saved device configuration.";
            return false;
        }

        ThrottleController::ReloadConfig();
        revision = CurrentRevision();
        return true;
    } catch (...) {
        error = "AbsoluteHOTAS could not apply the device configuration.";
        return false;
    }
}

} // namespace AbsoluteControlSettings

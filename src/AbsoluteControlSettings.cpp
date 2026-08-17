#include "PCH.h"

#include "AbsoluteControlSettings.h"

#include "RuntimePaths.h"
#include "ThrottleController.h"
#include "UIHook.h"
#include "WizardConfigInternal.h"

#include <SimpleIni.h>

#include <algorithm>
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
    return HashFile(RuntimePaths::CustomIniPath(), hash);
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
    AbsoluteControlSettings::ScalarState state;
    state.flightControlsEnabled =
        ini.GetBoolValue("Injection", "bEnableInjection", true);
    state.pitchInverted = ini.GetBoolValue("Hardware", "bInvertPitch", false);
    state.pitchSensitivity = std::clamp(
        ini.GetDoubleValue("Hardware", "fPitchSensitivity", 1.0), 0.1, 3.0);

    const std::string_view mode =
        ini.GetValue("Gate", "PilotGateMode", "InjectionOnly");
    state.pilotContextMode = _stricmp(mode.data(), "Full") == 0 ? 2 :
        (_stricmp(mode.data(), "InjectionOnly") == 0 ? 1 : 0);
    state.automaticPilotDetection =
        _stricmp(ini.GetValue("Gate", "PilotSignal", "Auto"), "Auto") == 0;
    state.pilotLatchMilliseconds = std::clamp(
        static_cast<int>(ini.GetLongValue(
            "Gate", "iPilotLatchMilliseconds", 5000)), 500, 30000);
    return state;
}

bool Equivalent(const AbsoluteControlSettings::ScalarState& left,
                const AbsoluteControlSettings::ScalarState& right) noexcept
{
    return left.flightControlsEnabled == right.flightControlsEnabled &&
           left.pitchInverted == right.pitchInverted &&
           std::abs(left.pitchSensitivity - right.pitchSensitivity) <= 0.005 &&
           left.pilotContextMode == right.pilotContextMode &&
           left.automaticPilotDetection == right.automaticPilotDetection &&
           left.pilotLatchMilliseconds == right.pilotLatchMilliseconds;
}

void Encode(const AbsoluteControlSettings::ScalarState& state, CSimpleIniA& ini)
{
    ini.SetBoolValue(
        "Injection", "bEnableInjection", state.flightControlsEnabled);
    ini.SetBoolValue("Hardware", "bInvertPitch", state.pitchInverted);
    char sensitivity[32]{};
    std::snprintf(sensitivity, sizeof(sensitivity), "%.2f",
        std::clamp(state.pitchSensitivity, 0.1, 3.0));
    ini.SetValue("Hardware", "fPitchSensitivity", sensitivity);

    const char* gateMode = state.pilotContextMode == 2 ? "Full" :
        (state.pilotContextMode == 1 ? "InjectionOnly" : "Off");
    ini.SetValue("Gate", "PilotGateMode", gateMode);
    ini.SetValue("Gate", "PilotSignal",
        state.automaticPilotDetection ? "Auto" : "Manual");
    ini.SetLongValue("Gate", "iPilotLatchMilliseconds",
        std::clamp(state.pilotLatchMilliseconds, 500, 30000));
    ini.SetLongValue("Meta", "iConfigVersion", WizardConfig::kConfigVersion);
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

bool Apply(const ScalarState& state, const Revision& expected,
           ScalarState& readBack, Revision& revision,
           std::string& error) noexcept
{
    try {
        error.clear();
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
        if (!Equivalent(state, readBack)) {
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

} // namespace AbsoluteControlSettings

#pragma once

#include "WizardConfig.h"

#include <SimpleIni.h>

#include <filesystem>
#include <string>
#include <vector>

namespace WizardConfig {

inline constexpr int kConfigVersion = 1;

namespace Detail {

struct ProfileRecord {
    std::filesystem::path path;
    std::string name;
    int sequence = 0;
};

void Log(const std::string& message);

WizardState& State();
WizardState& BaseState();
std::string& EditProfile();
std::string& SavedStateSignature();
void MarkStateSaved();

std::vector<ProfileRecord> ReadProfileRecords();
std::filesystem::path FindProfilePath(const std::string& name);
ProfileRecord AllocateProfileRecord(const std::string& name);

std::string SanitizeMacroName(const std::string& name);
void LoadMacroRows(WizardState& state,
                   const std::filesystem::path* profilePath = nullptr);
void ApplyProfileScalars(const CSimpleIniA& ini, WizardState& state);
void LoadEffectiveCollections(const std::filesystem::path& profilePath,
                              WizardState& state);
void SerializeUserOwnedState(const WizardState& state, CSimpleIniA& ini);
void SerializeMacros(const WizardState& state, CSimpleIniA& ini);
void ReplaceHotasOwnedState(CSimpleIniA& destination,
                            const CSimpleIniA& incoming);
std::string StateSignature(const WizardState& state);

bool SaveIniAtomically(CSimpleIniA& ini, const std::filesystem::path& path);
bool SaveBindingsToINI(std::string& err);
bool SaveProfileOverlay(const std::string& name, std::string& err);

}  // namespace Detail
}  // namespace WizardConfig

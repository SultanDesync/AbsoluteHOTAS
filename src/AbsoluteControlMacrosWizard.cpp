#include "PCH.h"

#include "AbsoluteControlMacros.h"

namespace {

class WizardMacroRepository final : public AbsoluteControlMacros::Repository {
public:
    bool Load(WizardState& state, std::string& error) override
    {
        error.clear();
        WizardConfig::LoadCurrentBindings();
        state = WizardConfig::GetState();
        return state.loaded;
    }

    bool Save(const WizardState& state, std::string& error) override
    {
        WizardConfig::GetState() = state;
        return WizardConfig::SaveActiveProfile(error);
    }
};

WizardMacroRepository g_repository;

} // namespace

namespace AbsoluteControlMacros {

Repository& WizardRepository() noexcept { return g_repository; }

} // namespace AbsoluteControlMacros

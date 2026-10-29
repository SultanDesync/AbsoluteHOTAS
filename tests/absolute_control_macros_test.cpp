#include "AbsoluteControlMacros.h"

#include "TestCheck.h"
#include <string>

namespace {

class MemoryRepository final : public AbsoluteControlMacros::Repository {
public:
    bool Load(WizardState& output, std::string& error) override
    {
        ++loads;
        output = stored;
        error.clear();
        return !failLoad;
    }

    bool Save(const WizardState& input, std::string& error) override
    {
        ++saves;
        if (failSave) {
            error = "save failed";
            return false;
        }
        stored = input;
        error.clear();
        return true;
    }

    WizardState stored{};
    int loads{};
    int saves{};
    bool failLoad{};
    bool failSave{};
};

} // namespace

int main()
{
    MemoryRepository repository;
    repository.stored.loaded = true;
    repository.stored.macros = {
        {"First", "Stick@4", false, {{{"NextSystem", "key:0x1E"}, false, 2, 25},
                                      {{"PreviousSystem"}, true, 400, 5}}},
        {"Incomplete", "(unbound)", true, {}},
        {"First", "Stick@5", false, {}},
    };
    repository.stored.customBindings = {
        {"Stick@8", "key:0x11"},
        {"(unbound)", "none"},
    };

    AbsoluteControlMacros::Session session(repository);
    std::string error;
    REQUIRE(session.Open(error));
    REQUIRE(repository.loads == 1);
    REQUIRE(!session.Dirty());
    REQUIRE(session.MacroRecords().size() == 3);
    REQUIRE(session.MacroRecords()[0].label == "First (0)");
    REQUIRE(session.MacroRecords()[2].label == "First (1)");
    REQUIRE(session.StepRecords().size() == 2);
    REQUIRE(session.TargetRecords().size() == 2);

    // Ordered steps and chord source tokens survive edits without resolution.
    const auto secondStep = session.StepRecords()[1].recordId;
    REQUIRE(session.SelectStep(secondStep));
    REQUIRE(session.MoveStep(-1));
    REQUIRE(session.SetStepGap(99));
    REQUIRE(session.AddTarget(0));
    REQUIRE(session.SelectedStep()->targets.back() == "FireBoosters");

    const auto incomplete = session.MacroRecords()[1].recordId;
    REQUIRE(session.SelectMacro(incomplete));
    REQUIRE(session.SelectedMacro()->steps.empty());
    REQUIRE(session.SetMacroName("Still Incomplete"));
    REQUIRE(session.SetMacroTrigger("-1"));
    REQUIRE(session.SelectedMacro()->buttonBinding == "(unbound)");

    // The Control surface deliberately exposes no power-system preset.
    REQUIRE(session.AddMacro());
    REQUIRE(session.SelectedMacro()->name == "Macro1");
    REQUIRE(session.SelectedMacro()->steps.size() == 1);
    REQUIRE(session.SelectedMacro()->steps[0].targets[0] == "NextSystem");

    // Raw unknown shortcut outputs remain visible until the user explicitly picks
    // a catalog output; incomplete rows are retained in the page transaction.
    REQUIRE(session.ShortcutRecords().size() == 2);
    const auto incompleteShortcut = session.ShortcutRecords()[1].recordId;
    REQUIRE(session.SelectShortcut(incompleteShortcut));
    REQUIRE(session.SetShortcutTrigger("Stick@9"));
    REQUIRE(session.SetShortcutOutput(0));
    REQUIRE(session.SelectedShortcut()->output == "mouse:1");
    REQUIRE(session.AddMenuNavigationPreset());
    REQUIRE(session.ShortcutRecords().size() == 9);

    REQUIRE(session.Dirty());
    REQUIRE(session.Apply(error));
    REQUIRE(repository.saves == 1); // one atomic configuration write/reload seam
    REQUIRE(!session.Dirty());
    REQUIRE(repository.stored.macros[0].steps[0].targets[0] == "PreviousSystem");
    REQUIRE(repository.stored.macros[0].steps[0].gapMs == 99);

    REQUIRE(session.DeleteShortcut());
    REQUIRE(session.Dirty());
    session.Cancel();
    REQUIRE(!session.Dirty());
    REQUIRE(session.ShortcutRecords().size() == 9);

    repository.failSave = true;
    REQUIRE(session.AddStep());
    REQUIRE(!session.Apply(error));
    REQUIRE(session.Dirty());
    REQUIRE(repository.saves == 2);
}

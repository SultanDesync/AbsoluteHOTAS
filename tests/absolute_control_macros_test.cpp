#include "AbsoluteControlMacros.h"

#include <cassert>
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
    assert(session.Open(error));
    assert(repository.loads == 1);
    assert(!session.Dirty());
    assert(session.MacroRecords().size() == 3);
    assert(session.MacroRecords()[0].label == "First (0)");
    assert(session.MacroRecords()[2].label == "First (1)");
    assert(session.StepRecords().size() == 2);
    assert(session.TargetRecords().size() == 2);

    // Ordered steps and chord source tokens survive edits without resolution.
    const auto secondStep = session.StepRecords()[1].recordId;
    assert(session.SelectStep(secondStep));
    assert(session.MoveStep(-1));
    assert(session.SetStepGap(99));
    assert(session.AddTarget(0));
    assert(session.SelectedStep()->targets.back() == "FireBoosters");

    const auto incomplete = session.MacroRecords()[1].recordId;
    assert(session.SelectMacro(incomplete));
    assert(session.SelectedMacro()->steps.empty());
    assert(session.SetMacroName("Still Incomplete"));
    assert(session.SetMacroTrigger("-1"));
    assert(session.SelectedMacro()->buttonBinding == "(unbound)");

    // The Control surface deliberately exposes no power-system preset.
    assert(session.AddMacro());
    assert(session.SelectedMacro()->name == "Macro1");
    assert(session.SelectedMacro()->steps.size() == 1);
    assert(session.SelectedMacro()->steps[0].targets[0] == "NextSystem");

    // Raw unknown shortcut outputs remain visible until the user explicitly picks
    // a catalog output; incomplete rows are retained in the page transaction.
    assert(session.ShortcutRecords().size() == 2);
    const auto incompleteShortcut = session.ShortcutRecords()[1].recordId;
    assert(session.SelectShortcut(incompleteShortcut));
    assert(session.SetShortcutTrigger("Stick@9"));
    assert(session.SetShortcutOutput(0));
    assert(session.SelectedShortcut()->output == "mouse:1");
    assert(session.AddMenuNavigationPreset());
    assert(session.ShortcutRecords().size() == 9);

    assert(session.Dirty());
    assert(session.Apply(error));
    assert(repository.saves == 1); // one atomic configuration write/reload seam
    assert(!session.Dirty());
    assert(repository.stored.macros[0].steps[0].targets[0] == "PreviousSystem");
    assert(repository.stored.macros[0].steps[0].gapMs == 99);

    assert(session.DeleteShortcut());
    assert(session.Dirty());
    session.Cancel();
    assert(!session.Dirty());
    assert(session.ShortcutRecords().size() == 9);

    repository.failSave = true;
    assert(session.AddStep());
    assert(!session.Apply(error));
    assert(session.Dirty());
    assert(repository.saves == 2);
}

#pragma once

namespace RE {
    class TESObjectREFR;

    namespace BSScript {
        class IVirtualMachine;
    }
}

namespace Papyrus {
    bool RegisterFunctions(RE::BSScript::IVirtualMachine* a_vm);
    void SetPilotState(bool a_isPiloting);
    void StartStateMonitor();
}

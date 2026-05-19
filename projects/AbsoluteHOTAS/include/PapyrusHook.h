#pragma once

namespace RE::BSScript {
    class IVirtualMachine;
}

namespace SFSE {
    class LoadInterface;
}

namespace PapyrusHook {
    void Install(const SFSE::LoadInterface* a_sfse);
}

#include "PCH.h"

#include "ProfileOverlay.h"

#include <cstring>

namespace ProfileOverlay {

int ComputeDiff(const CSimpleIniA& eff, const CSimpleIniA& base, CSimpleIniA& out) {
    int overrides = 0;

    CSimpleIniA::TNamesDepend sections;
    eff.GetAllSections(sections);
    for (const auto& sec : sections) {
        CSimpleIniA::TNamesDepend keys;
        eff.GetAllKeys(sec.pItem, keys);
        for (const auto& k : keys) {
            const char* effV  = eff.GetValue(sec.pItem, k.pItem, nullptr);
            const char* baseV = base.GetValue(sec.pItem, k.pItem, nullptr);
            const bool differs = !baseV || !effV || std::strcmp(effV, baseV) != 0;
            if (differs) {
                out.SetValue(sec.pItem, k.pItem, effV ? effV : "");
                ++overrides;
            } else if (out.GetValue(sec.pItem, k.pItem, nullptr)) {
                out.Delete(sec.pItem, k.pItem, /*removeEmpty*/ true);  // reverted to base
            }
        }
    }
    return overrides;
}

} // namespace ProfileOverlay

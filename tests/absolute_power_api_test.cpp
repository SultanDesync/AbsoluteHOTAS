#include "AbsolutePowerAPI.h"

#include <cassert>
#include <cstddef>

int main() {
    using namespace AbsolutePowerApi;
    static_assert(sizeof(void*) == 8);
    static_assert(sizeof(KeyboardBindingV1) == 72);
    static_assert(offsetof(ApiV1, processGameThread) == 128);
    static_assert(offsetof(ApiV1, getKeyboardBinding) == 136);
    static_assert(offsetof(ApiV1, clearKeyboardBinding) == 152);
    static_assert(offsetof(ApiV1, recordWeaponFire) == 160);
    static_assert(sizeof(ApiV1) == 168);
    assert(static_cast<std::uint32_t>(Result::Conflict) == 9);
    assert(static_cast<std::uint32_t>(Result::WriteFailure) == 10);
    return 0;
}

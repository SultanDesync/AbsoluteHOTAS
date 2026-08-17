#include "AbsoluteControlSubscriber.h"
#include "SFSEInterface.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace {
using namespace AbsoluteControlPanelApi;

enum class HostBehavior { Accept, NotReady, Reject, Invalid };

HostBehavior g_behavior{HostBehavior::Accept};
const ApiV1* g_resolvedApi{};
std::uint32_t g_moduleCalls{};
std::uint32_t g_pageCalls{};
ModuleDescriptorV1 g_copiedModule{};
std::array<PageDescriptorV1, 2> g_copiedPages{};

Result __cdecl RegisterModule(const ModuleDescriptorV1* module) noexcept {
    ++g_moduleCalls;
    if (g_behavior == HostBehavior::NotReady) return Result::NotReady;
    if (g_behavior == HostBehavior::Reject) return Result::Rejected;
    if (g_behavior == HostBehavior::Invalid) return Result::InvalidArgument;
    if (!module) return Result::InvalidArgument;
    g_copiedModule = *module;
    return Result::Ok;
}

Result __cdecl RegisterPage(const PageDescriptorV1* page) noexcept {
    if (g_behavior == HostBehavior::NotReady) return Result::NotReady;
    if (g_behavior == HostBehavior::Reject) return Result::Rejected;
    if (!page || g_pageCalls >= g_copiedPages.size()) return Result::InvalidArgument;
    g_copiedPages[g_pageCalls++] = *page;
    return Result::Ok;
}

Result __cdecl UnregisterModule(const char*) noexcept { return Result::Ok; }
Result __cdecl RequestRefresh(const char*, const char*) noexcept { return Result::Ok; }
std::uint8_t __cdecl IsOpen() noexcept { return 0; }
std::uint8_t __cdecl IsInputCaptureActive() noexcept { return 0; }

ApiV1 g_api{
    .structSize = sizeof(ApiV1),
    .abiVersion = kAbiVersion,
    .moduleId = "absolute.control_panel",
    .displayName = "Absolute Control Test Host",
    .version = "test",
    .registerPage = &RegisterPage,
    .unregisterModule = &UnregisterModule,
    .requestRefresh = &RequestRefresh,
    .registerModule = &RegisterModule,
    .isOpen = &IsOpen,
    .isInputCaptureActive = &IsInputCaptureActive,
};

const ApiV1* __cdecl ResolveHost(const wchar_t* moduleName) noexcept {
    return moduleName && std::wcscmp(moduleName, L"AbsoluteControlPanel.dll") == 0 ?
        g_resolvedApi : nullptr;
}

void ResetFakeHost() {
    g_behavior = HostBehavior::Accept;
    g_resolvedApi = &g_api;
    g_moduleCalls = 0;
    g_pageCalls = 0;
    g_copiedModule = {};
    g_copiedPages = {};
    AbsoluteControlSubscriber::Testing::Reset();
}
} // namespace

int main() {
    static_assert(sizeof(void*) == 8);
    static_assert(sizeof(ValueV1) == 288);
    static_assert(sizeof(ControlDescriptorV1) == 392);
    static_assert(sizeof(ChoiceOptionV1) == 112);
    static_assert(sizeof(ModuleDescriptorV1) == 356);
    static_assert(sizeof(PageDescriptorV1) == 488);
    static_assert(sizeof(ApiV1) == 88);
    static_assert(offsetof(PageDescriptorV1, readChoiceOptions) == 480);
    static_assert(offsetof(ApiV1, registerModule) == 56);
    static_assert(offsetof(ApiV1, isInputCaptureActive) == 72);
    static_assert(offsetof(ApiV1, capabilities) == 80);
    static_assert(kPageDescriptorV1BaseSize == 480);
    static_assert(kApiV1BaseSize == 80);
    static_assert(sizeof(SFSE::Impl::SFSEInterface) == 40);
    static_assert(sizeof(SFSE::Impl::SFSEMessagingInterface) == 24);
    static_assert(offsetof(SFSE::Impl::SFSEInterface, queryInterface) == 16);
    static_assert(offsetof(SFSE::Impl::SFSEMessagingInterface, registerListener) == 8);

    std::size_t pageCount{};
    const auto* pages = AbsoluteControlSubscriber::Testing::Pages(pageCount);
    assert(pageCount == 2);
    assert(std::strcmp(AbsoluteControlSubscriber::Testing::Module().moduleId,
                       "absolute.hotas") == 0);
    assert(std::strcmp(pages[0].pageId, "hotas-setup") == 0);
    assert(std::strcmp(pages[1].pageId, "hotas-diagnostics") == 0);
    assert(AbsoluteControlSubscriber::Testing::ValidateDescriptors(pages, pageCount) ==
           Result::Ok);

    std::array<PageDescriptorV1, 2> invalidPages{pages[0], pages[1]};
    std::array<ControlDescriptorV1, 5> setupControls{};
    std::array<ControlDescriptorV1, 5> diagnosticControls{};
    std::copy_n(pages[0].controls, setupControls.size(), setupControls.begin());
    std::copy_n(pages[1].controls, diagnosticControls.size(), diagnosticControls.begin());
    invalidPages[0].controls = setupControls.data();
    invalidPages[1].controls = diagnosticControls.data();

    strcpy_s(diagnosticControls[0].controlId, setupControls[0].controlId);
    assert(AbsoluteControlSubscriber::Testing::ValidateDescriptors(
               invalidPages.data(), invalidPages.size()) == Result::Duplicate);
    diagnosticControls[0] = pages[1].controls[0];
    diagnosticControls[0].kind = static_cast<ControlKind>(99);
    assert(AbsoluteControlSubscriber::Testing::ValidateDescriptors(
               invalidPages.data(), invalidPages.size()) == Result::InvalidArgument);

    ResetFakeHost();
    g_resolvedApi = nullptr;
    assert(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::NotFound);
    assert(!AbsoluteControlSubscriber::IsHosted());

    ResetFakeHost();
    g_behavior = HostBehavior::Reject;
    assert(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Rejected);
    assert(!AbsoluteControlSubscriber::IsHosted());
    g_behavior = HostBehavior::Accept;
    assert(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Rejected);

    ResetFakeHost();
    g_behavior = HostBehavior::Invalid;
    assert(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Rejected);
    g_behavior = HostBehavior::Accept;
    assert(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Rejected);

    ResetFakeHost();
    g_behavior = HostBehavior::NotReady;
    assert(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::NotReady);
    assert(!AbsoluteControlSubscriber::IsHosted());
    g_behavior = HostBehavior::Accept;
    assert(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Ok);
    assert(AbsoluteControlSubscriber::IsHosted());
    assert(g_moduleCalls == 2);
    assert(g_pageCalls == 2);
    assert(std::strcmp(g_copiedModule.moduleId, "absolute.hotas") == 0);
    assert(std::strcmp(g_copiedPages[0].pageId, "hotas-setup") == 0);
    assert(std::strcmp(g_copiedPages[1].pageId, "hotas-diagnostics") == 0);

    ValueV1 value{};
    assert(g_copiedPages[0].readValue(
               nullptr, g_copiedPages[0].controls[0].controlId, &value) == Result::Ok);
    assert(value.kind == ValueKind::String);
    assert(value.stringValue[0] != '\0');

    AbsoluteControlSubscriber::Testing::ForceReadException(true);
    value = {};
    assert(g_copiedPages[0].readValue(
               nullptr, g_copiedPages[0].controls[0].controlId, &value) ==
           Result::Rejected);
    AbsoluteControlSubscriber::Testing::ForceReadException(false);

    ResetFakeHost();
    ApiV1 invalidApi = g_api;
    invalidApi.structSize = static_cast<std::uint32_t>(offsetof(ApiV1, registerModule));
    g_resolvedApi = &invalidApi;
    assert(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Rejected);
    assert(!AbsoluteControlSubscriber::IsHosted());

    return 0;
}

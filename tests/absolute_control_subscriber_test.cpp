#include "AbsoluteControlSettings.h"
#include "AbsoluteControlSubscriber.h"
#include "SFSEInterface.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>
#include <vector>

namespace TestSettings {
AbsoluteControlSettings::ScalarState stored{};
AbsoluteControlSettings::Revision revision{1, 11};
bool canEdit{true};
bool failLoad{};
bool failApply{};
} // namespace TestSettings

namespace AbsoluteControlSettings {
bool Load(ScalarState& state, Revision& revision, std::string& error) noexcept
{
    if (TestSettings::failLoad) {
        error = "test load failure";
        return false;
    }
    state = TestSettings::stored;
    revision = TestSettings::revision;
    error.clear();
    return true;
}

bool Apply(const ScalarState& state, const Revision& expected,
           ScalarState& readBack, Revision& revision,
           std::string& error) noexcept
{
    if (!TestSettings::canEdit || TestSettings::failApply ||
        expected != TestSettings::revision) {
        error = "test apply failure";
        return false;
    }
    TestSettings::stored = state;
    ++TestSettings::revision.sourceFingerprint;
    readBack = TestSettings::stored;
    revision = TestSettings::revision;
    error.clear();
    return true;
}

Revision CurrentRevision() noexcept
{
    return TestSettings::revision;
}

bool CanEdit() noexcept
{
    return TestSettings::canEdit;
}
} // namespace AbsoluteControlSettings

namespace {
using namespace AbsoluteControlPanelApi;

enum class HostBehavior { Accept, NotReady, Reject, Invalid };

HostBehavior g_behavior{HostBehavior::Accept};
const ApiV1* g_resolvedApi{};
std::uint32_t g_moduleCalls{};
std::uint32_t g_pageCalls{};
bool g_hostOpen{};
bool g_hostCapture{};
ModuleDescriptorV1 g_copiedModule{};
std::array<PageDescriptorV1, 3> g_copiedPages{};

Result __cdecl RegisterModule(const ModuleDescriptorV1* module) noexcept
{
    ++g_moduleCalls;
    if (g_behavior == HostBehavior::NotReady) return Result::NotReady;
    if (g_behavior == HostBehavior::Reject) return Result::Rejected;
    if (g_behavior == HostBehavior::Invalid) return Result::InvalidArgument;
    if (!module) return Result::InvalidArgument;
    g_copiedModule = *module;
    return Result::Ok;
}

Result __cdecl RegisterPage(const PageDescriptorV1* page) noexcept
{
    if (g_behavior == HostBehavior::NotReady) return Result::NotReady;
    if (g_behavior == HostBehavior::Reject) return Result::Rejected;
    if (!page || g_pageCalls >= g_copiedPages.size()) return Result::InvalidArgument;
    g_copiedPages[g_pageCalls++] = *page;
    return Result::Ok;
}

Result __cdecl UnregisterModule(const char*) noexcept { return Result::Ok; }
Result __cdecl RequestRefresh(const char*, const char*) noexcept { return Result::Ok; }
std::uint8_t __cdecl IsOpen() noexcept { return g_hostOpen ? 1 : 0; }
std::uint8_t __cdecl IsInputCaptureActive() noexcept { return g_hostCapture ? 1 : 0; }

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
    .capabilities = kCapabilityLabeledChoices,
};

const ApiV1* __cdecl ResolveHost(const wchar_t* moduleName) noexcept
{
    return moduleName && std::wcscmp(moduleName, L"AbsoluteControlPanel.dll") == 0 ?
        g_resolvedApi : nullptr;
}

void ResetFakeHost()
{
    g_behavior = HostBehavior::Accept;
    g_resolvedApi = &g_api;
    g_moduleCalls = 0;
    g_pageCalls = 0;
    g_hostOpen = false;
    g_hostCapture = false;
    g_copiedModule = {};
    g_copiedPages = {};
    TestSettings::stored = {};
    TestSettings::revision = {1, 11};
    TestSettings::canEdit = true;
    TestSettings::failLoad = false;
    TestSettings::failApply = false;
    AbsoluteControlSubscriber::Testing::Reset();
}

const ControlDescriptorV1* FindControl(const PageDescriptorV1& page,
                                       std::string_view id)
{
    for (std::uint32_t index = 0; index < page.controlCount; ++index) {
        if (page.controls[index].controlId == id) return &page.controls[index];
    }
    return nullptr;
}
} // namespace

int main()
{
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
    assert(pageCount == 3);
    assert(std::strcmp(AbsoluteControlSubscriber::Testing::Module().moduleId,
                       "absolute.hotas") == 0);
    assert(std::strcmp(pages[0].pageId, "hotas-setup") == 0);
    assert(std::strcmp(pages[1].pageId, "hotas-flight-axes") == 0);
    assert(std::strcmp(pages[2].pageId, "hotas-diagnostics") == 0);
    assert(AbsoluteControlSubscriber::Testing::ValidateDescriptors(pages, pageCount) ==
           Result::Ok);

    std::array<PageDescriptorV1, 3> invalidPages{pages[0], pages[1], pages[2]};
    std::array<std::vector<ControlDescriptorV1>, 3> copiedControls;
    for (std::size_t index = 0; index < invalidPages.size(); ++index) {
        copiedControls[index].assign(
            pages[index].controls, pages[index].controls + pages[index].controlCount);
        invalidPages[index].controls = copiedControls[index].data();
    }
    strcpy_s(copiedControls[2][3].controlId, copiedControls[0][0].controlId);
    assert(AbsoluteControlSubscriber::Testing::ValidateDescriptors(
               invalidPages.data(), invalidPages.size()) == Result::Duplicate);
    copiedControls[2][3] = pages[2].controls[3];
    copiedControls[0][0].kind = static_cast<ControlKind>(99);
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
    assert(g_pageCalls == 3);
    assert(std::strcmp(g_copiedModule.moduleId, "absolute.hotas") == 0);
    assert(std::strcmp(g_copiedPages[0].pageId, "hotas-setup") == 0);
    assert(std::strcmp(g_copiedPages[1].pageId, "hotas-flight-axes") == 0);
    assert(std::strcmp(g_copiedPages[2].pageId, "hotas-diagnostics") == 0);

    g_hostOpen = true;
    g_hostCapture = true;
    assert(AbsoluteControlSubscriber::IsHostOpen());
    assert(AbsoluteControlSubscriber::IsHostInputCaptureActive());
    g_hostOpen = false;
    g_hostCapture = false;

    ValueV1 value{};
    assert(g_copiedPages[0].readValue(
               nullptr, g_copiedPages[0].controls[0].controlId, &value) == Result::Ok);
    assert(value.kind == ValueKind::String);
    assert(value.stringValue[0] != '\0');

    const auto& axesPage = g_copiedPages[1];
    assert(FindControl(axesPage, "flight-controls-enabled"));
    value = {};
    assert(axesPage.readValue(nullptr, "flight-controls-enabled", &value) == Result::Ok);
    assert(value.kind == ValueKind::Boolean && value.booleanValue == 1);

    ValueV1 edit = {};
    edit.kind = ValueKind::Boolean;
    edit.booleanValue = 0;
    assert(axesPage.writeDraft(nullptr, "flight-controls-enabled", &edit) == Result::Ok);
    value = {};
    assert(axesPage.readValue(nullptr, "flight-controls-enabled", &value) == Result::Ok);
    assert(value.booleanValue == 0);
    assert(TestSettings::stored.flightControlsEnabled);
    assert(axesPage.apply(nullptr) == Result::Ok);
    assert(!TestSettings::stored.flightControlsEnabled);

    edit = {};
    edit.kind = ValueKind::Float;
    edit.floatValue = 2.0;
    assert(axesPage.writeDraft(nullptr, "pitch-sensitivity", &edit) == Result::Ok);
    axesPage.cancel(nullptr);
    value = {};
    assert(axesPage.readValue(nullptr, "pitch-sensitivity", &value) == Result::Ok);
    assert(value.kind == ValueKind::Float && value.floatValue == 1.0);

    edit = {};
    edit.kind = ValueKind::Integer;
    edit.integerValue = 2;
    assert(axesPage.writeDraft(nullptr, "pitch-sensitivity", &edit) ==
           Result::InvalidArgument);
    edit.kind = ValueKind::Float;
    edit.floatValue = 3.1;
    assert(axesPage.writeDraft(nullptr, "pitch-sensitivity", &edit) ==
           Result::InvalidArgument);

    const auto& diagnosticsPage = g_copiedPages[2];
    std::array<ChoiceOptionV1, 3> options{};
    std::uint32_t optionCount{};
    assert(diagnosticsPage.readChoiceOptions(
               nullptr, "pilot-context-mode", options.data(),
               static_cast<std::uint32_t>(options.size()),
               &optionCount) == Result::Ok);
    assert(optionCount == 3);
    assert(options[1].value == 1);
    assert(std::strcmp(options[1].label, "Park flight controls") == 0);
    assert(diagnosticsPage.readChoiceOptions(
               nullptr, "pilot-context-mode", options.data(), 2, &optionCount) ==
           Result::CapacityExceeded);
    assert(optionCount == 3);

    TestSettings::canEdit = false;
    edit = {};
    edit.kind = ValueKind::Boolean;
    edit.booleanValue = 1;
    assert(axesPage.writeDraft(nullptr, "pitch-inverted", &edit) == Result::Rejected);
    TestSettings::canEdit = true;

    assert(axesPage.writeDraft(nullptr, "pitch-inverted", &edit) == Result::Ok);
    ++TestSettings::revision.sourceFingerprint;
    assert(axesPage.apply(nullptr) == Result::Rejected);
    axesPage.cancel(nullptr);
    value = {};
    assert(axesPage.readValue(nullptr, "pitch-inverted", &value) == Result::Ok);
    assert(value.booleanValue == 0);

    assert(axesPage.writeDraft(nullptr, "pitch-inverted", &edit) == Result::Ok);
    TestSettings::failApply = true;
    assert(axesPage.apply(nullptr) == Result::WriteFailure);
    TestSettings::failApply = false;
    value = {};
    assert(axesPage.readValue(nullptr, "pitch-inverted", &value) == Result::Ok);
    assert(value.booleanValue == 1);
    assert(axesPage.apply(nullptr) == Result::Ok);
    assert(TestSettings::stored.pitchInverted);

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

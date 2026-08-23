#include "AbsoluteControlSettings.h"
#include "AbsoluteControlMacros.h"
#include "AbsoluteControlProfiles.h"
#include "AbsoluteControlSubscriber.h"
#include "AbsoluteControlTelemetry.h"
#include "HotasBindingCapture.h"
#include "SFSEInterface.h"
#include "ShipOutput.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cwchar>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace TestSettings {
AbsoluteControlSettings::ScalarState stored = AbsoluteControlSettings::DefaultState();
HotasBindingCatalog::BindingState storedBindings{};
AbsoluteControlSettings::ShipRouteState storedRoutes =
    AbsoluteControlSettings::DefaultShipRoutes();
AbsoluteControlSettings::Revision revision{1, 11};
AbsoluteControlDevices::CalibrationMap calibration{};
bool failLoad{};
bool failApply{};
std::string editTarget;
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

bool LoadWithBindings(ScalarState& state,
                      HotasBindingCatalog::BindingState& bindings,
                      Revision& revision, std::string& error) noexcept
{
    if (!Load(state, revision, error)) return false;
    bindings = TestSettings::storedBindings;
    return true;
}

bool LoadWithBindingsAndRoutes(
    ScalarState& state, HotasBindingCatalog::BindingState& bindings,
    ShipRouteState& routes, Revision& revision, std::string& error) noexcept
{
    if (!LoadWithBindings(state, bindings, revision, error)) return false;
    routes = TestSettings::storedRoutes;
    return true;
}

bool LoadEditTargetWithBindingsAndRoutes(
    ScalarState& state, HotasBindingCatalog::BindingState& bindings,
    ShipRouteState& routes, std::string& editTarget,
    Revision& revision, std::string& error) noexcept
{
    if (!LoadWithBindingsAndRoutes(
            state, bindings, routes, revision, error)) return false;
    editTarget = TestSettings::editTarget;
    return true;
}

bool Apply(const ScalarState& state, const Revision& expected,
           ScalarState& readBack, Revision& revision,
           std::string& error) noexcept
{
    if (TestSettings::failApply ||
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

bool ApplyWithBindings(
    const ScalarState& state,
    const HotasBindingCatalog::BindingState& bindings,
    const Revision& expected, ScalarState& readBack,
    HotasBindingCatalog::BindingState& bindingReadBack,
    Revision& revision, std::string& error) noexcept
{
    if (TestSettings::failApply ||
        expected != TestSettings::revision) {
        error = "test apply failure";
        return false;
    }
    TestSettings::stored = state;
    TestSettings::storedBindings = bindings;
    ++TestSettings::revision.sourceFingerprint;
    readBack = TestSettings::stored;
    bindingReadBack = TestSettings::storedBindings;
    revision = TestSettings::revision;
    error.clear();
    return true;
}

bool ApplyWithBindingsAndRoutes(
    const ScalarState& state,
    const HotasBindingCatalog::BindingState& bindings,
    const ShipRouteState& routes, const Revision& expected,
    ScalarState& readBack,
    HotasBindingCatalog::BindingState& bindingReadBack,
    ShipRouteState& routeReadBack, Revision& revision,
    std::string& error) noexcept
{
    if (TestSettings::failApply ||
        expected != TestSettings::revision) {
        error = "test apply failure";
        return false;
    }
    TestSettings::stored = state;
    TestSettings::storedBindings = bindings;
    TestSettings::storedRoutes = routes;
    ++TestSettings::revision.sourceFingerprint;
    readBack = TestSettings::stored;
    bindingReadBack = TestSettings::storedBindings;
    routeReadBack = TestSettings::storedRoutes;
    revision = TestSettings::revision;
    error.clear();
    return true;
}

bool ApplyEditTargetWithBindingsAndRoutes(
    const ScalarState& state,
    const HotasBindingCatalog::BindingState& bindings,
    const ShipRouteState& routes, std::string_view expectedEditTarget,
    const Revision& expected, ScalarState& readBack,
    HotasBindingCatalog::BindingState& bindingReadBack,
    ShipRouteState& routeReadBack, Revision& revision,
    std::string& error) noexcept
{
    if (expectedEditTarget != TestSettings::editTarget) {
        error = "test edit target mismatch";
        return false;
    }
    return ApplyWithBindingsAndRoutes(state, bindings, routes, expected,
        readBack, bindingReadBack, routeReadBack, revision, error);
}

bool LoadDeviceState(
    HotasBindingCatalog::BindingState& bindings,
    AbsoluteControlDevices::CalibrationMap& calibration,
    Revision& revision, std::string& error) noexcept
{
    if (TestSettings::failLoad) {
        error = "test load failure";
        return false;
    }
    bindings = TestSettings::storedBindings;
    calibration = TestSettings::calibration;
    revision = TestSettings::revision;
    error.clear();
    return true;
}

bool ApplyDeviceState(
    const HotasBindingCatalog::BindingState& bindings,
    const AbsoluteControlDevices::CalibrationMap& calibration,
    const Revision& expected,
    HotasBindingCatalog::BindingState& bindingReadBack,
    AbsoluteControlDevices::CalibrationMap& calibrationReadBack,
    Revision& revision, std::string& error) noexcept
{
    if (TestSettings::failApply ||
        expected != TestSettings::revision) {
        error = "test apply failure";
        return false;
    }
    TestSettings::storedBindings = bindings;
    TestSettings::calibration = calibration;
    ++TestSettings::revision.sourceFingerprint;
    bindingReadBack = TestSettings::storedBindings;
    calibrationReadBack = TestSettings::calibration;
    revision = TestSettings::revision;
    error.clear();
    return true;
}

Revision CurrentRevision() noexcept
{
    return TestSettings::revision;
}

} // namespace AbsoluteControlSettings

namespace ShipOutputSystem {
ShipActionRouteInfo GetShipActionRouteInfo(std::string_view actionId)
{
    const auto* definition = FindShipAction(actionId);
    if (!definition) return {};
    ShipOutput output = NoOutput;
    if (definition->vanillaOutput.kind == ShipActionOutputKind::Keyboard) {
        output = {ShipOutputKind::Keyboard, definition->vanillaOutput.code,
                  definition->vanillaOutput.extended};
    } else if (definition->vanillaOutput.kind == ShipActionOutputKind::Mouse) {
        output = {ShipOutputKind::Mouse, definition->vanillaOutput.code,
                  definition->vanillaOutput.extended};
    }
    return {
        definition->actionId,
        definition->displayLabel,
        definition->group,
        definition->recommendedMethod,
        false,
        output,
        definition->recommendedMethod == ShipControlMethod::Context
            ? KeyboardResolutionSource::FixedContext
            : KeyboardResolutionSource::VanillaFallback,
        actionId == "FireBoosters"
            ? ShipActionAvailability::SupportedWaitingForContext
            : ShipActionAvailability::AvailableNow,
    };
}
} // namespace ShipOutputSystem

namespace TestProfiles {
class Repository final : public AbsoluteControlProfiles::Repository {
public:
    std::vector<WizardConfig::ProfileSummary> summaries{
        {.name = "Flight Aux", .kind = "overlay",
         .filename = "Profile_01_Flight_Aux.ini", .trigger = "#1@7",
         .mode = "toggle", .sequence = 1, .slot = 2, .overrideCount = 4},
        {.name = "Independent", .kind = "full",
         .filename = "Profile_02_Independent.ini", .trigger = "8",
         .mode = "selector", .sequence = 2, .slot = 3, .overrideCount = 70},
    };
    std::string current;
    bool editDirty{};
    std::vector<WizardConfig::ProfileActivationUpdate> updates;

    std::vector<WizardConfig::ProfileSummary> List() override { return summaries; }
    void GetMainActivation(std::string& trigger, std::string& mode) override
    {
        trigger = "1";
        mode = "momentary";
    }
    std::string CurrentEditTarget() override { return current; }
    bool LoadEditTarget(const std::string& name, std::string&) override
    {
        current = name;
        TestSettings::editTarget = name;
        editDirty = false;
        return true;
    }
    bool HasEditTargetChanges() override { return editDirty; }
    bool SaveEditTarget(std::string&) override { editDirty = false; return true; }
    bool DiscardEditTarget(std::string&) override { editDirty = false; return true; }
    bool SaveActivations(
        const std::vector<WizardConfig::ProfileActivationUpdate>& value,
        std::string&) override
    {
        updates = value;
        return true;
    }
    bool CreateOverlay(const std::string& name, std::string&) override
    {
        summaries.push_back({.name = name, .kind = "overlay",
            .filename = "created.ini"});
        return true;
    }
    bool ExportFull(const std::string& name, std::string&) override
    {
        summaries.push_back({.name = name, .kind = "full",
            .filename = "exported.ini"});
        return true;
    }
    bool ImportFull(const std::string&, std::string&) override { return true; }
    bool ResetMain(std::string&) override { return true; }
};
Repository repository;
} // namespace TestProfiles

namespace AbsoluteControlProfiles {
Repository& WizardRepository() noexcept { return TestProfiles::repository; }
} // namespace AbsoluteControlProfiles

namespace TestMacros {
class Repository final : public AbsoluteControlMacros::Repository {
public:
    WizardState stored = [] {
        WizardState state;
        state.loaded = true;
        state.macros.push_back({"Landing", "Stick@4", false,
            {{{"NextSystem", "key:0x1E"}, false, 2, 25}}});
        state.customBindings.push_back({"Stick@8", "key:0x11"});
        return state;
    }();
    int saves{};

    bool Load(WizardState& state, std::string& error) override
    {
        state = stored;
        error.clear();
        return true;
    }
    bool Save(const WizardState& state, std::string& error) override
    {
        stored = state;
        ++saves;
        error.clear();
        return true;
    }
};
Repository repository;
} // namespace TestMacros

namespace AbsoluteControlMacros {
Repository& WizardRepository() noexcept { return TestMacros::repository; }
} // namespace AbsoluteControlMacros

namespace TestCapture {
HotasBindingCapture::PollState nextState{HotasBindingCapture::PollState::Capturing};
std::string nextBinding;
int beginSlot{-1};
int settleMilliseconds{-1};
int cancelCalls{};
} // namespace TestCapture

namespace HotasBindingCapture {
void Begin(const HotasBindingCatalog::Target& target)
{
    TestCapture::beginSlot = target.captureSlot;
}

void BeginButton(int captureSlot, std::string_view, int settleWindowMilliseconds)
{
    TestCapture::beginSlot = captureSlot;
    TestCapture::settleMilliseconds = settleWindowMilliseconds;
}

PollState Poll(std::string& binding)
{
    binding = TestCapture::nextBinding;
    return TestCapture::nextState;
}

void Cancel() noexcept
{
    ++TestCapture::cancelCalls;
}
} // namespace HotasBindingCapture

namespace {
using namespace AbsoluteControlPanelApi;

inline constexpr std::array<const char*, 9> kExpectedPageIds{
    "hotas-flight-axes",
    "hotas-ship-buttons",
    "hotas-throttle",
    "hotas-aiming",
    "hotas-profiles",
    "hotas-macros",
    "hotas-devices",
    "hotas-diagnostics",
    "hotas-setup",
};

enum class HostBehavior { Accept, NotReady, Reject, Invalid, DuplicateModule };

HostBehavior g_behavior{HostBehavior::Accept};
const ApiV1* g_resolvedApi{};
std::uint32_t g_moduleCalls{};
std::uint32_t g_pageCalls{};
std::uint32_t g_unregisterCalls{};
std::size_t g_rejectPageIndex{kExpectedPageIds.size()};
bool g_hostOpen{};
bool g_hostCapture{};
std::uint32_t g_openPageCalls{};
Result g_openPageResult{Result::Ok};
std::string g_openModule;
std::string g_openPage;
ModuleDescriptorV1 g_copiedModule{};
std::array<PageDescriptorV1, kExpectedPageIds.size()> g_copiedPages{};

namespace Composition = AbsoluteControlCompositionExperimental;
Composition::PageCompositionDescriptorV1 g_copiedComposition{};
std::vector<Composition::NodeDescriptorV1> g_compositionNodes;
std::vector<Composition::AssociationDescriptorV1> g_compositionAssociations;

Result __cdecl RegisterComposition(
    const Composition::PageCompositionDescriptorV1* descriptor) noexcept
{
    if (!descriptor || !descriptor->nodes || descriptor->nodeCount == 0 ||
        (descriptor->associationCount != 0 && !descriptor->associations)) {
        return Result::InvalidArgument;
    }
    g_copiedComposition = *descriptor;
    g_compositionNodes.assign(
        descriptor->nodes, descriptor->nodes + descriptor->nodeCount);
    if (descriptor->associationCount != 0) {
        g_compositionAssociations.assign(descriptor->associations,
            descriptor->associations + descriptor->associationCount);
    } else {
        g_compositionAssociations.clear();
    }
    g_copiedComposition.nodes = g_compositionNodes.data();
    g_copiedComposition.associations = g_compositionAssociations.data();
    return Result::Ok;
}

Result __cdecl UnregisterComposition(const char*) noexcept
{
    return Result::Ok;
}

Result __cdecl RequestCompositionRefresh(
    const char*, const char*) noexcept
{
    return Result::Ok;
}

Composition::ApiV1 g_compositionApi{
    .structSize = sizeof(Composition::ApiV1),
    .abiVersion = Composition::kAbiVersion,
    .moduleId = "absolute.control_panel",
    .version = "C2 test",
    .capabilities = Composition::kC2Capabilities,
    .registerPageComposition = &RegisterComposition,
    .unregisterModule = &UnregisterComposition,
    .requestRefresh = &RequestCompositionRefresh,
};

Result __cdecl RegisterModule(const ModuleDescriptorV1* module) noexcept
{
    ++g_moduleCalls;
    if (g_behavior == HostBehavior::NotReady) return Result::NotReady;
    if (g_behavior == HostBehavior::Reject) return Result::Rejected;
    if (g_behavior == HostBehavior::Invalid) return Result::InvalidArgument;
    if (g_behavior == HostBehavior::DuplicateModule) return Result::Duplicate;
    if (!module) return Result::InvalidArgument;
    g_copiedModule = *module;
    return Result::Ok;
}

Result __cdecl RegisterPage(const PageDescriptorV1* page) noexcept
{
    if (g_behavior == HostBehavior::NotReady) return Result::NotReady;
    if (g_behavior == HostBehavior::Reject) return Result::Rejected;
    if (!page || g_pageCalls >= g_copiedPages.size()) return Result::InvalidArgument;
    const auto pageIndex = g_pageCalls++;
    if (pageIndex == g_rejectPageIndex) return Result::Rejected;
    g_copiedPages[pageIndex] = *page;
    return Result::Ok;
}

Result __cdecl UnregisterModule(const char* moduleId) noexcept
{
    if (!moduleId || std::strcmp(moduleId, "absolute.hotas") != 0) {
        return Result::InvalidArgument;
    }
    ++g_unregisterCalls;
    g_copiedModule = {};
    g_copiedPages = {};
    g_copiedComposition = {};
    g_compositionNodes.clear();
    g_compositionAssociations.clear();
    return Result::Ok;
}
Result __cdecl RequestRefresh(const char*, const char*) noexcept { return Result::Ok; }
std::uint8_t __cdecl IsOpen() noexcept { return g_hostOpen ? 1 : 0; }
std::uint8_t __cdecl IsInputCaptureActive() noexcept { return g_hostCapture ? 1 : 0; }
Result __cdecl RequestOpenPage(const char* moduleId, const char* pageId) noexcept
{
    ++g_openPageCalls;
    g_openModule = moduleId ? moduleId : "";
    g_openPage = pageId ? pageId : "";
    return g_openPageResult;
}

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
    .capabilities = kCapabilityLabeledChoices |
                    kCapabilityProviderBindingCapture |
                    kCapabilityBindingConflictResolution |
                    kCapabilityRecordCollections |
                    kCapabilityActionConfirmation |
                    kCapabilityPageOpenRequests |
                    kCapabilityPinnedContextControls,
    .requestOpenPage = &RequestOpenPage,
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
    g_unregisterCalls = 0;
    g_rejectPageIndex = kExpectedPageIds.size();
    g_hostOpen = false;
    g_hostCapture = false;
    g_openPageCalls = 0;
    g_openPageResult = Result::Ok;
    g_openModule.clear();
    g_openPage.clear();
    g_copiedModule = {};
    g_copiedPages = {};
    TestSettings::stored = AbsoluteControlSettings::DefaultState();
    TestSettings::storedBindings = {};
    TestSettings::storedBindings.fill("(unbound)");
    TestSettings::storedRoutes = AbsoluteControlSettings::DefaultShipRoutes();
    TestSettings::revision = {1, 11};
    TestSettings::calibration.clear();
    TestSettings::failLoad = false;
    TestSettings::failApply = false;
    TestSettings::editTarget.clear();
    TestCapture::nextState = HotasBindingCapture::PollState::Capturing;
    TestCapture::nextBinding.clear();
    TestCapture::beginSlot = -1;
    TestCapture::settleMilliseconds = -1;
    TestCapture::cancelCalls = 0;
    TestProfiles::repository = {};
    TestMacros::repository = {};
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

const PageDescriptorV1* FindPageForControl(
    const PageDescriptorV1* pages, std::size_t pageCount, std::string_view id)
{
    for (std::size_t index = 0; index < pageCount; ++index) {
        if (FindControl(pages[index], id)) return &pages[index];
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
    static_assert(sizeof(RecordItemV1) == 552);
    static_assert(sizeof(BindingCaptureV1) == 456);
    static_assert(sizeof(ModuleDescriptorV1) == 356);
    static_assert(sizeof(PageDescriptorV1) == 528);
    static_assert(sizeof(ApiV1) == 96);
    static_assert(offsetof(PageDescriptorV1, readChoiceOptions) == 480);
    static_assert(offsetof(PageDescriptorV1, beginBindingCapture) == 488);
    static_assert(offsetof(PageDescriptorV1, pollBindingCapture) == 496);
    static_assert(offsetof(PageDescriptorV1, cancelBindingCapture) == 504);
    static_assert(offsetof(PageDescriptorV1, reassignBinding) == 512);
    static_assert(offsetof(PageDescriptorV1, readRecordItems) == 520);
    static_assert(offsetof(ApiV1, registerModule) == 56);
    static_assert(offsetof(ApiV1, isInputCaptureActive) == 72);
    static_assert(offsetof(ApiV1, capabilities) == 80);
    static_assert(offsetof(ApiV1, requestOpenPage) == 88);
    static_assert(kPageDescriptorV1BaseSize == 480);
    static_assert(kPageDescriptorV1RecordItemsSize == 528);
    static_assert(kApiV1BaseSize == 80);
    static_assert(kApiV1RequestOpenPageSize == 96);
    static_assert(static_cast<std::uint32_t>(ControlKind::GroupHeader) == 7);
    static_assert(static_cast<std::uint32_t>(ControlKind::RecordCollection) == 8);
    static_assert(kControlLayoutInline == (1U << 6));
    static_assert(kControlRequiresConfirmation == (1U << 7));
    static_assert(kCapabilityPageOpenRequests == (1ULL << 6));
    static_assert(kControlPinnedContext == (1U << 13));
    static_assert(kCapabilityPinnedContextControls == (1ULL << 7));
    static_assert(sizeof(SFSE::Impl::SFSEInterface) == 40);
    static_assert(sizeof(SFSE::Impl::SFSEMessagingInterface) == 24);
    static_assert(offsetof(SFSE::Impl::SFSEInterface, queryInterface) == 16);
    static_assert(offsetof(SFSE::Impl::SFSEMessagingInterface, registerListener) == 8);

    ResetFakeHost();
    assert(AbsoluteControlSubscriber::Testing::RegisterFlightAxesComposition(
               &g_compositionApi) == Result::Ok);
    assert(AbsoluteControlSubscriber::Testing::
        IsFlightAxesCompositionRegistered());
    assert(std::strcmp(g_copiedComposition.moduleId, "absolute.hotas") == 0);
    assert(std::strcmp(g_copiedComposition.pageId,
                       "hotas-flight-axes") == 0);
    assert(g_copiedComposition.nodeCount == 83);
    assert(g_copiedComposition.associationCount == 10);
    std::array<Composition::NodeStateV1, 9> compositionStates{};
    std::uint32_t compositionStateCount{};
    assert(g_copiedComposition.readNodeStates(
               g_copiedComposition.context, "absolute.hotas",
               "hotas-flight-axes", compositionStates.data(),
               static_cast<std::uint32_t>(compositionStates.size()),
               &compositionStateCount) == Result::Ok);
    assert(compositionStateCount == compositionStates.size());
    assert(std::strcmp(compositionStates[0].nodeId, "flight-summary") == 0);
    assert(std::strcmp(compositionStates[0].value, "0 / 6 AXES BOUND") == 0);
    assert(std::strcmp(compositionStates[1].value, "NEEDS BINDING") == 0);

    std::size_t pageCount{};
    const auto* pages = AbsoluteControlSubscriber::Testing::Pages(pageCount);
    ValueV1 compositionBindingEdit{};
    compositionBindingEdit.kind = ValueKind::String;
    strcpy_s(compositionBindingEdit.stringValue, "Test Stick@0x31");
    assert(pages[0].writeDraft(pages[0].context, "bind-throttle-axis",
               &compositionBindingEdit) == Result::Ok);
    compositionStates = {};
    compositionStateCount = 0;
    assert(g_copiedComposition.readNodeStates(
               g_copiedComposition.context, "absolute.hotas",
               "hotas-flight-axes", compositionStates.data(),
               static_cast<std::uint32_t>(compositionStates.size()),
               &compositionStateCount) == Result::Ok);
    assert(std::strcmp(compositionStates[0].value, "1 / 6 AXES BOUND") == 0);
    assert(std::strcmp(compositionStates[0].sourceLabel,
                       "UNAPPLIED DRAFT") == 0);
    assert(std::strcmp(compositionStates[1].value, "BOUND") == 0);
    pages[0].cancel(pages[0].context);
    compositionStates = {};
    compositionStateCount = 0;
    assert(g_copiedComposition.readNodeStates(
               g_copiedComposition.context, "absolute.hotas",
               "hotas-flight-axes", compositionStates.data(),
               static_cast<std::uint32_t>(compositionStates.size()),
               &compositionStateCount) == Result::Ok);
    assert(std::strcmp(compositionStates[1].value, "NEEDS BINDING") == 0);
    assert(AbsoluteControlSubscriber::Testing::RegisterShipButtonsComposition(
               &g_compositionApi) == Result::Ok);
    assert(AbsoluteControlSubscriber::Testing::
        IsShipButtonsCompositionRegistered());
    assert(std::strcmp(g_copiedComposition.pageId,
                       "hotas-ship-buttons") == 0);
    assert(g_copiedComposition.nodeCount == 100);
    assert(g_copiedComposition.associationCount == 0);
    assert(pageCount == kExpectedPageIds.size());
    assert(std::strcmp(AbsoluteControlSubscriber::Testing::Module().moduleId,
                       "absolute.hotas") == 0);
    assert(std::string_view(pages[0].pageId) == "hotas-flight-axes");
    assert(std::string_view(pages[0].displayName) == "Flight Axes");
    assert(std::string_view(pages[0].controls[0].controlId) ==
           "flight-axes-edit-profile");
    assert((pages[0].controls[0].flags & kControlPinnedContext) != 0);
    assert(std::string_view(pages[0].controls[1].controlId) ==
           "flight-axes-layer-mode");
    assert(std::string_view(pages[0].controls[2].controlId) ==
           "flight-axes-layer-modifier");
    assert(FindControl(pages[0], "bind-throttle-axis") != nullptr);
    assert(std::string_view(pages[1].pageId) == "hotas-ship-buttons");
    assert(std::string_view(pages[1].displayName) == "Ship Buttons");
    assert(FindControl(pages[1], "ship-native-controls")->kind ==
           ControlKind::GroupHeader);
    assert(FindControl(pages[1], "ship-axis-bindings") == nullptr);
    assert(FindControl(pages[1], "bind-throttle-axis") == nullptr);
    assert(FindControl(pages[1], "bind-ship-select-accept") != nullptr);
    assert(FindControl(pages[1], "bind-menu-accept") != nullptr);
    assert(FindControl(pages[1], "bind-menu-cancel") != nullptr);
    assert(FindControl(pages[1], "shortcut-menu-preset") == nullptr);
    assert(std::string_view(pages[8].pageId) == "hotas-setup");
    assert(std::string_view(pages[8].displayName) == "Administration");
    for (std::size_t index = 0; index < pageCount; ++index) {
        assert(std::strcmp(pages[index].pageId, kExpectedPageIds[index]) == 0);
        assert(pages[index].beginBindingCapture != nullptr);
        assert(pages[index].pollBindingCapture != nullptr);
        assert(pages[index].cancelBindingCapture != nullptr);
        assert(pages[index].reassignBinding != nullptr);
        assert(pages[index].context != nullptr);
    }
    std::uint32_t editableControlCount{};
    std::uint32_t pinnedControlCount{};
    for (std::size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        for (std::uint32_t controlIndex = 0;
             controlIndex < pages[pageIndex].controlCount; ++controlIndex) {
            const auto& control = pages[pageIndex].controls[controlIndex];
            editableControlCount +=
                (control.flags & kControlReadOnly) == 0 &&
                control.kind != ControlKind::GroupHeader ? 1 : 0;
            pinnedControlCount +=
                (control.flags & kControlPinnedContext) != 0 ? 1 : 0;
        }
    }
    assert(pinnedControlCount == 27);
    assert(editableControlCount ==
           AbsoluteControlSettings::Definitions().size() +
           HotasBindingCatalog::kTargetCount + 90);
    for (const auto& definition : AbsoluteControlSettings::Definitions()) {
        const auto* owner = FindPageForControl(pages, pageCount, definition.controlId);
        assert(owner);
        const auto* control = FindControl(*owner, definition.controlId);
        assert(control);
        const auto expectedKind =
            definition.type == AbsoluteControlSettings::ScalarType::Boolean ?
                ControlKind::Toggle :
            definition.type == AbsoluteControlSettings::ScalarType::Integer ?
                ControlKind::IntegerSlider :
            definition.type == AbsoluteControlSettings::ScalarType::Choice ?
                ControlKind::Choice : ControlKind::FloatSlider;
        assert(control->kind == expectedKind);
        if (definition.type != AbsoluteControlSettings::ScalarType::Boolean) {
            assert(control->minimumValue == definition.minimum);
            assert(control->maximumValue == definition.maximum);
            assert(control->stepValue == definition.step);
        }
        assert((control->flags & kControlReadOnly) == 0);
        assert(owner->writeDraft && owner->apply && owner->cancel);
    }
    for (const auto& target : HotasBindingCatalog::kTargets) {
        const auto* owner = FindPageForControl(pages, pageCount, target.controlId);
        assert(owner && std::string_view(owner->pageId) == target.pageId);
        const auto* control = FindControl(*owner, target.controlId);
        assert(control && control->kind == ControlKind::InputBinding);
        assert((control->flags & kBindingController) != 0);
        assert((control->flags & kBindingClearable) != 0);
        assert((control->flags & kControlReadOnly) == 0);
        assert(owner->writeDraft && owner->apply && owner->cancel);
        assert(owner->beginBindingCapture && owner->pollBindingCapture &&
               owner->cancelBindingCapture && owner->reassignBinding);
    }
    std::size_t routeStatusCount{};
    std::size_t selectableRouteCount{};
    std::size_t fixedRouteCount{};
    for (const auto& target : HotasBindingCatalog::kTargets) {
        if (target.family != HotasBindingCatalog::TargetFamily::ShipAction) continue;
        const auto routeId = std::string(target.controlId) + "-route";
        const auto* control = FindControl(pages[1], routeId);
        const auto* action = FindShipAction(target.actionId);
        assert(action);
        assert(control);
        ValueV1 routeValue{};
        assert(pages[1].readValue(
                   pages[1].context, routeId.c_str(), &routeValue) == Result::Ok);
        assert(control->kind == ControlKind::Choice);
        assert(routeValue.kind == ValueKind::Integer);
        if (HasSelectableShipControlRoute(*action)) {
            assert((control->flags & kControlReadOnly) == 0);
            assert(routeValue.integerValue ==
                (action->recommendedMethod == ShipControlMethod::Direct ? 0 : 1));
            ++selectableRouteCount;
        } else {
            assert((control->flags & kControlReadOnly) != 0);
            assert(routeValue.integerValue == 1);
            ++fixedRouteCount;
        }
        std::array<ChoiceOptionV1, 2> choices{};
        std::uint32_t choiceCount{};
        assert(pages[1].readChoiceOptions(
                   pages[1].context, routeId.c_str(), choices.data(),
                   static_cast<std::uint32_t>(choices.size()),
                   &choiceCount) == Result::Ok);
        assert(choiceCount == 2);
        assert(std::string_view(choices[0].label) ==
            (HasSelectableShipControlRoute(*action)
                ? "Direct function" : "Direct injection unavailable"));
        assert(std::string_view(choices[1].label) ==
            (!HasSelectableShipControlRoute(*action)
                ? "Fixed ship-context SendInput"
                : AlternateShipControlMethod(*action) == ShipControlMethod::Context
                    ? "Ship-context SendInput E"
                    : "SendInput key / mouse"));
        ++routeStatusCount;
    }
    assert(routeStatusCount == 23);
    assert(selectableRouteCount == 18);
    assert(fixedRouteCount == 5);
    assert(pages[1].controlCount < kMaximumChoiceOptions);
    for (const auto id : {"profile-operation-name", "macro-name"}) {
        const auto* owner = FindPageForControl(pages, pageCount, id);
        assert(owner);
        const auto* text = FindControl(*owner, id);
        assert(text && text->kind == ControlKind::TextInput);
        assert(text->minimumValue == 0.0);
        assert(text->maximumValue == kStringValueCapacity - 1);
        assert(text->stepValue == 1.0);
    }
    const auto& devicePage = pages[6];
    assert(devicePage.controlCount == 14);
    assert(devicePage.writeDraft && devicePage.invokeAction &&
           devicePage.readRecordItems);
    assert(devicePage.apply && devicePage.cancel);
    const auto* deviceRecords = FindControl(devicePage, "device-selection");
    const auto* deviceClear = FindControl(
        devicePage, "clear-device-calibration");
    const auto* deviceReassign = FindControl(
        devicePage, "reassign-duplicate-device");
    assert(deviceRecords &&
           deviceRecords->kind == ControlKind::RecordCollection &&
           (deviceRecords->flags & kControlTransientSelection) != 0);
    assert(deviceClear && deviceReassign &&
           (deviceClear->flags & kControlRequiresConfirmation) != 0 &&
           (deviceReassign->flags & kControlRequiresConfirmation) != 0);
    assert(pages[1].writeDraft && pages[1].apply && pages[1].cancel);
    assert(pages[2].writeDraft && pages[2].apply && pages[2].cancel);
    assert(pages[2].readChoiceOptions);
    assert(pages[3].writeDraft && pages[3].apply && pages[3].cancel);
    assert(pages[4].writeDraft && pages[4].apply && pages[4].cancel);
    assert(pages[4].invokeAction && pages[4].readChoiceOptions &&
           pages[4].readRecordItems);
    assert(pages[5].writeDraft && pages[5].invokeAction && pages[5].apply &&
           pages[5].cancel && pages[5].readChoiceOptions &&
           pages[5].readRecordItems && pages[5].beginBindingCapture);
    assert(FindControl(pages[5], "macro-records"));
    assert(FindControl(pages[5], "macro-step-records"));
    assert(FindControl(pages[5], "macro-target-records"));
    assert(FindControl(pages[5], "macro-step-amount"));
    assert(FindControl(pages[1], "shortcut-records"));
    assert(FindControl(pages[1], "shortcut-output"));
    const auto* throttleSummary = FindControl(
        pages[0], "flight-throttle-summary");
    const auto* bindingsLink = FindControl(pages[0], "flight-open-bindings");
    const auto* throttleLink = FindControl(pages[0], "flight-open-throttle");
    const auto* macroLink = FindControl(pages[1], "shortcut-macro-link");
    assert(throttleSummary &&
           (throttleSummary->flags & kControlReadOnly) != 0);
    assert(throttleLink && throttleLink->kind == ControlKind::Action &&
           pages[0].invokeAction);
    assert(bindingsLink && bindingsLink->kind == ControlKind::Action);
    assert(macroLink && macroLink->kind == ControlKind::Action &&
           pages[1].invokeAction);
    assert(!FindControl(pages[0], "rate-throttle-enabled"));
    const auto* profileRecords = FindControl(pages[4], "profile-records");
    assert(profileRecords && profileRecords->kind == ControlKind::RecordCollection);
    assert((profileRecords->flags & kControlTransientSelection) != 0);
    const auto* profileImport = FindControl(pages[4], "profile-import-full");
    const auto* profileReset = FindControl(pages[4], "profile-reset-main");
    assert(profileImport && profileReset);
    assert((profileImport->flags & kControlRequiresConfirmation) != 0);
    assert((profileReset->flags & kControlRequiresConfirmation) != 0);

    std::unordered_set<std::string_view> catalogIds;
    for (std::size_t index = 0;
         index < AbsoluteControlSettings::Definitions().size(); ++index) {
        const auto& definition = AbsoluteControlSettings::Definitions()[index];
        assert(static_cast<std::size_t>(definition.field) == index);
        assert(catalogIds.insert(definition.controlId).second);
        assert(!definition.section.empty() && !definition.key.empty());
        assert(definition.minimum <= definition.defaultValue);
        assert(definition.defaultValue <= definition.maximum);
    }
    std::string catalogError;
    auto invalidCatalogState = AbsoluteControlSettings::DefaultState();
    assert(AbsoluteControlSettings::Validate(invalidCatalogState, catalogError));
    AbsoluteControlSettings::SetFloat(
        invalidCatalogState,
        AbsoluteControlSettings::ScalarField::MenuReleaseThreshold,
        AbsoluteControlSettings::GetFloat(
            invalidCatalogState,
            AbsoluteControlSettings::ScalarField::MenuEngageThreshold));
    assert(!AbsoluteControlSettings::Validate(invalidCatalogState, catalogError));
    assert(!catalogError.empty());

    for (std::size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        for (std::uint32_t controlIndex = 0;
             controlIndex < pages[pageIndex].controlCount; ++controlIndex) {
            const std::string_view id =
                pages[pageIndex].controls[controlIndex].controlId;
            assert(id.find("power") == std::string_view::npos);
            assert(id.find("head") == std::string_view::npos);
            assert(id.find("mouse") == std::string_view::npos);
            assert(id.find("hosam") == std::string_view::npos);
            assert(id.find("alignment") == std::string_view::npos);
        }
    }
    assert(AbsoluteControlSubscriber::Testing::ValidateDescriptors(pages, pageCount) ==
           Result::Ok);

    std::array<PageDescriptorV1, kExpectedPageIds.size()> invalidPages{};
    std::copy_n(pages, invalidPages.size(), invalidPages.begin());
    std::array<std::vector<ControlDescriptorV1>, kExpectedPageIds.size()> copiedControls;
    for (std::size_t index = 0; index < invalidPages.size(); ++index) {
        copiedControls[index].assign(
            pages[index].controls, pages[index].controls + pages[index].controlCount);
        invalidPages[index].controls = copiedControls[index].data();
    }
    strcpy_s(copiedControls[7][3].controlId, copiedControls[8][0].controlId);
    assert(AbsoluteControlSubscriber::Testing::ValidateDescriptors(
               invalidPages.data(), invalidPages.size()) == Result::Duplicate);
    copiedControls[7][3] = pages[7].controls[3];
    const auto profileText = std::ranges::find_if(copiedControls[4],
        [](const ControlDescriptorV1& control) {
            return std::string_view(control.controlId) == "profile-operation-name";
        });
    assert(profileText != copiedControls[4].end());
    profileText->maximumValue = 0.0;
    assert(AbsoluteControlSubscriber::Testing::ValidateDescriptors(
               invalidPages.data(), invalidPages.size()) == Result::InvalidArgument);
    *profileText = *FindControl(pages[4], "profile-operation-name");
    copiedControls[8][0].kind = static_cast<ControlKind>(99);
    assert(AbsoluteControlSubscriber::Testing::ValidateDescriptors(
               invalidPages.data(), invalidPages.size()) == Result::InvalidArgument);

    ResetFakeHost();
    g_resolvedApi = nullptr;
    assert(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::NotFound);
    assert(!AbsoluteControlSubscriber::IsHosted());

    ResetFakeHost();
    ApiV1 olderApi = g_api;
    olderApi.structSize = kApiV1BaseSize;
    g_resolvedApi = &olderApi;
    assert(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Rejected);
    assert(!AbsoluteControlSubscriber::IsHosted());
    assert(g_moduleCalls == 0);
    assert(g_pageCalls == 0);

    ResetFakeHost();
    ApiV1 capturelessApi = g_api;
    capturelessApi.capabilities = kCapabilityLabeledChoices;
    g_resolvedApi = &capturelessApi;
    assert(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Ok);
    assert(AbsoluteControlSubscriber::IsHosted());
    assert(g_pageCalls == kExpectedPageIds.size());
    for (const auto& page : g_copiedPages) {
        assert(page.beginBindingCapture == nullptr);
        assert(page.pollBindingCapture == nullptr);
        assert(page.cancelBindingCapture == nullptr);
        assert(page.reassignBinding == nullptr);
    }
    for (const auto& target : HotasBindingCatalog::kTargets) {
        assert(FindPageForControl(g_copiedPages.data(), g_copiedPages.size(),
                                  target.controlId) == nullptr);
    }
    assert(FindControl(g_copiedPages[6], "devices-scope"));
    assert(!FindControl(g_copiedPages[6], "device-selection"));
    assert(g_copiedPages[6].readRecordItems == nullptr);
    assert(g_copiedPages[6].invokeAction == nullptr);
    assert(!AbsoluteControlSubscriber::RequestHostPage("hotas-setup"));
    assert(g_openPageCalls == 0);

    // A pre-command ABI-v1 host can still register every existing page. Its
    // shorter table must never be read past the advertised struct size.
    ResetFakeHost();
    ApiV1 preOpenCommandApi = g_api;
    preOpenCommandApi.structSize =
        static_cast<std::uint32_t>(offsetof(ApiV1, requestOpenPage));
    g_resolvedApi = &preOpenCommandApi;
    assert(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Ok);
    assert(!AbsoluteControlSubscriber::RequestHostPage("hotas-setup"));
    assert(g_openPageCalls == 0);
    assert(!FindControl(g_copiedPages[0], "flight-throttle-summary"));
    assert(!FindControl(g_copiedPages[0], "flight-open-bindings"));
    assert(!FindControl(g_copiedPages[0], "flight-open-throttle"));
    const auto* fallbackMacroLink = FindControl(
        g_copiedPages[1], "shortcut-macro-link");
    assert(fallbackMacroLink &&
           fallbackMacroLink->kind == ControlKind::InputBinding &&
           (fallbackMacroLink->flags & kControlReadOnly) != 0);
    assert(g_copiedPages[1].invokeAction(
               g_copiedPages[1].context, "shortcut-macro-link") ==
           Result::NotFound);

    // Record collections and confirmed actions do not depend on provider input
    // capture. A host with the compound-control tail receives the full device
    // page while binding/profile/macro pages retain their captureless fallbacks.
    ResetFakeHost();
    ApiV1 compoundCapturelessApi = g_api;
    compoundCapturelessApi.capabilities =
        kCapabilityLabeledChoices | kCapabilityRecordCollections |
        kCapabilityActionConfirmation;
    g_resolvedApi = &compoundCapturelessApi;
    assert(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Ok);
    assert(FindControl(g_copiedPages[6], "device-selection"));
    assert(g_copiedPages[6].readRecordItems);
    assert(g_copiedPages[6].invokeAction);
    assert(g_copiedPages[6].writeDraft);
    assert(!g_copiedPages[6].apply && !g_copiedPages[6].cancel);
    for (const auto& page : g_copiedPages) {
        assert(page.beginBindingCapture == nullptr);
    }

    // A host may support static provider capture without the later selected-
    // record/confirmation tail. Keep binding pages native while only Profiles
    // falls back to its read-only status row.
    ResetFakeHost();
    ApiV1 recordlessCaptureApi = g_api;
    recordlessCaptureApi.capabilities = kCapabilityLabeledChoices |
        kCapabilityProviderBindingCapture |
        kCapabilityBindingConflictResolution;
    g_resolvedApi = &recordlessCaptureApi;
    assert(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Ok);
    assert(g_copiedPages[0].beginBindingCapture != nullptr);
    assert(g_copiedPages[4].controlCount == 1);
    assert(FindControl(g_copiedPages[4], "profiles-scope"));
    assert(g_copiedPages[4].readRecordItems == nullptr);
    assert(g_copiedPages[4].beginBindingCapture == nullptr);

    ResetFakeHost();
    g_rejectPageIndex = 4;
    assert(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Rejected);
    assert(!AbsoluteControlSubscriber::IsHosted());
    assert(g_moduleCalls == 1);
    assert(g_pageCalls == 5);
    assert(g_unregisterCalls == 1);
    assert(g_copiedModule.moduleId[0] == '\0');
    for (const auto& copiedPage : g_copiedPages) {
        assert(copiedPage.pageId[0] == '\0');
    }

    ResetFakeHost();
    g_behavior = HostBehavior::DuplicateModule;
    g_rejectPageIndex = 2;
    assert(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Rejected);
    assert(!AbsoluteControlSubscriber::IsHosted());
    assert(g_moduleCalls == 1);
    assert(g_pageCalls == 3);
    assert(g_unregisterCalls == 0);

    ResetFakeHost();
    ApiV1 missingCapabilityApi = g_api;
    missingCapabilityApi.capabilities = kCapabilityStructuredLayout |
                                        kCapabilityProviderBindingCapture;
    g_resolvedApi = &missingCapabilityApi;
    assert(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Rejected);
    assert(!AbsoluteControlSubscriber::IsHosted());
    assert(g_moduleCalls == 0);
    assert(g_pageCalls == 0);

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
    assert(g_pageCalls == kExpectedPageIds.size());
    assert(std::strcmp(g_copiedModule.moduleId, "absolute.hotas") == 0);
    for (std::size_t index = 0; index < kExpectedPageIds.size(); ++index) {
        assert(std::strcmp(g_copiedPages[index].pageId,
                           kExpectedPageIds[index]) == 0);
    }

    g_hostOpen = true;
    g_hostCapture = true;
    assert(AbsoluteControlSubscriber::IsHostOpen());
    assert(AbsoluteControlSubscriber::IsHostInputCaptureActive());
    g_hostOpen = false;
    g_hostCapture = false;

    assert(AbsoluteControlSubscriber::RequestHostPage("hotas-setup"));
    assert(g_openPageCalls == 1);
    assert(g_openModule == "absolute.hotas");
    assert(g_openPage == "hotas-setup");
    assert(g_copiedPages[0].invokeAction(
               g_copiedPages[0].context, "flight-open-throttle") == Result::Ok);
    assert(g_openPageCalls == 2 && g_openPage == "hotas-throttle");
    assert(g_copiedPages[0].invokeAction(
               g_copiedPages[0].context, "flight-open-bindings") == Result::Ok);
    assert(g_openPageCalls == 3 && g_openPage == "hotas-ship-buttons");
    assert(g_copiedPages[1].invokeAction(
               g_copiedPages[1].context, "shortcut-macro-link") == Result::Ok);
    assert(g_openPageCalls == 4 && g_openPage == "hotas-macros");
    g_openPageResult = Result::Rejected;
    assert(!AbsoluteControlSubscriber::RequestHostPage("hotas-diagnostics"));
    assert(g_openPageCalls == 5);
    assert(g_openPage == "hotas-diagnostics");
    assert(!AbsoluteControlSubscriber::RequestHostPage(nullptr));
    assert(g_openPageCalls == 5);

    const auto& axesPage = g_copiedPages[0];
    const auto& shipPage = g_copiedPages[1];
    const auto& aimingPage = g_copiedPages[3];
    const auto& profilesPage = g_copiedPages[4];

    ValueV1 throttleSummaryValue{};
    assert(axesPage.readValue(
               axesPage.context, "flight-throttle-summary",
               &throttleSummaryValue) == Result::Ok);
    assert(throttleSummaryValue.kind == ValueKind::String);
    assert(std::string_view{ throttleSummaryValue.stringValue }.find(
               "edited once on Throttle Setup") != std::string_view::npos);

    std::array<RecordItemV1, kMaximumRecordItems> profileItems{};
    std::uint32_t profileCount{};
    assert(profilesPage.readRecordItems(
               profilesPage.context, "profile-records", profileItems.data(), 2,
               &profileCount) == Result::CapacityExceeded);
    assert(profileCount == 3);
    assert(profilesPage.readRecordItems(
               profilesPage.context, "profile-records", profileItems.data(),
               static_cast<std::uint32_t>(profileItems.size()), &profileCount) ==
           Result::Ok);
    assert(std::strcmp(profileItems[0].recordId, "main-controls") == 0);
    assert(std::string_view(profileItems[1].summary).find("inherits Main") !=
           std::string_view::npos);

    ValueV1 profileValue{};
    assert(profilesPage.readValue(
               profilesPage.context, "profile-records", &profileValue) == Result::Ok);
    assert(std::strcmp(profileValue.stringValue, "main-controls") == 0);
    ValueV1 profileEdit{};
    profileEdit.kind = ValueKind::String;
    strcpy_s(profileEdit.stringValue, profileItems[1].recordId);
    assert(profilesPage.writeDraft(
               profilesPage.context, "profile-records", &profileEdit) == Result::Ok);
    profileValue = {};
    assert(profilesPage.readValue(
               profilesPage.context, "profile-records", &profileValue) == Result::Ok);
    assert(std::strcmp(profileValue.stringValue, profileItems[1].recordId) == 0);

    profileEdit = {};
    profileEdit.kind = ValueKind::Integer;
    profileEdit.integerValue = 2;
    assert(profilesPage.writeDraft(profilesPage.context,
               "profile-activation-mode", &profileEdit) == Result::Ok);
    assert(profilesPage.beginBindingCapture(profilesPage.context,
               "profile-activation-trigger") == Result::Ok);
    assert(TestCapture::beginSlot == CaptureSlot::kProfileTrigger);
    assert(TestCapture::settleMilliseconds == 300);
    assert(profilesPage.cancelBindingCapture(profilesPage.context,
               "profile-activation-trigger") == Result::Ok);

    // Dirty selection remains on the current record until one of the explicit
    // save/discard/stay actions resolves the legacy Wizard target switch.
    profileEdit = {};
    profileEdit.kind = ValueKind::String;
    strcpy_s(profileEdit.stringValue, profileItems[2].recordId);
    assert(profilesPage.writeDraft(
               profilesPage.context, "profile-records", &profileEdit) == Result::Ok);
    profileValue = {};
    assert(profilesPage.readValue(
               profilesPage.context, "profile-records", &profileValue) == Result::Ok);
    assert(std::strcmp(profileValue.stringValue, profileItems[1].recordId) == 0);
    assert(profilesPage.invokeAction(
               profilesPage.context, "profile-switch-stay") == Result::Ok);
    assert(profilesPage.apply(profilesPage.context) == Result::Ok);
    assert(TestProfiles::repository.updates.size() == 1);
    assert(TestProfiles::repository.updates.front().profile == "Flight Aux");

    // Ordinary assignment detects a collision; reassign atomically clears the
    // previous activation owner within this page draft.
    profileEdit = {};
    profileEdit.kind = ValueKind::String;
    strcpy_s(profileEdit.stringValue, "8");
    assert(profilesPage.writeDraft(profilesPage.context,
               "profile-activation-trigger", &profileEdit) == Result::Duplicate);
    assert(profilesPage.reassignBinding(profilesPage.context,
               "profile-activation-trigger", "8") == Result::Ok);
    assert(profilesPage.apply(profilesPage.context) == Result::Ok);

    // Every rich page exposes the same pinned profile/layer context. Switching
    // here changes the actual Wizard edit target, activation fields share the
    // Profiles draft, and a dirty settings page cannot be silently abandoned.
    std::array<RecordItemV1, kMaximumRecordItems> pinnedProfileItems{};
    std::uint32_t pinnedProfileCount{};
    assert(axesPage.readRecordItems(axesPage.context,
               "flight-axes-edit-profile", pinnedProfileItems.data(),
               static_cast<std::uint32_t>(pinnedProfileItems.size()),
               &pinnedProfileCount) == Result::Ok);
    assert(pinnedProfileCount == profileCount);
    profileValue = {};
    assert(axesPage.readValue(axesPage.context, "flight-axes-edit-profile",
               &profileValue) == Result::Ok);
    assert(std::strcmp(profileValue.stringValue,
               profileItems[1].recordId) == 0);

    ValueV1 pinnedAxisEdit{};
    pinnedAxisEdit.kind = ValueKind::Boolean;
    pinnedAxisEdit.booleanValue = 1;
    assert(axesPage.writeDraft(axesPage.context, "pitch-inverted",
               &pinnedAxisEdit) == Result::Ok);
    profileEdit = {};
    profileEdit.kind = ValueKind::String;
    strcpy_s(profileEdit.stringValue, profileItems[2].recordId);
    assert(axesPage.writeDraft(axesPage.context, "flight-axes-edit-profile",
               &profileEdit) == Result::Rejected);
    assert(TestProfiles::repository.current == "Flight Aux");
    axesPage.cancel(axesPage.context);

    assert(axesPage.writeDraft(axesPage.context, "flight-axes-edit-profile",
               &profileEdit) == Result::Ok);
    assert(TestProfiles::repository.current == "Independent");
    assert(TestSettings::editTarget == "Independent");
    profileEdit = {};
    profileEdit.kind = ValueKind::Integer;
    profileEdit.integerValue = 1;
    assert(axesPage.writeDraft(axesPage.context, "flight-axes-layer-mode",
               &profileEdit) == Result::Ok);
    profileEdit = {};
    profileEdit.kind = ValueKind::String;
    strcpy_s(profileEdit.stringValue, "10");
    assert(axesPage.writeDraft(axesPage.context,
               "flight-axes-layer-modifier", &profileEdit) == Result::Ok);
    assert(axesPage.beginBindingCapture(axesPage.context,
               "flight-axes-layer-modifier") == Result::Ok);
    assert(TestCapture::beginSlot == CaptureSlot::kProfileTrigger);
    assert(TestCapture::settleMilliseconds == 50);
    assert(axesPage.cancelBindingCapture(axesPage.context,
               "flight-axes-layer-modifier") == Result::Ok);

    pinnedAxisEdit = {};
    pinnedAxisEdit.kind = ValueKind::Float;
    pinnedAxisEdit.floatValue = 1.25;
    assert(axesPage.writeDraft(axesPage.context, "yaw-sensitivity",
               &pinnedAxisEdit) == Result::Ok);
    assert(axesPage.apply(axesPage.context) == Result::Ok);
    assert(TestSettings::editTarget == "Independent");
    const auto independentUpdate = std::find_if(
        TestProfiles::repository.updates.begin(),
        TestProfiles::repository.updates.end(), [](const auto& update) {
            return update.profile == "Independent";
        });
    assert(independentUpdate != TestProfiles::repository.updates.end());
    assert(independentUpdate->mode == "toggle");
    assert(independentUpdate->trigger == "10");

    const auto* throttleBinding =
        HotasBindingCatalog::Find("bind-throttle-axis");
    assert(throttleBinding);
    assert(axesPage.beginBindingCapture(
               axesPage.context, throttleBinding->controlId.data()) == Result::Ok);
    assert(TestCapture::beginSlot == throttleBinding->captureSlot);
    BindingCaptureV1 capture{};
    assert(axesPage.pollBindingCapture(
               axesPage.context, throttleBinding->controlId.data(), &capture) ==
           Result::Ok);
    assert(capture.state == BindingCaptureState::Capturing);
    assert(axesPage.pollBindingCapture(
               aimingPage.context, throttleBinding->controlId.data(), &capture) ==
           Result::InvalidArgument);
    TestCapture::nextState = HotasBindingCapture::PollState::Captured;
    TestCapture::nextBinding = "Test Stick@0x32";
    capture = {};
    assert(axesPage.pollBindingCapture(
               axesPage.context, throttleBinding->controlId.data(), &capture) ==
           Result::Ok);
    assert(capture.state == BindingCaptureState::Captured);
    assert(std::strcmp(capture.binding, "Test Stick@0x32") == 0);
    ValueV1 bindingEdit{};
    bindingEdit.kind = ValueKind::String;
    strcpy_s(bindingEdit.stringValue, capture.binding);
    assert(axesPage.writeDraft(
               axesPage.context, throttleBinding->controlId.data(), &bindingEdit) ==
           Result::Ok);
    ValueV1 bindingValue{};
    assert(axesPage.readValue(
               axesPage.context, throttleBinding->controlId.data(), &bindingValue) ==
           Result::Ok);
    assert(std::strcmp(bindingValue.stringValue, "Test Stick@0x32") == 0);
    axesPage.cancel(axesPage.context);
    bindingValue = {};
    assert(axesPage.readValue(
               axesPage.context, throttleBinding->controlId.data(), &bindingValue) ==
           Result::Ok);
    assert(std::strcmp(bindingValue.stringValue, "(unbound)") == 0);

    TestCapture::nextState = HotasBindingCapture::PollState::Capturing;
    assert(axesPage.beginBindingCapture(
               axesPage.context, throttleBinding->controlId.data()) == Result::Ok);
    bindingEdit = {};
    bindingEdit.kind = ValueKind::String;
    strcpy_s(bindingEdit.stringValue, "Test Stick@0x31");
    assert(axesPage.writeDraft(
               axesPage.context, throttleBinding->controlId.data(), &bindingEdit) ==
           Result::Ok);
    assert(axesPage.apply(axesPage.context) == Result::Ok);
    capture = {};
    assert(axesPage.pollBindingCapture(
               axesPage.context, throttleBinding->controlId.data(), &capture) ==
           Result::Ok);
    assert(capture.state == BindingCaptureState::Cancelled);
    bindingEdit = {};
    bindingEdit.kind = ValueKind::String;
    strcpy_s(bindingEdit.stringValue, "(unbound)");
    assert(axesPage.writeDraft(
               axesPage.context, throttleBinding->controlId.data(), &bindingEdit) ==
           Result::Ok);
    assert(axesPage.apply(axesPage.context) == Result::Ok);

    const auto* previousBinding =
        HotasBindingCatalog::Find("bind-ship-fire-boosters");
    const auto* targetBinding =
        HotasBindingCatalog::Find("bind-ship-switch-flight-modes");
    assert(previousBinding && targetBinding);
    bindingEdit = {};
    bindingEdit.kind = ValueKind::String;
    strcpy_s(bindingEdit.stringValue, "Test Throttle@7");
    assert(shipPage.writeDraft(shipPage.context,
               previousBinding->controlId.data(), &bindingEdit) == Result::Ok);
    TestCapture::nextState = HotasBindingCapture::PollState::Captured;
    TestCapture::nextBinding = "Test Throttle@7";
    assert(shipPage.beginBindingCapture(
               shipPage.context, targetBinding->controlId.data()) == Result::Ok);
    capture = {};
    assert(shipPage.pollBindingCapture(
               shipPage.context, targetBinding->controlId.data(), &capture) ==
           Result::Ok);
    assert(capture.state == BindingCaptureState::Captured);
    bindingEdit = {};
    bindingEdit.kind = ValueKind::String;
    strcpy_s(bindingEdit.stringValue, capture.binding);
    assert(shipPage.writeDraft(shipPage.context,
               targetBinding->controlId.data(), &bindingEdit) == Result::Duplicate);
    assert(shipPage.reassignBinding(
               shipPage.context, targetBinding->controlId.data(),
               capture.binding) == Result::Ok);
    bindingValue = {};
    assert(shipPage.readValue(shipPage.context,
               previousBinding->controlId.data(), &bindingValue) == Result::Ok);
    assert(std::strcmp(bindingValue.stringValue, "(unbound)") == 0);
    bindingValue = {};
    assert(shipPage.readValue(shipPage.context,
               targetBinding->controlId.data(), &bindingValue) == Result::Ok);
    assert(std::strcmp(bindingValue.stringValue, "Test Throttle@7") == 0);
    assert(shipPage.apply(shipPage.context) == Result::Ok);
    assert(TestSettings::storedBindings[
               HotasBindingCatalog::IndexOf(*targetBinding)] ==
           "Test Throttle@7");

    // Output method is part of the same provider-owned transaction as the
    // controller binding. Select / Accept retains its context-aware E default
    // but can opt into the validated native Select Target function.
    const std::string boostRouteId =
        std::string(previousBinding->controlId) + "-route";
    ValueV1 routeEdit{};
    routeEdit.kind = ValueKind::Integer;
    routeEdit.integerValue = 1;
    assert(shipPage.writeDraft(shipPage.context, boostRouteId.c_str(),
                               &routeEdit) == Result::Ok);
    assert(shipPage.writeDraft(axesPage.context, boostRouteId.c_str(),
                               &routeEdit) == Result::InvalidArgument);
    const auto* contextBinding =
        HotasBindingCatalog::Find("bind-ship-select-accept");
    assert(contextBinding);
    const std::string contextRouteId =
        std::string(contextBinding->controlId) + "-route";
    ValueV1 directSelectEdit{};
    directSelectEdit.kind = ValueKind::Integer;
    directSelectEdit.integerValue = 0;
    assert(shipPage.writeDraft(shipPage.context, contextRouteId.c_str(),
                               &directSelectEdit) == Result::Ok);
    assert(shipPage.apply(shipPage.context) == Result::Ok);
    const auto* boostAction = FindShipAction(previousBinding->actionId);
    assert(boostAction);
    assert(TestSettings::storedRoutes[static_cast<std::size_t>(
               boostAction - kShipActionCatalog.data())] ==
           ShipControlMethod::KeyboardCompatibility);
    const auto* selectAction = FindShipAction(contextBinding->actionId);
    assert(selectAction);
    assert(TestSettings::storedRoutes[static_cast<std::size_t>(
               selectAction - kShipActionCatalog.data())] ==
           ShipControlMethod::Direct);
    ValueV1 routeReadBack{};
    assert(shipPage.readValue(shipPage.context, boostRouteId.c_str(),
                              &routeReadBack) == Result::Ok);
    assert(routeReadBack.kind == ValueKind::Integer &&
           routeReadBack.integerValue == 1);

    TestCapture::nextState = HotasBindingCapture::PollState::Capturing;
    assert(axesPage.beginBindingCapture(
               axesPage.context, throttleBinding->controlId.data()) == Result::Ok);
    assert(axesPage.cancelBindingCapture(
               axesPage.context, throttleBinding->controlId.data()) == Result::Ok);
    capture = {};
    assert(axesPage.pollBindingCapture(
               axesPage.context, throttleBinding->controlId.data(), &capture) ==
           Result::Ok);
    assert(capture.state == BindingCaptureState::Idle);

    ValueV1 value{};
    assert(g_copiedPages[8].readValue(
               g_copiedPages[8].context,
               g_copiedPages[8].controls[0].controlId, &value) == Result::Ok);
    assert(value.kind == ValueKind::String);
    assert(value.stringValue[0] != '\0');

    assert(FindControl(axesPage, "flight-controls-enabled"));
    value = {};
    assert(axesPage.readValue(axesPage.context, "flight-controls-enabled", &value) == Result::Ok);
    assert(value.kind == ValueKind::Boolean && value.booleanValue == 1);

    ValueV1 edit = {};
    edit.kind = ValueKind::Boolean;
    edit.booleanValue = 0;
    assert(axesPage.writeDraft(axesPage.context, "flight-controls-enabled", &edit) == Result::Ok);
    value = {};
    assert(axesPage.readValue(axesPage.context, "flight-controls-enabled", &value) == Result::Ok);
    assert(value.booleanValue == 0);
    assert(AbsoluteControlSettings::GetBoolean(
        TestSettings::stored,
        AbsoluteControlSettings::ScalarField::FlightControlsEnabled));
    assert(axesPage.apply(axesPage.context) == Result::Ok);
    assert(!AbsoluteControlSettings::GetBoolean(
        TestSettings::stored,
        AbsoluteControlSettings::ScalarField::FlightControlsEnabled));

    edit = {};
    edit.kind = ValueKind::Float;
    edit.floatValue = 2.0;
    assert(axesPage.writeDraft(axesPage.context, "pitch-sensitivity", &edit) == Result::Ok);
    axesPage.cancel(axesPage.context);
    value = {};
    assert(axesPage.readValue(axesPage.context, "pitch-sensitivity", &value) == Result::Ok);
    assert(value.kind == ValueKind::Float && value.floatValue == 1.0);

    edit = {};
    edit.kind = ValueKind::Integer;
    edit.integerValue = 2;
    assert(axesPage.writeDraft(axesPage.context, "pitch-sensitivity", &edit) ==
           Result::InvalidArgument);
    edit.kind = ValueKind::Float;
    edit.floatValue = 3.1;
    assert(axesPage.writeDraft(axesPage.context, "pitch-sensitivity", &edit) ==
           Result::InvalidArgument);

    const auto& throttlePage = g_copiedPages[2];
    assert(FindControl(throttlePage, "throttle-landmark-guide")->kind ==
           ControlKind::GroupHeader);
    assert(FindControl(throttlePage, "throttle-positional-zones")->kind ==
           ControlKind::GroupHeader);
    assert(FindControl(throttlePage, "throttle-rate-mode")->kind ==
           ControlKind::GroupHeader);
    for (const auto* controlId : {
             "throttle-detent-center", "reverse-zone-center",
             "boost-zone-center"}) {
        assert((FindControl(throttlePage, controlId)->flags &
                kControlAdvanced) != 0);
    }
    for (const auto* actionId : {
             "throttle-capture-detent", "throttle-capture-reverse",
             "throttle-capture-boost", "throttle-link-idle-saturation"}) {
        const auto* action = FindControl(throttlePage, actionId);
        assert(action && action->kind == ControlKind::Action);
        assert((action->flags & kControlMutatesDraft) != 0);
        if (std::string_view{actionId} != "throttle-link-idle-saturation") {
            assert((action->flags & kControlLayoutInline) != 0);
        }
    }
    assert(throttlePage.invokeAction);
    assert(throttlePage.invokeAction(
               throttlePage.context, "throttle-capture-detent") == Result::Ok);
    assert(AbsoluteControlTelemetry::GetThrottleCaptureTarget() ==
        AbsoluteControlTelemetry::ThrottleCaptureTarget::Detent);
    value = {};
    assert(throttlePage.readValue(
               throttlePage.context, "throttle-scope", &value) == Result::Ok);
    assert(std::string_view{value.stringValue}.find("LIVE SET") !=
        std::string_view::npos);

    AbsoluteControlTelemetry::ThrottleSample currentThrottle;
    currentThrottle.logicalRawPosition = 41234;
    currentThrottle.logicalRawAvailable = true;
    AbsoluteControlTelemetry::PublishThrottle(currentThrottle);
    // Tracking previews the live landmark; the draft changes only when the
    // same action is pressed again (or Apply finalizes the gesture).
    value = {};
    assert(throttlePage.readValue(
               throttlePage.context, "throttle-detent-center", &value) == Result::Ok);
    assert(value.integerValue != 41234);
    assert(throttlePage.invokeAction(
               throttlePage.context, "throttle-capture-detent") == Result::Ok);
    assert(AbsoluteControlTelemetry::GetThrottleCaptureTarget() ==
        AbsoluteControlTelemetry::ThrottleCaptureTarget::None);
    value = {};
    assert(throttlePage.readValue(
               throttlePage.context, "throttle-detent-center", &value) == Result::Ok);
    assert(value.integerValue == 41234);
    assert(AbsoluteControlSettings::GetInteger(
               TestSettings::stored,
               AbsoluteControlSettings::ScalarField::DetentCenter) != 41234);
    throttlePage.cancel(throttlePage.context);

    // A direct graph drag writes the linked numeric control and cancels an
    // in-progress physical tracking gesture so the two editors never fight.
    assert(throttlePage.invokeAction(
               throttlePage.context, "throttle-capture-detent") == Result::Ok);
    edit = {};
    edit.kind = ValueKind::Integer;
    edit.integerValue = 36000;
    assert(throttlePage.writeDraft(
               throttlePage.context, "throttle-detent-center", &edit) == Result::Ok);
    assert(AbsoluteControlTelemetry::GetThrottleCaptureTarget() ==
        AbsoluteControlTelemetry::ThrottleCaptureTarget::None);
    throttlePage.cancel(throttlePage.context);

    edit = {};
    edit.kind = ValueKind::Float;
    edit.floatValue = 0.12;
    assert(throttlePage.writeDraft(
               throttlePage.context, "throttle-idle-plateau", &edit) == Result::Ok);
    assert(throttlePage.invokeAction(
               throttlePage.context, "throttle-capture-reverse") == Result::Ok);
    assert(throttlePage.invokeAction(
               throttlePage.context, "throttle-capture-reverse") == Result::Ok);
    assert(throttlePage.invokeAction(
               throttlePage.context, "throttle-capture-boost") == Result::Ok);
    assert(throttlePage.invokeAction(
               throttlePage.context, "throttle-link-idle-saturation") == Result::Ok);
    assert(AbsoluteControlTelemetry::GetThrottleCaptureTarget() ==
        AbsoluteControlTelemetry::ThrottleCaptureTarget::Boost);
    value = {};
    assert(throttlePage.readValue(
               throttlePage.context, "throttle-saturation", &value) == Result::Ok);
    assert(value.kind == ValueKind::Float && std::abs(value.floatValue - 0.88) < 1e-9);
    assert(throttlePage.apply(throttlePage.context) == Result::Ok);
    assert(AbsoluteControlTelemetry::GetThrottleCaptureTarget() ==
        AbsoluteControlTelemetry::ThrottleCaptureTarget::None);
    assert(AbsoluteControlSettings::GetInteger(
               TestSettings::stored,
               AbsoluteControlSettings::ScalarField::ReverseZoneCenter) == 41234);
    assert(AbsoluteControlSettings::GetInteger(
               TestSettings::stored,
               AbsoluteControlSettings::ScalarField::BoostZoneCenter) == 41234);

    edit = {};
    edit.kind = ValueKind::Integer;
    for (const auto rawLandmark : {32768LL, 32868LL, 32769LL}) {
        edit.integerValue = rawLandmark;
        assert(throttlePage.writeDraft(
                   throttlePage.context, "throttle-detent-center", &edit) == Result::Ok);
        value = {};
        assert(throttlePage.readValue(
                   throttlePage.context, "throttle-detent-center", &value) == Result::Ok);
        assert(value.kind == ValueKind::Integer);
        assert(value.integerValue == rawLandmark);
    }
    throttlePage.cancel(throttlePage.context);

    const auto& diagnosticsPage = g_copiedPages[7];
    edit = {};
    edit.kind = ValueKind::Integer;
    edit.integerValue = 501;
    assert(diagnosticsPage.writeDraft(
               diagnosticsPage.context, "pilot-latch-ms", &edit) == Result::InvalidArgument);
    std::array<ChoiceOptionV1, 3> options{};
    std::uint32_t optionCount{};
    assert(diagnosticsPage.readChoiceOptions(
               diagnosticsPage.context, "pilot-context-mode", options.data(),
               static_cast<std::uint32_t>(options.size()),
               &optionCount) == Result::Ok);
    assert(optionCount == 3);
    assert(options[1].value == 1);
    assert(std::strcmp(options[1].label, "Park flight controls") == 0);
    assert(diagnosticsPage.readChoiceOptions(
               diagnosticsPage.context, "pilot-context-mode", options.data(), 2, &optionCount) ==
           Result::CapacityExceeded);
    assert(optionCount == 3);

    assert(throttlePage.readChoiceOptions(
               throttlePage.context, "turn-assist-mode", options.data(),
               static_cast<std::uint32_t>(options.size()),
               &optionCount) == Result::Ok);
    assert(optionCount == 3);
    assert(options[2].value == 2);
    assert(std::strcmp(options[2].label, "Toggle on/off") == 0);

    auto expectedRoundTrip = TestSettings::stored;
    for (const auto& definition : AbsoluteControlSettings::Definitions()) {
        const auto* owner = FindPageForControl(
            g_copiedPages.data(), g_copiedPages.size(), definition.controlId);
        assert(owner && owner->writeDraft && owner->readValue);

        ValueV1 invalid{};
        switch (definition.type) {
        case AbsoluteControlSettings::ScalarType::Boolean:
            invalid.kind = ValueKind::Boolean;
            invalid.booleanValue = 2;
            break;
        case AbsoluteControlSettings::ScalarType::Integer:
        case AbsoluteControlSettings::ScalarType::Choice:
            invalid.kind = ValueKind::Integer;
            invalid.integerValue =
                static_cast<std::int64_t>(definition.minimum) - 1;
            break;
        case AbsoluteControlSettings::ScalarType::Float:
            invalid.kind = ValueKind::Float;
            invalid.floatValue = std::numeric_limits<double>::quiet_NaN();
            break;
        }
        assert(owner->writeDraft(owner->context, definition.controlId.data(), &invalid) ==
               Result::InvalidArgument);

        ValueV1 desired{};
        switch (definition.type) {
        case AbsoluteControlSettings::ScalarType::Boolean: {
            const bool next = !AbsoluteControlSettings::GetBoolean(
                expectedRoundTrip, definition.field);
            desired.kind = ValueKind::Boolean;
            desired.booleanValue = next ? 1 : 0;
            AbsoluteControlSettings::SetBoolean(
                expectedRoundTrip, definition.field, next);
            break;
        }
        case AbsoluteControlSettings::ScalarType::Integer:
        case AbsoluteControlSettings::ScalarType::Choice: {
            const auto minimum = static_cast<std::int64_t>(definition.minimum);
            const auto step = static_cast<std::int64_t>(definition.step);
            const auto current = AbsoluteControlSettings::GetInteger(
                expectedRoundTrip, definition.field);
            const auto next = current == minimum ? minimum + step : minimum;
            desired.kind = ValueKind::Integer;
            desired.integerValue = next;
            AbsoluteControlSettings::SetInteger(
                expectedRoundTrip, definition.field, next);
            break;
        }
        case AbsoluteControlSettings::ScalarType::Float: {
            double next = definition.minimum;
            if (definition.field ==
                AbsoluteControlSettings::ScalarField::MenuEngageThreshold) {
                next = definition.maximum;
            } else if (definition.field ==
                AbsoluteControlSettings::ScalarField::MenuReleaseThreshold) {
                next = definition.minimum;
            } else if (std::abs(AbsoluteControlSettings::GetFloat(
                           expectedRoundTrip, definition.field) - next) < 1e-9) {
                next += definition.step;
            }
            desired.kind = ValueKind::Float;
            desired.floatValue = next;
            AbsoluteControlSettings::SetFloat(
                expectedRoundTrip, definition.field, next);
            break;
        }
        }
        assert(owner->writeDraft(owner->context, definition.controlId.data(), &desired) ==
               Result::Ok);
        ValueV1 readBack{};
        assert(owner->readValue(
                   owner->context, definition.controlId.data(), &readBack) == Result::Ok);
        assert(readBack.kind == desired.kind);
        if (desired.kind == ValueKind::Boolean) {
            assert(readBack.booleanValue == desired.booleanValue);
        } else if (desired.kind == ValueKind::Integer) {
            assert(readBack.integerValue == desired.integerValue);
        } else {
            assert(std::abs(readBack.floatValue - desired.floatValue) < 1e-9);
        }
    }
    assert(axesPage.apply(axesPage.context) == Result::Ok);
    assert(AbsoluteControlSettings::Equivalent(
        TestSettings::stored, expectedRoundTrip));

    edit = {};
    edit.kind = ValueKind::Boolean;
    edit.booleanValue = 0;
    assert(axesPage.writeDraft(axesPage.context, "pitch-inverted", &edit) == Result::Ok);
    ++TestSettings::revision.sourceFingerprint;
    assert(axesPage.apply(axesPage.context) == Result::Rejected);
    axesPage.cancel(axesPage.context);
    value = {};
    assert(axesPage.readValue(axesPage.context, "pitch-inverted", &value) == Result::Ok);
    assert(value.booleanValue == 1);

    assert(axesPage.writeDraft(axesPage.context, "pitch-inverted", &edit) == Result::Ok);
    TestSettings::failApply = true;
    assert(axesPage.apply(axesPage.context) == Result::WriteFailure);
    TestSettings::failApply = false;
    value = {};
    assert(axesPage.readValue(axesPage.context, "pitch-inverted", &value) == Result::Ok);
    assert(value.booleanValue == 0);
    assert(axesPage.apply(axesPage.context) == Result::Ok);
    assert(!AbsoluteControlSettings::GetBoolean(
        TestSettings::stored,
        AbsoluteControlSettings::ScalarField::PitchInvert));

    AbsoluteControlSubscriber::Testing::ForceReadException(true);
    value = {};
    assert(g_copiedPages[8].readValue(
               g_copiedPages[8].context,
               "setup-control-host", &value) ==
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

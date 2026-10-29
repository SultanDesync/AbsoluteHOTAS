#include "AbsoluteControlSettings.h"
#include "AbsoluteControlMacros.h"
#include "AbsoluteControlProfiles.h"
#include "AbsoluteControlSubscriber.h"
#include "AbsoluteControlTelemetry.h"
#include "HotasBindingCapture.h"
#include "SFSEInterface.h"
#include "ShipOutput.h"
#include "SupportReport.h"

#include <algorithm>
#include <array>
#include "TestCheck.h"
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

namespace SupportReport {
bool failExport{};
std::string lastFrontend;
bool Export(std::string_view frontend) noexcept {
    lastFrontend = frontend;
    return !failExport;
}
std::string Status() { return failExport ? "Report failed" : "Report saved"; }
}

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
    REQUIRE(AbsoluteControlSubscriber::Testing::RegisterFlightAxesComposition(
               &g_compositionApi) == Result::Ok);
    REQUIRE(AbsoluteControlSubscriber::Testing::
        IsFlightAxesCompositionRegistered());
    REQUIRE(std::strcmp(g_copiedComposition.moduleId, "absolute.hotas") == 0);
    REQUIRE(std::strcmp(g_copiedComposition.pageId,
                       "hotas-flight-axes") == 0);
    REQUIRE(g_copiedComposition.nodeCount == 83);
    REQUIRE(g_copiedComposition.associationCount == 10);
    std::array<Composition::NodeStateV1, 9> compositionStates{};
    std::uint32_t compositionStateCount{};
    REQUIRE(g_copiedComposition.readNodeStates(
               g_copiedComposition.context, "absolute.hotas",
               "hotas-flight-axes", compositionStates.data(),
               static_cast<std::uint32_t>(compositionStates.size()),
               &compositionStateCount) == Result::Ok);
    REQUIRE(compositionStateCount == compositionStates.size());
    REQUIRE(std::strcmp(compositionStates[0].nodeId, "flight-summary") == 0);
    REQUIRE(std::strcmp(compositionStates[0].value, "0 / 6 AXES BOUND") == 0);
    REQUIRE(std::strcmp(compositionStates[1].value, "NEEDS BINDING") == 0);

    std::size_t pageCount{};
    const auto* pages = AbsoluteControlSubscriber::Testing::Pages(pageCount);
    ValueV1 compositionBindingEdit{};
    compositionBindingEdit.kind = ValueKind::String;
    strcpy_s(compositionBindingEdit.stringValue, "Test Stick@0x31");
    REQUIRE(pages[0].writeDraft(pages[0].context, "bind-throttle-axis",
               &compositionBindingEdit) == Result::Ok);
    compositionStates = {};
    compositionStateCount = 0;
    REQUIRE(g_copiedComposition.readNodeStates(
               g_copiedComposition.context, "absolute.hotas",
               "hotas-flight-axes", compositionStates.data(),
               static_cast<std::uint32_t>(compositionStates.size()),
               &compositionStateCount) == Result::Ok);
    REQUIRE(std::strcmp(compositionStates[0].value, "1 / 6 AXES BOUND") == 0);
    REQUIRE(std::strcmp(compositionStates[0].sourceLabel,
                       "UNAPPLIED DRAFT") == 0);
    REQUIRE(std::strcmp(compositionStates[1].value, "BOUND") == 0);
    pages[0].cancel(pages[0].context);
    compositionStates = {};
    compositionStateCount = 0;
    REQUIRE(g_copiedComposition.readNodeStates(
               g_copiedComposition.context, "absolute.hotas",
               "hotas-flight-axes", compositionStates.data(),
               static_cast<std::uint32_t>(compositionStates.size()),
               &compositionStateCount) == Result::Ok);
    REQUIRE(std::strcmp(compositionStates[1].value, "NEEDS BINDING") == 0);
    REQUIRE(AbsoluteControlSubscriber::Testing::RegisterShipButtonsComposition(
               &g_compositionApi) == Result::Ok);
    REQUIRE(AbsoluteControlSubscriber::Testing::
        IsShipButtonsCompositionRegistered());
    REQUIRE(std::strcmp(g_copiedComposition.pageId,
                       "hotas-ship-buttons") == 0);
    REQUIRE(g_copiedComposition.nodeCount == 100);
    REQUIRE(g_copiedComposition.associationCount == 0);
    REQUIRE(pageCount == kExpectedPageIds.size());
    REQUIRE(std::strcmp(AbsoluteControlSubscriber::Testing::Module().moduleId,
                       "absolute.hotas") == 0);
    REQUIRE(std::string_view(pages[0].pageId) == "hotas-flight-axes");
    REQUIRE(std::string_view(pages[0].displayName) == "Flight Axes");
    REQUIRE(std::string_view(pages[0].controls[0].controlId) ==
           "flight-axes-edit-profile");
    REQUIRE((pages[0].controls[0].flags & kControlPinnedContext) != 0);
    REQUIRE(std::string_view(pages[0].controls[1].controlId) ==
           "flight-axes-layer-mode");
    REQUIRE(std::string_view(pages[0].controls[2].controlId) ==
           "flight-axes-layer-modifier");
    REQUIRE(FindControl(pages[0], "bind-throttle-axis") != nullptr);
    REQUIRE(std::string_view(pages[1].pageId) == "hotas-ship-buttons");
    REQUIRE(std::string_view(pages[1].displayName) == "Ship Buttons");
    REQUIRE(FindControl(pages[1], "ship-native-controls")->kind ==
           ControlKind::GroupHeader);
    REQUIRE(FindControl(pages[1], "ship-axis-bindings") == nullptr);
    REQUIRE(FindControl(pages[1], "bind-throttle-axis") == nullptr);
    REQUIRE(FindControl(pages[1], "bind-ship-select-accept") != nullptr);
    REQUIRE(FindControl(pages[1], "bind-menu-accept") != nullptr);
    REQUIRE(FindControl(pages[1], "bind-menu-cancel") != nullptr);
    REQUIRE(FindControl(pages[1], "shortcut-menu-preset") == nullptr);
    REQUIRE(std::string_view(pages[8].pageId) == "hotas-setup");
    REQUIRE(std::string_view(pages[8].displayName) == "Administration");
    for (std::size_t index = 0; index < pageCount; ++index) {
        REQUIRE(std::strcmp(pages[index].pageId, kExpectedPageIds[index]) == 0);
        REQUIRE(pages[index].beginBindingCapture != nullptr);
        REQUIRE(pages[index].pollBindingCapture != nullptr);
        REQUIRE(pages[index].cancelBindingCapture != nullptr);
        REQUIRE(pages[index].reassignBinding != nullptr);
        REQUIRE(pages[index].context != nullptr);
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
    REQUIRE(pinnedControlCount == 27);
    REQUIRE(editableControlCount ==
           AbsoluteControlSettings::Definitions().size() +
           HotasBindingCatalog::kTargetCount + 91);
    for (const auto& definition : AbsoluteControlSettings::Definitions()) {
        const auto* owner = FindPageForControl(pages, pageCount, definition.controlId);
        REQUIRE(owner);
        const auto* control = FindControl(*owner, definition.controlId);
        REQUIRE(control);
        const auto expectedKind =
            definition.type == AbsoluteControlSettings::ScalarType::Boolean ?
                ControlKind::Toggle :
            definition.type == AbsoluteControlSettings::ScalarType::Integer ?
                ControlKind::IntegerSlider :
            definition.type == AbsoluteControlSettings::ScalarType::Choice ?
                ControlKind::Choice : ControlKind::FloatSlider;
        REQUIRE(control->kind == expectedKind);
        if (definition.type != AbsoluteControlSettings::ScalarType::Boolean) {
            REQUIRE(control->minimumValue == definition.minimum);
            REQUIRE(control->maximumValue == definition.maximum);
            REQUIRE(control->stepValue == definition.step);
        }
        REQUIRE((control->flags & kControlReadOnly) == 0);
        REQUIRE(owner->writeDraft && owner->apply && owner->cancel);
    }
    for (const auto& target : HotasBindingCatalog::kTargets) {
        const auto* owner = FindPageForControl(pages, pageCount, target.controlId);
        REQUIRE(owner && std::string_view(owner->pageId) == target.pageId);
        const auto* control = FindControl(*owner, target.controlId);
        REQUIRE(control && control->kind == ControlKind::InputBinding);
        REQUIRE((control->flags & kBindingController) != 0);
        REQUIRE((control->flags & kBindingClearable) != 0);
        REQUIRE((control->flags & kControlReadOnly) == 0);
        REQUIRE(owner->writeDraft && owner->apply && owner->cancel);
        REQUIRE(owner->beginBindingCapture && owner->pollBindingCapture &&
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
        REQUIRE(action);
        REQUIRE(control);
        ValueV1 routeValue{};
        REQUIRE(pages[1].readValue(
                   pages[1].context, routeId.c_str(), &routeValue) == Result::Ok);
        REQUIRE(control->kind == ControlKind::Choice);
        REQUIRE(routeValue.kind == ValueKind::Integer);
        if (HasSelectableShipControlRoute(*action)) {
            REQUIRE((control->flags & kControlReadOnly) == 0);
            REQUIRE(routeValue.integerValue ==
                (action->recommendedMethod == ShipControlMethod::Direct ? 0 : 1));
            ++selectableRouteCount;
        } else {
            REQUIRE((control->flags & kControlReadOnly) != 0);
            REQUIRE(routeValue.integerValue == 1);
            ++fixedRouteCount;
        }
        std::array<ChoiceOptionV1, 2> choices{};
        std::uint32_t choiceCount{};
        REQUIRE(pages[1].readChoiceOptions(
                   pages[1].context, routeId.c_str(), choices.data(),
                   static_cast<std::uint32_t>(choices.size()),
                   &choiceCount) == Result::Ok);
        REQUIRE(choiceCount == 2);
        REQUIRE(std::string_view(choices[0].label) ==
            (HasSelectableShipControlRoute(*action)
                ? "Direct function" : "Direct injection unavailable"));
        REQUIRE(std::string_view(choices[1].label) ==
            (!HasSelectableShipControlRoute(*action)
                ? "Fixed ship-context SendInput"
                : AlternateShipControlMethod(*action) == ShipControlMethod::Context
                    ? "Ship-context SendInput E"
                    : "SendInput key / mouse"));
        ++routeStatusCount;
    }
    REQUIRE(routeStatusCount == 23);
    REQUIRE(selectableRouteCount == 18);
    REQUIRE(fixedRouteCount == 5);
    REQUIRE(pages[1].controlCount < kMaximumChoiceOptions);
    for (const auto id : {"profile-operation-name", "macro-name"}) {
        const auto* owner = FindPageForControl(pages, pageCount, id);
        REQUIRE(owner);
        const auto* text = FindControl(*owner, id);
        REQUIRE(text && text->kind == ControlKind::TextInput);
        REQUIRE(text->minimumValue == 0.0);
        REQUIRE(text->maximumValue == kStringValueCapacity - 1);
        REQUIRE(text->stepValue == 1.0);
    }
    const auto& devicePage = pages[6];
    REQUIRE(devicePage.controlCount == 14);
    REQUIRE(devicePage.writeDraft && devicePage.invokeAction &&
           devicePage.readRecordItems);
    REQUIRE(devicePage.apply && devicePage.cancel);
    const auto* deviceRecords = FindControl(devicePage, "device-selection");
    const auto* deviceClear = FindControl(
        devicePage, "clear-device-calibration");
    const auto* deviceReassign = FindControl(
        devicePage, "reassign-duplicate-device");
    REQUIRE(deviceRecords &&
           deviceRecords->kind == ControlKind::RecordCollection &&
           (deviceRecords->flags & kControlTransientSelection) != 0);
    REQUIRE(deviceClear && deviceReassign &&
           (deviceClear->flags & kControlRequiresConfirmation) != 0 &&
           (deviceReassign->flags & kControlRequiresConfirmation) != 0);
    REQUIRE(pages[1].writeDraft && pages[1].apply && pages[1].cancel);
    REQUIRE(pages[2].writeDraft && pages[2].apply && pages[2].cancel);
    REQUIRE(pages[2].readChoiceOptions);
    REQUIRE(pages[3].writeDraft && pages[3].apply && pages[3].cancel);
    const auto* reportPage = FindPageForControl(pages, pageCount, "export-support-report");
    REQUIRE(reportPage && reportPage->invokeAction);
    REQUIRE(reportPage->invokeAction(reportPage->context, "export-support-report") == Result::Ok);
    REQUIRE(SupportReport::lastFrontend.contains("Control ABI 1"));
    SupportReport::failExport = true;
    REQUIRE(reportPage->invokeAction(reportPage->context, "export-support-report") == Result::Rejected);
    ValueV1 reportStatus;
    REQUIRE(reportPage->readValue(reportPage->context, "support-report-status", &reportStatus) == Result::Ok);
    REQUIRE(std::string_view(reportStatus.stringValue) == "Report failed");
    SupportReport::failExport = false;
    REQUIRE(pages[4].writeDraft && pages[4].apply && pages[4].cancel);
    REQUIRE(pages[4].invokeAction && pages[4].readChoiceOptions &&
           pages[4].readRecordItems);
    REQUIRE(pages[5].writeDraft && pages[5].invokeAction && pages[5].apply &&
           pages[5].cancel && pages[5].readChoiceOptions &&
           pages[5].readRecordItems && pages[5].beginBindingCapture);
    REQUIRE(FindControl(pages[5], "macro-records"));
    REQUIRE(FindControl(pages[5], "macro-step-records"));
    REQUIRE(FindControl(pages[5], "macro-target-records"));
    REQUIRE(FindControl(pages[5], "macro-step-amount"));
    REQUIRE(FindControl(pages[1], "shortcut-records"));
    REQUIRE(FindControl(pages[1], "shortcut-output"));
    const auto* throttleSummary = FindControl(
        pages[0], "flight-throttle-summary");
    const auto* bindingsLink = FindControl(pages[0], "flight-open-bindings");
    const auto* throttleLink = FindControl(pages[0], "flight-open-throttle");
    const auto* macroLink = FindControl(pages[1], "shortcut-macro-link");
    REQUIRE(throttleSummary &&
           (throttleSummary->flags & kControlReadOnly) != 0);
    REQUIRE(throttleLink && throttleLink->kind == ControlKind::Action &&
           pages[0].invokeAction);
    REQUIRE(bindingsLink && bindingsLink->kind == ControlKind::Action);
    REQUIRE(macroLink && macroLink->kind == ControlKind::Action &&
           pages[1].invokeAction);
    REQUIRE(!FindControl(pages[0], "rate-throttle-enabled"));
    const auto* profileRecords = FindControl(pages[4], "profile-records");
    REQUIRE(profileRecords && profileRecords->kind == ControlKind::RecordCollection);
    REQUIRE((profileRecords->flags & kControlTransientSelection) != 0);
    const auto* profileImport = FindControl(pages[4], "profile-import-full");
    const auto* profileReset = FindControl(pages[4], "profile-reset-main");
    REQUIRE(profileImport && profileReset);
    REQUIRE((profileImport->flags & kControlRequiresConfirmation) != 0);
    REQUIRE((profileReset->flags & kControlRequiresConfirmation) != 0);

    std::unordered_set<std::string_view> catalogIds;
    for (std::size_t index = 0;
         index < AbsoluteControlSettings::Definitions().size(); ++index) {
        const auto& definition = AbsoluteControlSettings::Definitions()[index];
        REQUIRE(static_cast<std::size_t>(definition.field) == index);
        REQUIRE(catalogIds.insert(definition.controlId).second);
        REQUIRE(!definition.section.empty() && !definition.key.empty());
        REQUIRE(definition.minimum <= definition.defaultValue);
        REQUIRE(definition.defaultValue <= definition.maximum);
    }
    std::string catalogError;
    auto invalidCatalogState = AbsoluteControlSettings::DefaultState();
    REQUIRE(AbsoluteControlSettings::Validate(invalidCatalogState, catalogError));
    AbsoluteControlSettings::SetFloat(
        invalidCatalogState,
        AbsoluteControlSettings::ScalarField::MenuReleaseThreshold,
        AbsoluteControlSettings::GetFloat(
            invalidCatalogState,
            AbsoluteControlSettings::ScalarField::MenuEngageThreshold));
    REQUIRE(!AbsoluteControlSettings::Validate(invalidCatalogState, catalogError));
    REQUIRE(!catalogError.empty());

    for (std::size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        for (std::uint32_t controlIndex = 0;
             controlIndex < pages[pageIndex].controlCount; ++controlIndex) {
            const std::string_view id =
                pages[pageIndex].controls[controlIndex].controlId;
            REQUIRE(id.find("power") == std::string_view::npos);
            REQUIRE(id.find("head") == std::string_view::npos);
            REQUIRE(id.find("mouse") == std::string_view::npos);
            REQUIRE(id.find("hosam") == std::string_view::npos);
            REQUIRE(id.find("alignment") == std::string_view::npos);
        }
    }
    REQUIRE(AbsoluteControlSubscriber::Testing::ValidateDescriptors(pages, pageCount) ==
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
    REQUIRE(AbsoluteControlSubscriber::Testing::ValidateDescriptors(
               invalidPages.data(), invalidPages.size()) == Result::Duplicate);
    copiedControls[7][3] = pages[7].controls[3];
    const auto profileText = std::ranges::find_if(copiedControls[4],
        [](const ControlDescriptorV1& control) {
            return std::string_view(control.controlId) == "profile-operation-name";
        });
    REQUIRE(profileText != copiedControls[4].end());
    profileText->maximumValue = 0.0;
    REQUIRE(AbsoluteControlSubscriber::Testing::ValidateDescriptors(
               invalidPages.data(), invalidPages.size()) == Result::InvalidArgument);
    *profileText = *FindControl(pages[4], "profile-operation-name");
    copiedControls[8][0].kind = static_cast<ControlKind>(99);
    REQUIRE(AbsoluteControlSubscriber::Testing::ValidateDescriptors(
               invalidPages.data(), invalidPages.size()) == Result::InvalidArgument);

    ResetFakeHost();
    g_resolvedApi = nullptr;
    REQUIRE(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::NotFound);
    REQUIRE(!AbsoluteControlSubscriber::IsHosted());

    ResetFakeHost();
    ApiV1 olderApi = g_api;
    olderApi.structSize = kApiV1BaseSize;
    g_resolvedApi = &olderApi;
    REQUIRE(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Rejected);
    REQUIRE(!AbsoluteControlSubscriber::IsHosted());
    REQUIRE(g_moduleCalls == 0);
    REQUIRE(g_pageCalls == 0);

    ResetFakeHost();
    ApiV1 capturelessApi = g_api;
    capturelessApi.capabilities = kCapabilityLabeledChoices;
    g_resolvedApi = &capturelessApi;
    REQUIRE(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Ok);
    REQUIRE(AbsoluteControlSubscriber::IsHosted());
    REQUIRE(g_pageCalls == kExpectedPageIds.size());
    for (const auto& page : g_copiedPages) {
        REQUIRE(page.beginBindingCapture == nullptr);
        REQUIRE(page.pollBindingCapture == nullptr);
        REQUIRE(page.cancelBindingCapture == nullptr);
        REQUIRE(page.reassignBinding == nullptr);
    }
    for (const auto& target : HotasBindingCatalog::kTargets) {
        REQUIRE(FindPageForControl(g_copiedPages.data(), g_copiedPages.size(),
                                  target.controlId) == nullptr);
    }
    REQUIRE(FindControl(g_copiedPages[6], "devices-scope"));
    REQUIRE(!FindControl(g_copiedPages[6], "device-selection"));
    REQUIRE(g_copiedPages[6].readRecordItems == nullptr);
    REQUIRE(g_copiedPages[6].invokeAction == nullptr);
    REQUIRE(!AbsoluteControlSubscriber::RequestHostPage("hotas-setup"));
    REQUIRE(g_openPageCalls == 0);

    // A pre-command ABI-v1 host can still register every existing page. Its
    // shorter table must never be read past the advertised struct size.
    ResetFakeHost();
    ApiV1 preOpenCommandApi = g_api;
    preOpenCommandApi.structSize =
        static_cast<std::uint32_t>(offsetof(ApiV1, requestOpenPage));
    g_resolvedApi = &preOpenCommandApi;
    REQUIRE(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Ok);
    REQUIRE(!AbsoluteControlSubscriber::RequestHostPage("hotas-setup"));
    REQUIRE(g_openPageCalls == 0);
    REQUIRE(!FindControl(g_copiedPages[0], "flight-throttle-summary"));
    REQUIRE(!FindControl(g_copiedPages[0], "flight-open-bindings"));
    REQUIRE(!FindControl(g_copiedPages[0], "flight-open-throttle"));
    const auto* fallbackMacroLink = FindControl(
        g_copiedPages[1], "shortcut-macro-link");
    REQUIRE(fallbackMacroLink &&
           fallbackMacroLink->kind == ControlKind::InputBinding &&
           (fallbackMacroLink->flags & kControlReadOnly) != 0);
    REQUIRE(g_copiedPages[1].invokeAction(
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
    REQUIRE(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Ok);
    REQUIRE(FindControl(g_copiedPages[6], "device-selection"));
    REQUIRE(g_copiedPages[6].readRecordItems);
    REQUIRE(g_copiedPages[6].invokeAction);
    REQUIRE(g_copiedPages[6].writeDraft);
    REQUIRE(!g_copiedPages[6].apply && !g_copiedPages[6].cancel);
    for (const auto& page : g_copiedPages) {
        REQUIRE(page.beginBindingCapture == nullptr);
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
    REQUIRE(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Ok);
    REQUIRE(g_copiedPages[0].beginBindingCapture != nullptr);
    REQUIRE(g_copiedPages[4].controlCount == 1);
    REQUIRE(FindControl(g_copiedPages[4], "profiles-scope"));
    REQUIRE(g_copiedPages[4].readRecordItems == nullptr);
    REQUIRE(g_copiedPages[4].beginBindingCapture == nullptr);

    ResetFakeHost();
    g_rejectPageIndex = 4;
    REQUIRE(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Rejected);
    REQUIRE(!AbsoluteControlSubscriber::IsHosted());
    REQUIRE(g_moduleCalls == 1);
    REQUIRE(g_pageCalls == 5);
    REQUIRE(g_unregisterCalls == 1);
    REQUIRE(g_copiedModule.moduleId[0] == '\0');
    for (const auto& copiedPage : g_copiedPages) {
        REQUIRE(copiedPage.pageId[0] == '\0');
    }

    ResetFakeHost();
    g_behavior = HostBehavior::DuplicateModule;
    g_rejectPageIndex = 2;
    REQUIRE(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Rejected);
    REQUIRE(!AbsoluteControlSubscriber::IsHosted());
    REQUIRE(g_moduleCalls == 1);
    REQUIRE(g_pageCalls == 3);
    REQUIRE(g_unregisterCalls == 0);

    ResetFakeHost();
    ApiV1 missingCapabilityApi = g_api;
    missingCapabilityApi.capabilities = kCapabilityStructuredLayout |
                                        kCapabilityProviderBindingCapture;
    g_resolvedApi = &missingCapabilityApi;
    REQUIRE(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Rejected);
    REQUIRE(!AbsoluteControlSubscriber::IsHosted());
    REQUIRE(g_moduleCalls == 0);
    REQUIRE(g_pageCalls == 0);

    ResetFakeHost();
    g_behavior = HostBehavior::Reject;
    REQUIRE(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Rejected);
    REQUIRE(!AbsoluteControlSubscriber::IsHosted());
    g_behavior = HostBehavior::Accept;
    REQUIRE(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Rejected);

    ResetFakeHost();
    g_behavior = HostBehavior::Invalid;
    REQUIRE(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Rejected);
    g_behavior = HostBehavior::Accept;
    REQUIRE(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Rejected);

    ResetFakeHost();
    g_behavior = HostBehavior::NotReady;
    REQUIRE(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::NotReady);
    REQUIRE(!AbsoluteControlSubscriber::IsHosted());
    g_behavior = HostBehavior::Accept;
    REQUIRE(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Ok);
    REQUIRE(AbsoluteControlSubscriber::IsHosted());
    REQUIRE(g_moduleCalls == 2);
    REQUIRE(g_pageCalls == kExpectedPageIds.size());
    REQUIRE(std::strcmp(g_copiedModule.moduleId, "absolute.hotas") == 0);
    for (std::size_t index = 0; index < kExpectedPageIds.size(); ++index) {
        REQUIRE(std::strcmp(g_copiedPages[index].pageId,
                           kExpectedPageIds[index]) == 0);
    }

    g_hostOpen = true;
    g_hostCapture = true;
    REQUIRE(AbsoluteControlSubscriber::IsHostOpen());
    REQUIRE(AbsoluteControlSubscriber::IsHostInputCaptureActive());
    g_hostOpen = false;
    g_hostCapture = false;

    REQUIRE(AbsoluteControlSubscriber::RequestHostPage("hotas-setup"));
    REQUIRE(g_openPageCalls == 1);
    REQUIRE(g_openModule == "absolute.hotas");
    REQUIRE(g_openPage == "hotas-setup");
    REQUIRE(g_copiedPages[0].invokeAction(
               g_copiedPages[0].context, "flight-open-throttle") == Result::Ok);
    REQUIRE(g_openPageCalls == 2 && g_openPage == "hotas-throttle");
    REQUIRE(g_copiedPages[0].invokeAction(
               g_copiedPages[0].context, "flight-open-bindings") == Result::Ok);
    REQUIRE(g_openPageCalls == 3 && g_openPage == "hotas-ship-buttons");
    REQUIRE(g_copiedPages[1].invokeAction(
               g_copiedPages[1].context, "shortcut-macro-link") == Result::Ok);
    REQUIRE(g_openPageCalls == 4 && g_openPage == "hotas-macros");
    g_openPageResult = Result::Rejected;
    REQUIRE(!AbsoluteControlSubscriber::RequestHostPage("hotas-diagnostics"));
    REQUIRE(g_openPageCalls == 5);
    REQUIRE(g_openPage == "hotas-diagnostics");
    REQUIRE(!AbsoluteControlSubscriber::RequestHostPage(nullptr));
    REQUIRE(g_openPageCalls == 5);

    const auto& axesPage = g_copiedPages[0];
    const auto& shipPage = g_copiedPages[1];
    const auto& aimingPage = g_copiedPages[3];
    const auto& profilesPage = g_copiedPages[4];

    ValueV1 throttleSummaryValue{};
    REQUIRE(axesPage.readValue(
               axesPage.context, "flight-throttle-summary",
               &throttleSummaryValue) == Result::Ok);
    REQUIRE(throttleSummaryValue.kind == ValueKind::String);
    REQUIRE(std::string_view{ throttleSummaryValue.stringValue }.find(
               "edited once on Throttle Setup") != std::string_view::npos);

    std::array<RecordItemV1, kMaximumRecordItems> profileItems{};
    std::uint32_t profileCount{};
    REQUIRE(profilesPage.readRecordItems(
               profilesPage.context, "profile-records", profileItems.data(), 2,
               &profileCount) == Result::CapacityExceeded);
    REQUIRE(profileCount == 3);
    REQUIRE(profilesPage.readRecordItems(
               profilesPage.context, "profile-records", profileItems.data(),
               static_cast<std::uint32_t>(profileItems.size()), &profileCount) ==
           Result::Ok);
    REQUIRE(std::strcmp(profileItems[0].recordId, "main-controls") == 0);
    REQUIRE(std::string_view(profileItems[1].summary).find("inherits Main") !=
           std::string_view::npos);

    ValueV1 profileValue{};
    REQUIRE(profilesPage.readValue(
               profilesPage.context, "profile-records", &profileValue) == Result::Ok);
    REQUIRE(std::strcmp(profileValue.stringValue, "main-controls") == 0);
    ValueV1 profileEdit{};
    profileEdit.kind = ValueKind::String;
    strcpy_s(profileEdit.stringValue, profileItems[1].recordId);
    REQUIRE(profilesPage.writeDraft(
               profilesPage.context, "profile-records", &profileEdit) == Result::Ok);
    profileValue = {};
    REQUIRE(profilesPage.readValue(
               profilesPage.context, "profile-records", &profileValue) == Result::Ok);
    REQUIRE(std::strcmp(profileValue.stringValue, profileItems[1].recordId) == 0);

    profileEdit = {};
    profileEdit.kind = ValueKind::Integer;
    profileEdit.integerValue = 2;
    REQUIRE(profilesPage.writeDraft(profilesPage.context,
               "profile-activation-mode", &profileEdit) == Result::Ok);
    REQUIRE(profilesPage.beginBindingCapture(profilesPage.context,
               "profile-activation-trigger") == Result::Ok);
    REQUIRE(TestCapture::beginSlot == CaptureSlot::kProfileTrigger);
    REQUIRE(TestCapture::settleMilliseconds == 300);
    REQUIRE(profilesPage.cancelBindingCapture(profilesPage.context,
               "profile-activation-trigger") == Result::Ok);

    // Dirty selection remains on the current record until one of the explicit
    // save/discard/stay actions resolves the legacy Wizard target switch.
    profileEdit = {};
    profileEdit.kind = ValueKind::String;
    strcpy_s(profileEdit.stringValue, profileItems[2].recordId);
    REQUIRE(profilesPage.writeDraft(
               profilesPage.context, "profile-records", &profileEdit) == Result::Ok);
    profileValue = {};
    REQUIRE(profilesPage.readValue(
               profilesPage.context, "profile-records", &profileValue) == Result::Ok);
    REQUIRE(std::strcmp(profileValue.stringValue, profileItems[1].recordId) == 0);
    REQUIRE(profilesPage.invokeAction(
               profilesPage.context, "profile-switch-stay") == Result::Ok);
    REQUIRE(profilesPage.apply(profilesPage.context) == Result::Ok);
    REQUIRE(TestProfiles::repository.updates.size() == 1);
    REQUIRE(TestProfiles::repository.updates.front().profile == "Flight Aux");

    // Ordinary assignment detects a collision; reassign atomically clears the
    // previous activation owner within this page draft.
    profileEdit = {};
    profileEdit.kind = ValueKind::String;
    strcpy_s(profileEdit.stringValue, "8");
    REQUIRE(profilesPage.writeDraft(profilesPage.context,
               "profile-activation-trigger", &profileEdit) == Result::Duplicate);
    REQUIRE(profilesPage.reassignBinding(profilesPage.context,
               "profile-activation-trigger", "8") == Result::Ok);
    REQUIRE(profilesPage.apply(profilesPage.context) == Result::Ok);

    // Every rich page exposes the same pinned profile/layer context. Switching
    // here changes the actual Wizard edit target, activation fields share the
    // Profiles draft, and a dirty settings page cannot be silently abandoned.
    std::array<RecordItemV1, kMaximumRecordItems> pinnedProfileItems{};
    std::uint32_t pinnedProfileCount{};
    REQUIRE(axesPage.readRecordItems(axesPage.context,
               "flight-axes-edit-profile", pinnedProfileItems.data(),
               static_cast<std::uint32_t>(pinnedProfileItems.size()),
               &pinnedProfileCount) == Result::Ok);
    REQUIRE(pinnedProfileCount == profileCount);
    profileValue = {};
    REQUIRE(axesPage.readValue(axesPage.context, "flight-axes-edit-profile",
               &profileValue) == Result::Ok);
    REQUIRE(std::strcmp(profileValue.stringValue,
               profileItems[1].recordId) == 0);

    ValueV1 pinnedAxisEdit{};
    pinnedAxisEdit.kind = ValueKind::Boolean;
    pinnedAxisEdit.booleanValue = 1;
    REQUIRE(axesPage.writeDraft(axesPage.context, "pitch-inverted",
               &pinnedAxisEdit) == Result::Ok);
    profileEdit = {};
    profileEdit.kind = ValueKind::String;
    strcpy_s(profileEdit.stringValue, profileItems[2].recordId);
    REQUIRE(axesPage.writeDraft(axesPage.context, "flight-axes-edit-profile",
               &profileEdit) == Result::Rejected);
    REQUIRE(TestProfiles::repository.current == "Flight Aux");
    axesPage.cancel(axesPage.context);

    REQUIRE(axesPage.writeDraft(axesPage.context, "flight-axes-edit-profile",
               &profileEdit) == Result::Ok);
    REQUIRE(TestProfiles::repository.current == "Independent");
    REQUIRE(TestSettings::editTarget == "Independent");
    profileEdit = {};
    profileEdit.kind = ValueKind::Integer;
    profileEdit.integerValue = 1;
    REQUIRE(axesPage.writeDraft(axesPage.context, "flight-axes-layer-mode",
               &profileEdit) == Result::Ok);
    profileEdit = {};
    profileEdit.kind = ValueKind::String;
    strcpy_s(profileEdit.stringValue, "10");
    REQUIRE(axesPage.writeDraft(axesPage.context,
               "flight-axes-layer-modifier", &profileEdit) == Result::Ok);
    REQUIRE(axesPage.beginBindingCapture(axesPage.context,
               "flight-axes-layer-modifier") == Result::Ok);
    REQUIRE(TestCapture::beginSlot == CaptureSlot::kProfileTrigger);
    REQUIRE(TestCapture::settleMilliseconds == 50);
    REQUIRE(axesPage.cancelBindingCapture(axesPage.context,
               "flight-axes-layer-modifier") == Result::Ok);

    pinnedAxisEdit = {};
    pinnedAxisEdit.kind = ValueKind::Float;
    pinnedAxisEdit.floatValue = 1.25;
    REQUIRE(axesPage.writeDraft(axesPage.context, "yaw-sensitivity",
               &pinnedAxisEdit) == Result::Ok);
    REQUIRE(axesPage.apply(axesPage.context) == Result::Ok);
    REQUIRE(TestSettings::editTarget == "Independent");
    const auto independentUpdate = std::find_if(
        TestProfiles::repository.updates.begin(),
        TestProfiles::repository.updates.end(), [](const auto& update) {
            return update.profile == "Independent";
        });
    REQUIRE(independentUpdate != TestProfiles::repository.updates.end());
    REQUIRE(independentUpdate->mode == "toggle");
    REQUIRE(independentUpdate->trigger == "10");

    const auto* throttleBinding =
        HotasBindingCatalog::Find("bind-throttle-axis");
    REQUIRE(throttleBinding);
    REQUIRE(axesPage.beginBindingCapture(
               axesPage.context, throttleBinding->controlId.data()) == Result::Ok);
    REQUIRE(TestCapture::beginSlot == throttleBinding->captureSlot);
    BindingCaptureV1 capture{};
    REQUIRE(axesPage.pollBindingCapture(
               axesPage.context, throttleBinding->controlId.data(), &capture) ==
           Result::Ok);
    REQUIRE(capture.state == BindingCaptureState::Capturing);
    REQUIRE(axesPage.pollBindingCapture(
               aimingPage.context, throttleBinding->controlId.data(), &capture) ==
           Result::InvalidArgument);
    TestCapture::nextState = HotasBindingCapture::PollState::Captured;
    TestCapture::nextBinding = "Test Stick@0x32";
    capture = {};
    REQUIRE(axesPage.pollBindingCapture(
               axesPage.context, throttleBinding->controlId.data(), &capture) ==
           Result::Ok);
    REQUIRE(capture.state == BindingCaptureState::Captured);
    REQUIRE(std::strcmp(capture.binding, "Test Stick@0x32") == 0);
    ValueV1 bindingEdit{};
    bindingEdit.kind = ValueKind::String;
    strcpy_s(bindingEdit.stringValue, capture.binding);
    REQUIRE(axesPage.writeDraft(
               axesPage.context, throttleBinding->controlId.data(), &bindingEdit) ==
           Result::Ok);
    ValueV1 bindingValue{};
    REQUIRE(axesPage.readValue(
               axesPage.context, throttleBinding->controlId.data(), &bindingValue) ==
           Result::Ok);
    REQUIRE(std::strcmp(bindingValue.stringValue, "Test Stick@0x32") == 0);
    axesPage.cancel(axesPage.context);
    bindingValue = {};
    REQUIRE(axesPage.readValue(
               axesPage.context, throttleBinding->controlId.data(), &bindingValue) ==
           Result::Ok);
    REQUIRE(std::strcmp(bindingValue.stringValue, "(unbound)") == 0);

    TestCapture::nextState = HotasBindingCapture::PollState::Capturing;
    REQUIRE(axesPage.beginBindingCapture(
               axesPage.context, throttleBinding->controlId.data()) == Result::Ok);
    bindingEdit = {};
    bindingEdit.kind = ValueKind::String;
    strcpy_s(bindingEdit.stringValue, "Test Stick@0x31");
    REQUIRE(axesPage.writeDraft(
               axesPage.context, throttleBinding->controlId.data(), &bindingEdit) ==
           Result::Ok);
    REQUIRE(axesPage.apply(axesPage.context) == Result::Ok);
    capture = {};
    REQUIRE(axesPage.pollBindingCapture(
               axesPage.context, throttleBinding->controlId.data(), &capture) ==
           Result::Ok);
    REQUIRE(capture.state == BindingCaptureState::Cancelled);
    bindingEdit = {};
    bindingEdit.kind = ValueKind::String;
    strcpy_s(bindingEdit.stringValue, "(unbound)");
    REQUIRE(axesPage.writeDraft(
               axesPage.context, throttleBinding->controlId.data(), &bindingEdit) ==
           Result::Ok);
    REQUIRE(axesPage.apply(axesPage.context) == Result::Ok);

    const auto* previousBinding =
        HotasBindingCatalog::Find("bind-ship-fire-boosters");
    const auto* targetBinding =
        HotasBindingCatalog::Find("bind-ship-switch-flight-modes");
    REQUIRE(previousBinding && targetBinding);
    bindingEdit = {};
    bindingEdit.kind = ValueKind::String;
    strcpy_s(bindingEdit.stringValue, "Test Throttle@7");
    REQUIRE(shipPage.writeDraft(shipPage.context,
               previousBinding->controlId.data(), &bindingEdit) == Result::Ok);
    TestCapture::nextState = HotasBindingCapture::PollState::Captured;
    TestCapture::nextBinding = "Test Throttle@7";
    REQUIRE(shipPage.beginBindingCapture(
               shipPage.context, targetBinding->controlId.data()) == Result::Ok);
    capture = {};
    REQUIRE(shipPage.pollBindingCapture(
               shipPage.context, targetBinding->controlId.data(), &capture) ==
           Result::Ok);
    REQUIRE(capture.state == BindingCaptureState::Captured);
    bindingEdit = {};
    bindingEdit.kind = ValueKind::String;
    strcpy_s(bindingEdit.stringValue, capture.binding);
    REQUIRE(shipPage.writeDraft(shipPage.context,
               targetBinding->controlId.data(), &bindingEdit) == Result::Duplicate);
    REQUIRE(shipPage.reassignBinding(
               shipPage.context, targetBinding->controlId.data(),
               capture.binding) == Result::Ok);
    bindingValue = {};
    REQUIRE(shipPage.readValue(shipPage.context,
               previousBinding->controlId.data(), &bindingValue) == Result::Ok);
    REQUIRE(std::strcmp(bindingValue.stringValue, "(unbound)") == 0);
    bindingValue = {};
    REQUIRE(shipPage.readValue(shipPage.context,
               targetBinding->controlId.data(), &bindingValue) == Result::Ok);
    REQUIRE(std::strcmp(bindingValue.stringValue, "Test Throttle@7") == 0);
    REQUIRE(shipPage.apply(shipPage.context) == Result::Ok);
    REQUIRE(TestSettings::storedBindings[
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
    REQUIRE(shipPage.writeDraft(shipPage.context, boostRouteId.c_str(),
                               &routeEdit) == Result::Ok);
    REQUIRE(shipPage.writeDraft(axesPage.context, boostRouteId.c_str(),
                               &routeEdit) == Result::InvalidArgument);
    const auto* contextBinding =
        HotasBindingCatalog::Find("bind-ship-select-accept");
    REQUIRE(contextBinding);
    const std::string contextRouteId =
        std::string(contextBinding->controlId) + "-route";
    ValueV1 directSelectEdit{};
    directSelectEdit.kind = ValueKind::Integer;
    directSelectEdit.integerValue = 0;
    REQUIRE(shipPage.writeDraft(shipPage.context, contextRouteId.c_str(),
                               &directSelectEdit) == Result::Ok);
    REQUIRE(shipPage.apply(shipPage.context) == Result::Ok);
    const auto* boostAction = FindShipAction(previousBinding->actionId);
    REQUIRE(boostAction);
    REQUIRE(TestSettings::storedRoutes[static_cast<std::size_t>(
               boostAction - kShipActionCatalog.data())] ==
           ShipControlMethod::KeyboardCompatibility);
    const auto* selectAction = FindShipAction(contextBinding->actionId);
    REQUIRE(selectAction);
    REQUIRE(TestSettings::storedRoutes[static_cast<std::size_t>(
               selectAction - kShipActionCatalog.data())] ==
           ShipControlMethod::Direct);
    ValueV1 routeReadBack{};
    REQUIRE(shipPage.readValue(shipPage.context, boostRouteId.c_str(),
                              &routeReadBack) == Result::Ok);
    REQUIRE(routeReadBack.kind == ValueKind::Integer &&
           routeReadBack.integerValue == 1);

    TestCapture::nextState = HotasBindingCapture::PollState::Capturing;
    REQUIRE(axesPage.beginBindingCapture(
               axesPage.context, throttleBinding->controlId.data()) == Result::Ok);
    REQUIRE(axesPage.cancelBindingCapture(
               axesPage.context, throttleBinding->controlId.data()) == Result::Ok);
    capture = {};
    REQUIRE(axesPage.pollBindingCapture(
               axesPage.context, throttleBinding->controlId.data(), &capture) ==
           Result::Ok);
    REQUIRE(capture.state == BindingCaptureState::Idle);

    ValueV1 value{};
    REQUIRE(g_copiedPages[8].readValue(
               g_copiedPages[8].context,
               g_copiedPages[8].controls[0].controlId, &value) == Result::Ok);
    REQUIRE(value.kind == ValueKind::String);
    REQUIRE(value.stringValue[0] != '\0');

    REQUIRE(FindControl(axesPage, "flight-controls-enabled"));
    value = {};
    REQUIRE(axesPage.readValue(axesPage.context, "flight-controls-enabled", &value) == Result::Ok);
    REQUIRE(value.kind == ValueKind::Boolean && value.booleanValue == 1);

    ValueV1 edit = {};
    edit.kind = ValueKind::Boolean;
    edit.booleanValue = 0;
    REQUIRE(axesPage.writeDraft(axesPage.context, "flight-controls-enabled", &edit) == Result::Ok);
    value = {};
    REQUIRE(axesPage.readValue(axesPage.context, "flight-controls-enabled", &value) == Result::Ok);
    REQUIRE(value.booleanValue == 0);
    REQUIRE(AbsoluteControlSettings::GetBoolean(
        TestSettings::stored,
        AbsoluteControlSettings::ScalarField::FlightControlsEnabled));
    REQUIRE(axesPage.apply(axesPage.context) == Result::Ok);
    REQUIRE(!AbsoluteControlSettings::GetBoolean(
        TestSettings::stored,
        AbsoluteControlSettings::ScalarField::FlightControlsEnabled));

    edit = {};
    edit.kind = ValueKind::Float;
    edit.floatValue = 2.0;
    REQUIRE(axesPage.writeDraft(axesPage.context, "pitch-sensitivity", &edit) == Result::Ok);
    axesPage.cancel(axesPage.context);
    value = {};
    REQUIRE(axesPage.readValue(axesPage.context, "pitch-sensitivity", &value) == Result::Ok);
    REQUIRE(value.kind == ValueKind::Float && value.floatValue == 1.0);

    edit = {};
    edit.kind = ValueKind::Integer;
    edit.integerValue = 2;
    REQUIRE(axesPage.writeDraft(axesPage.context, "pitch-sensitivity", &edit) ==
           Result::InvalidArgument);
    edit.kind = ValueKind::Float;
    edit.floatValue = 3.1;
    REQUIRE(axesPage.writeDraft(axesPage.context, "pitch-sensitivity", &edit) ==
           Result::InvalidArgument);

    const auto& throttlePage = g_copiedPages[2];
    REQUIRE(FindControl(throttlePage, "throttle-landmark-guide")->kind ==
           ControlKind::GroupHeader);
    REQUIRE(FindControl(throttlePage, "throttle-positional-zones")->kind ==
           ControlKind::GroupHeader);
    REQUIRE(FindControl(throttlePage, "throttle-rate-mode")->kind ==
           ControlKind::GroupHeader);
    for (const auto* controlId : {
             "throttle-detent-center", "reverse-zone-center",
             "boost-zone-center"}) {
        REQUIRE((FindControl(throttlePage, controlId)->flags &
                kControlAdvanced) != 0);
    }
    for (const auto* actionId : {
             "throttle-capture-detent", "throttle-capture-reverse",
             "throttle-capture-boost", "throttle-link-idle-saturation"}) {
        const auto* action = FindControl(throttlePage, actionId);
        REQUIRE(action && action->kind == ControlKind::Action);
        REQUIRE((action->flags & kControlMutatesDraft) != 0);
        if (std::string_view{actionId} != "throttle-link-idle-saturation") {
            REQUIRE((action->flags & kControlLayoutInline) != 0);
        }
    }
    REQUIRE(throttlePage.invokeAction);
    REQUIRE(throttlePage.invokeAction(
               throttlePage.context, "throttle-capture-detent") == Result::Ok);
    REQUIRE(AbsoluteControlTelemetry::GetThrottleCaptureTarget() ==
        AbsoluteControlTelemetry::ThrottleCaptureTarget::Detent);
    value = {};
    REQUIRE(throttlePage.readValue(
               throttlePage.context, "throttle-scope", &value) == Result::Ok);
    REQUIRE(std::string_view{value.stringValue}.find("LIVE SET") !=
        std::string_view::npos);

    AbsoluteControlTelemetry::ThrottleSample currentThrottle;
    currentThrottle.logicalRawPosition = 41234;
    currentThrottle.logicalRawAvailable = true;
    AbsoluteControlTelemetry::PublishThrottle(currentThrottle);
    // Tracking previews the live landmark; the draft changes only when the
    // same action is pressed again (or Apply finalizes the gesture).
    value = {};
    REQUIRE(throttlePage.readValue(
               throttlePage.context, "throttle-detent-center", &value) == Result::Ok);
    REQUIRE(value.integerValue != 41234);
    REQUIRE(throttlePage.invokeAction(
               throttlePage.context, "throttle-capture-detent") == Result::Ok);
    REQUIRE(AbsoluteControlTelemetry::GetThrottleCaptureTarget() ==
        AbsoluteControlTelemetry::ThrottleCaptureTarget::None);
    value = {};
    REQUIRE(throttlePage.readValue(
               throttlePage.context, "throttle-detent-center", &value) == Result::Ok);
    REQUIRE(value.integerValue == 41234);
    REQUIRE(AbsoluteControlSettings::GetInteger(
               TestSettings::stored,
               AbsoluteControlSettings::ScalarField::DetentCenter) != 41234);
    throttlePage.cancel(throttlePage.context);

    // A direct graph drag writes the linked numeric control and cancels an
    // in-progress physical tracking gesture so the two editors never fight.
    REQUIRE(throttlePage.invokeAction(
               throttlePage.context, "throttle-capture-detent") == Result::Ok);
    edit = {};
    edit.kind = ValueKind::Integer;
    edit.integerValue = 36000;
    REQUIRE(throttlePage.writeDraft(
               throttlePage.context, "throttle-detent-center", &edit) == Result::Ok);
    REQUIRE(AbsoluteControlTelemetry::GetThrottleCaptureTarget() ==
        AbsoluteControlTelemetry::ThrottleCaptureTarget::None);
    throttlePage.cancel(throttlePage.context);

    edit = {};
    edit.kind = ValueKind::Float;
    edit.floatValue = 0.12;
    REQUIRE(throttlePage.writeDraft(
               throttlePage.context, "throttle-idle-plateau", &edit) == Result::Ok);
    REQUIRE(throttlePage.invokeAction(
               throttlePage.context, "throttle-capture-reverse") == Result::Ok);
    REQUIRE(throttlePage.invokeAction(
               throttlePage.context, "throttle-capture-reverse") == Result::Ok);
    REQUIRE(throttlePage.invokeAction(
               throttlePage.context, "throttle-capture-boost") == Result::Ok);
    REQUIRE(throttlePage.invokeAction(
               throttlePage.context, "throttle-link-idle-saturation") == Result::Ok);
    REQUIRE(AbsoluteControlTelemetry::GetThrottleCaptureTarget() ==
        AbsoluteControlTelemetry::ThrottleCaptureTarget::Boost);
    value = {};
    REQUIRE(throttlePage.readValue(
               throttlePage.context, "throttle-saturation", &value) == Result::Ok);
    REQUIRE(value.kind == ValueKind::Float && std::abs(value.floatValue - 0.88) < 1e-9);
    REQUIRE(throttlePage.apply(throttlePage.context) == Result::Ok);
    REQUIRE(AbsoluteControlTelemetry::GetThrottleCaptureTarget() ==
        AbsoluteControlTelemetry::ThrottleCaptureTarget::None);
    REQUIRE(AbsoluteControlSettings::GetInteger(
               TestSettings::stored,
               AbsoluteControlSettings::ScalarField::ReverseZoneCenter) == 41234);
    REQUIRE(AbsoluteControlSettings::GetInteger(
               TestSettings::stored,
               AbsoluteControlSettings::ScalarField::BoostZoneCenter) == 41234);

    edit = {};
    edit.kind = ValueKind::Integer;
    for (const auto rawLandmark : {32768LL, 32868LL, 32769LL}) {
        edit.integerValue = rawLandmark;
        REQUIRE(throttlePage.writeDraft(
                   throttlePage.context, "throttle-detent-center", &edit) == Result::Ok);
        value = {};
        REQUIRE(throttlePage.readValue(
                   throttlePage.context, "throttle-detent-center", &value) == Result::Ok);
        REQUIRE(value.kind == ValueKind::Integer);
        REQUIRE(value.integerValue == rawLandmark);
    }
    throttlePage.cancel(throttlePage.context);

    const auto& diagnosticsPage = g_copiedPages[7];
    edit = {};
    edit.kind = ValueKind::Integer;
    edit.integerValue = 501;
    REQUIRE(diagnosticsPage.writeDraft(
               diagnosticsPage.context, "pilot-latch-ms", &edit) == Result::InvalidArgument);
    std::array<ChoiceOptionV1, 3> options{};
    std::uint32_t optionCount{};
    REQUIRE(diagnosticsPage.readChoiceOptions(
               diagnosticsPage.context, "pilot-context-mode", options.data(),
               static_cast<std::uint32_t>(options.size()),
               &optionCount) == Result::Ok);
    REQUIRE(optionCount == 3);
    REQUIRE(options[1].value == 1);
    REQUIRE(std::strcmp(options[1].label, "Park flight controls") == 0);
    REQUIRE(diagnosticsPage.readChoiceOptions(
               diagnosticsPage.context, "pilot-context-mode", options.data(), 2, &optionCount) ==
           Result::CapacityExceeded);
    REQUIRE(optionCount == 3);

    REQUIRE(throttlePage.readChoiceOptions(
               throttlePage.context, "turn-assist-mode", options.data(),
               static_cast<std::uint32_t>(options.size()),
               &optionCount) == Result::Ok);
    REQUIRE(optionCount == 3);
    REQUIRE(options[2].value == 2);
    REQUIRE(std::strcmp(options[2].label, "Toggle on/off") == 0);

    auto expectedRoundTrip = TestSettings::stored;
    for (const auto& definition : AbsoluteControlSettings::Definitions()) {
        const auto* owner = FindPageForControl(
            g_copiedPages.data(), g_copiedPages.size(), definition.controlId);
        REQUIRE(owner && owner->writeDraft && owner->readValue);

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
        REQUIRE(owner->writeDraft(owner->context, definition.controlId.data(), &invalid) ==
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
        REQUIRE(owner->writeDraft(owner->context, definition.controlId.data(), &desired) ==
               Result::Ok);
        ValueV1 readBack{};
        REQUIRE(owner->readValue(
                   owner->context, definition.controlId.data(), &readBack) == Result::Ok);
        REQUIRE(readBack.kind == desired.kind);
        if (desired.kind == ValueKind::Boolean) {
            REQUIRE(readBack.booleanValue == desired.booleanValue);
        } else if (desired.kind == ValueKind::Integer) {
            REQUIRE(readBack.integerValue == desired.integerValue);
        } else {
            REQUIRE(std::abs(readBack.floatValue - desired.floatValue) < 1e-9);
        }
    }
    REQUIRE(axesPage.apply(axesPage.context) == Result::Ok);
    REQUIRE(AbsoluteControlSettings::Equivalent(
        TestSettings::stored, expectedRoundTrip));

    edit = {};
    edit.kind = ValueKind::Boolean;
    edit.booleanValue = 0;
    REQUIRE(axesPage.writeDraft(axesPage.context, "pitch-inverted", &edit) == Result::Ok);
    ++TestSettings::revision.sourceFingerprint;
    REQUIRE(axesPage.apply(axesPage.context) == Result::Rejected);
    axesPage.cancel(axesPage.context);
    value = {};
    REQUIRE(axesPage.readValue(axesPage.context, "pitch-inverted", &value) == Result::Ok);
    REQUIRE(value.booleanValue == 1);

    REQUIRE(axesPage.writeDraft(axesPage.context, "pitch-inverted", &edit) == Result::Ok);
    TestSettings::failApply = true;
    REQUIRE(axesPage.apply(axesPage.context) == Result::WriteFailure);
    TestSettings::failApply = false;
    value = {};
    REQUIRE(axesPage.readValue(axesPage.context, "pitch-inverted", &value) == Result::Ok);
    REQUIRE(value.booleanValue == 0);
    REQUIRE(axesPage.apply(axesPage.context) == Result::Ok);
    REQUIRE(!AbsoluteControlSettings::GetBoolean(
        TestSettings::stored,
        AbsoluteControlSettings::ScalarField::PitchInvert));

    AbsoluteControlSubscriber::Testing::ForceReadException(true);
    value = {};
    REQUIRE(g_copiedPages[8].readValue(
               g_copiedPages[8].context,
               "setup-control-host", &value) ==
           Result::Rejected);
    AbsoluteControlSubscriber::Testing::ForceReadException(false);

    ResetFakeHost();
    ApiV1 invalidApi = g_api;
    invalidApi.structSize = static_cast<std::uint32_t>(offsetof(ApiV1, registerModule));
    g_resolvedApi = &invalidApi;
    REQUIRE(AbsoluteControlSubscriber::Testing::RegisterWithResolver(&ResolveHost) ==
           Result::Rejected);
    REQUIRE(!AbsoluteControlSubscriber::IsHosted());

    return 0;
}

#include "ConfigOwnershipPolicy.h"

#include <SimpleIni.h>

#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

const char* Value(const CSimpleIniA& ini, const char* section, const char* key)
{
    const char* value = ini.GetValue(section, key, nullptr);
    return value ? value : "";
}

std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input.is_open());
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

} // namespace

int main()
{
    using namespace ConfigOwnershipPolicy;
    assert(IsStandaloneOwned("HeadTracking", "bEnabled"));
    assert(IsStandaloneOwned("Aim", "bHOSAMMode"));
    assert(IsStandaloneOwned("Aim", "fAlignmentDecayRate"));
    assert(IsStandaloneOwned("AbsoluteZero", "unknown"));
    assert(IsStandaloneOwned("Power", "preset"));
    assert(!IsStandaloneOwned("Aim", "fAimSensitivity"));
    assert(!IsStandaloneOwned("ShipButtons", "iIncreaseSystemPowerButton"));

    CSimpleIniA current;
    current.SetUnicode(false);
    current.SetValue("Hardware", "iThrottleAxis", "#0@0x32");
    current.SetValue("Aim", "fAimSensitivity", "1.0");
    current.SetValue("Aim", "bHOSAMMode", "true");
    current.SetValue("Aim", "externalFutureKey", "preserve-aim");
    current.SetValue("HeadTracking", "bEnabled", "true");
    current.SetValue("HeadTracking", "futureKey", "preserve-head");
    current.SetValue("Power", "futureKey", "preserve-power");
    current.SetValue("Foreign", "opaque", "preserve-foreign");
    current.SetValue("Calibration", "iCalib_0_0x30", "1,2");
    current.SetValue("Macro:Old", "Step0", "Old tap 1 0");

    CSimpleIniA incoming;
    incoming.SetUnicode(false);
    incoming.SetValue("Hardware", "iThrottleAxis", "#2@0x32");
    incoming.SetValue("Aim", "fAimSensitivity", "2.0");
    incoming.SetValue("Aim", "bHOSAMMode", "false");
    incoming.SetValue("HeadTracking", "bEnabled", "false");
    incoming.SetValue("Foreign", "opaque", "must-not-import");
    incoming.SetValue("Calibration", "iCalib_2_0x30", "10,65000");
    incoming.SetValue("Macro:New", "Step0", "New tap 1 0");

    CSimpleIniA managed;
    managed.SetUnicode(false);
    managed.SetValue("Hardware", "iThrottleAxis", "");
    managed.SetValue("Aim", "fAimSensitivity", "");
    ReplaceManagedPayload(current, incoming, managed);

    assert(std::strcmp(Value(current, "Hardware", "iThrottleAxis"),
                       "#2@0x32") == 0);
    assert(std::strcmp(Value(current, "Aim", "fAimSensitivity"), "2.0") == 0);
    assert(std::strcmp(Value(current, "Aim", "bHOSAMMode"), "true") == 0);
    assert(std::strcmp(Value(current, "Aim", "externalFutureKey"),
                       "preserve-aim") == 0);
    assert(std::strcmp(Value(current, "HeadTracking", "bEnabled"), "true") == 0);
    assert(std::strcmp(Value(current, "HeadTracking", "futureKey"),
                       "preserve-head") == 0);
    assert(std::strcmp(Value(current, "Power", "futureKey"),
                       "preserve-power") == 0);
    assert(std::strcmp(Value(current, "Foreign", "opaque"),
                       "preserve-foreign") == 0);
    assert(!current.GetValue("Calibration", "iCalib_0_0x30", nullptr));
    assert(std::strcmp(Value(current, "Calibration", "iCalib_2_0x30"),
                       "10,65000") == 0);
    assert(!current.GetValue("Macro:Old", "Step0", nullptr));
    assert(std::strcmp(Value(current, "Macro:New", "Step0"),
                       "New tap 1 0") == 0);

    CSimpleIniA exported;
    exported.SetUnicode(false);
    exported.SetValue("Aim", "fAimSensitivity", "2.0");
    exported.SetValue("Aim", "bAlignmentAssist", "true");
    exported.SetValue("HeadTracking", "bEnabled", "true");
    exported.SetValue("Power", "futureKey", "external");
    RemoveStandaloneOwned(exported);
    assert(std::strcmp(Value(exported, "Aim", "fAimSensitivity"), "2.0") == 0);
    assert(!exported.GetValue("Aim", "bAlignmentAssist", nullptr));
    assert(!exported.GetValue("HeadTracking", "bEnabled", nullptr));
    assert(!exported.GetValue("Power", "futureKey", nullptr));

    // Codec and UI boundary regression: the legacy serializer has no writer for
    // moved fields; device reassignment/capture omit Head Tracking; the remaining
    // legacy tabs compile only read-only ownership status for these modules.
    const auto root = std::filesystem::current_path();
    const auto codec = Read(root / "src/WizardConfigCodec.cpp");
    assert(codec.find("ini.SetBoolValue(\"HeadTracking\"") == std::string::npos);
    assert(codec.find("ini.SetBoolValue(\"Aim\", \"bHOSAMMode\"") ==
           std::string::npos);
    assert(codec.find("ini.SetBoolValue(\"Aim\", \"bAlignmentAssist\"") ==
           std::string::npos);

    const auto devices = Read(root / "src/WizardAdvancedPages.cpp");
    assert(devices.find("allBindings.push_back(&s.headLook") ==
           std::string::npos);
    const auto capture = Read(root / "src/WizardProfileUI.cpp");
    assert(capture.find("s.headLookRecenterBinding = binding") ==
           std::string::npos);
    assert(capture.find("s.headLookToggleBinding = binding") ==
           std::string::npos);
    const auto axesUi = Read(root / "src/WizardFlightAxesPage.cpp");
    assert(axesUi.find("ImGui::Checkbox(\"Mouse steering (HOSAM)\"") ==
           std::string::npos);
    assert(axesUi.find("ImGui::Checkbox(\"Alignment assist\"") ==
           std::string::npos);
    const auto cameraUi = Read(root / "src/WizardTunePages.cpp");
    assert(cameraUi.find("legacy HOTAS fallback is read-only") !=
           std::string::npos);
    assert(cameraUi.find("ImGui::Checkbox(\"Enable camera look\"") ==
           std::string::npos);
    assert(cameraUi.find("CaptureSlot::kHeadLook") == std::string::npos);

    const auto workbench = Read(root / "src/BindingWizard.cpp");
    assert(workbench.find("PowerModuleUI") == std::string::npos);
    assert(workbench.find("Page::Power") == std::string::npos);
    assert(workbench.find("Power##Primary") == std::string::npos);
    const auto session = Read(root / "include/WizardSession.h");
    assert(session.find("enum class Page { Bind, Tune, Advanced, Power }") ==
           std::string::npos);

    // The legacy workbench remains historical source reference only. The shipping
    // plugin has one frontend: Absolute Control. Keep renderer hooks, ImGui,
    // MinHook, D3D12, and DXGI out of the target so graphics stacks cannot share
    // an interception surface with HOTAS.
    const auto build = Read(root / "xmake.lua");
    assert(build.find("add_requires(\"imgui") == std::string::npos);
    assert(build.find("add_requires(\"minhook") == std::string::npos);
    assert(build.find("add_packages(\"imgui") == std::string::npos);
    assert(build.find("\"d3d12\"") == std::string::npos);
    assert(build.find("\"dxgi\"") == std::string::npos);
    assert(build.find("remove_files(") != std::string::npos);
    assert(build.find("\"src/UIHook.cpp\"") != std::string::npos);
    assert(build.find("\"src/BindingWizard.cpp\"") != std::string::npos);

    const auto mainSource = Read(root / "src/Main.cpp");
    assert(mainSource.find("UIHook") == std::string::npos);
    assert(mainSource.find("BindingWizard") == std::string::npos);
    const auto controller = Read(root / "src/ThrottleController.cpp");
    assert(controller.find("UIHook") == std::string::npos);
    assert(controller.find("RequestHostPage(\"hotas-setup\")") !=
           std::string::npos);
    return 0;
}

#include "ConfigOwnershipPolicy.h"

#include <SimpleIni.h>

#include "TestCheck.h"
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
    REQUIRE(input.is_open());
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

} // namespace

int main()
{
    using namespace ConfigOwnershipPolicy;
    REQUIRE(IsStandaloneOwned("HeadTracking", "bEnabled"));
    REQUIRE(IsStandaloneOwned("Aim", "bHOSAMMode"));
    REQUIRE(IsStandaloneOwned("Aim", "fAlignmentDecayRate"));
    REQUIRE(IsStandaloneOwned("AbsoluteZero", "unknown"));
    REQUIRE(IsStandaloneOwned("Power", "preset"));
    REQUIRE(!IsStandaloneOwned("Aim", "fAimSensitivity"));
    REQUIRE(!IsStandaloneOwned("ShipButtons", "iIncreaseSystemPowerButton"));

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

    REQUIRE(std::strcmp(Value(current, "Hardware", "iThrottleAxis"),
                       "#2@0x32") == 0);
    REQUIRE(std::strcmp(Value(current, "Aim", "fAimSensitivity"), "2.0") == 0);
    REQUIRE(std::strcmp(Value(current, "Aim", "bHOSAMMode"), "true") == 0);
    REQUIRE(std::strcmp(Value(current, "Aim", "externalFutureKey"),
                       "preserve-aim") == 0);
    REQUIRE(std::strcmp(Value(current, "HeadTracking", "bEnabled"), "true") == 0);
    REQUIRE(std::strcmp(Value(current, "HeadTracking", "futureKey"),
                       "preserve-head") == 0);
    REQUIRE(std::strcmp(Value(current, "Power", "futureKey"),
                       "preserve-power") == 0);
    REQUIRE(std::strcmp(Value(current, "Foreign", "opaque"),
                       "preserve-foreign") == 0);
    REQUIRE(!current.GetValue("Calibration", "iCalib_0_0x30", nullptr));
    REQUIRE(std::strcmp(Value(current, "Calibration", "iCalib_2_0x30"),
                       "10,65000") == 0);
    REQUIRE(!current.GetValue("Macro:Old", "Step0", nullptr));
    REQUIRE(std::strcmp(Value(current, "Macro:New", "Step0"),
                       "New tap 1 0") == 0);

    CSimpleIniA exported;
    exported.SetUnicode(false);
    exported.SetValue("Aim", "fAimSensitivity", "2.0");
    exported.SetValue("Aim", "bAlignmentAssist", "true");
    exported.SetValue("HeadTracking", "bEnabled", "true");
    exported.SetValue("Power", "futureKey", "external");
    RemoveStandaloneOwned(exported);
    REQUIRE(std::strcmp(Value(exported, "Aim", "fAimSensitivity"), "2.0") == 0);
    REQUIRE(!exported.GetValue("Aim", "bAlignmentAssist", nullptr));
    REQUIRE(!exported.GetValue("HeadTracking", "bEnabled", nullptr));
    REQUIRE(!exported.GetValue("Power", "futureKey", nullptr));

    // Codec and UI boundary regression: the legacy serializer has no writer for
    // moved fields; device reassignment/capture omit Head Tracking; the remaining
    // legacy tabs compile only read-only ownership status for these modules.
    const auto root = std::filesystem::current_path();
    const auto codec = Read(root / "src/WizardConfigCodec.cpp");
    REQUIRE(codec.find("ini.SetBoolValue(\"HeadTracking\"") == std::string::npos);
    REQUIRE(codec.find("ini.SetBoolValue(\"Aim\", \"bHOSAMMode\"") ==
           std::string::npos);
    REQUIRE(codec.find("ini.SetBoolValue(\"Aim\", \"bAlignmentAssist\"") ==
           std::string::npos);

    const auto devices = Read(root / "src/WizardAdvancedPages.cpp");
    REQUIRE(devices.find("allBindings.push_back(&s.headLook") ==
           std::string::npos);
    const auto capture = Read(root / "src/WizardProfileUI.cpp");
    REQUIRE(capture.find("s.headLookRecenterBinding = binding") ==
           std::string::npos);
    REQUIRE(capture.find("s.headLookToggleBinding = binding") ==
           std::string::npos);
    const auto axesUi = Read(root / "src/WizardFlightAxesPage.cpp");
    REQUIRE(axesUi.find("ImGui::Checkbox(\"Mouse steering (HOSAM)\"") ==
           std::string::npos);
    REQUIRE(axesUi.find("ImGui::Checkbox(\"Alignment assist\"") ==
           std::string::npos);
    const auto cameraUi = Read(root / "src/WizardTunePages.cpp");
    REQUIRE(cameraUi.find("legacy HOTAS fallback is read-only") !=
           std::string::npos);
    REQUIRE(cameraUi.find("ImGui::Checkbox(\"Enable camera look\"") ==
           std::string::npos);
    REQUIRE(cameraUi.find("CaptureSlot::kHeadLook") == std::string::npos);

    const auto workbench = Read(root / "src/BindingWizard.cpp");
    REQUIRE(workbench.find("PowerModuleUI") == std::string::npos);
    REQUIRE(workbench.find("Page::Power") == std::string::npos);
    REQUIRE(workbench.find("Power##Primary") == std::string::npos);
    const auto session = Read(root / "include/WizardSession.h");
    REQUIRE(session.find("enum class Page { Bind, Tune, Advanced, Power }") ==
           std::string::npos);

    // The legacy workbench remains historical source reference only. The shipping
    // plugin has one frontend: Absolute Control. Keep renderer hooks, ImGui,
    // MinHook, D3D12, and DXGI out of the target so graphics stacks cannot share
    // an interception surface with HOTAS.
    const auto build = Read(root / "xmake.lua");
    REQUIRE(build.find("add_requires(\"imgui") == std::string::npos);
    REQUIRE(build.find("add_requires(\"minhook") == std::string::npos);
    REQUIRE(build.find("add_packages(\"imgui") == std::string::npos);
    REQUIRE(build.find("\"d3d12\"") == std::string::npos);
    REQUIRE(build.find("\"dxgi\"") == std::string::npos);
    REQUIRE(build.find("remove_files(") != std::string::npos);
    REQUIRE(build.find("\"src/UIHook.cpp\"") != std::string::npos);
    REQUIRE(build.find("\"src/BindingWizard.cpp\"") != std::string::npos);

    const auto mainSource = Read(root / "src/Main.cpp");
    REQUIRE(mainSource.find("UIHook") == std::string::npos);
    REQUIRE(mainSource.find("BindingWizard") == std::string::npos);
    const auto controller = Read(root / "src/ThrottleController.cpp");
    REQUIRE(controller.find("UIHook") == std::string::npos);
    REQUIRE(controller.find("RequestHostPage(\"hotas-setup\")") !=
           std::string::npos);
    return 0;
}

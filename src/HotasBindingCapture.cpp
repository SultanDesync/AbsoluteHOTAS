#include "PCH.h"

#include "HotasBindingCapture.h"

#include "WizardCapture.h"

namespace {

std::string g_capturedBinding;

void OnCaptureCommit(int, const char* binding)
{
    g_capturedBinding = binding ? binding : "";
}

} // namespace

namespace HotasBindingCapture {

void Begin(const HotasBindingCatalog::Target& target)
{
    WizardCapture::CancelCapture();
    g_capturedBinding.clear();
    if (target.captureKind == HotasBindingCatalog::CaptureKind::Axis) {
        WizardCapture::StartAxisCapture(target.captureSlot,
                                        target.displayLabel.data());
    } else {
        WizardCapture::StartButtonCapture(
            0, target.captureSlot, target.displayLabel.data());
    }
}

void BeginButton(int captureSlot, std::string_view label,
                 int settleWindowMilliseconds)
{
    WizardCapture::CancelCapture();
    g_capturedBinding.clear();
    const std::string ownedLabel{label};
    WizardCapture::StartButtonCapture(
        0, captureSlot, ownedLabel.c_str(), settleWindowMilliseconds);
}

PollState Poll(std::string& binding)
{
    auto& pending = WizardCapture::GetPendingBind();
    if (!pending.active) return PollState::TimedOut;
    if (WizardCapture::UpdateCapture(&OnCaptureCommit)) {
        binding = g_capturedBinding;
        return PollState::Captured;
    }
    return pending.active ? PollState::Capturing : PollState::TimedOut;
}

void Cancel() noexcept
{
    WizardCapture::CancelCapture();
    g_capturedBinding.clear();
}

} // namespace HotasBindingCapture

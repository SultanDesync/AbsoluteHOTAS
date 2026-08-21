#include "AbsoluteControlDevices.h"

#include "BindingRef.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <format>
#include <ranges>

namespace {

std::uint64_t Hash(std::string_view value) noexcept
{
    std::uint64_t result = 1469598103934665603ULL;
    for (const unsigned char byte : value) {
        result ^= byte;
        result *= 1099511628211ULL;
    }
    return result;
}

bool SameProduct(const AbsoluteControlDevices::RuntimeDevice& left,
                 const AbsoluteControlDevices::RuntimeDevice& right) noexcept
{
    const bool hasProductGuid = std::ranges::any_of(
        left.productGuid, [](std::uint8_t byte) { return byte != 0; });
    if (hasProductGuid) return left.productGuid == right.productGuid;
    return left.vendorId == right.vendorId && left.productId == right.productId &&
           left.productName == right.productName;
}

void SwapCalibration(AbsoluteControlDevices::CalibrationMap& calibration,
                     std::uint32_t left, std::uint32_t right,
                     std::size_t& changed)
{
    AbsoluteControlDevices::CalibrationMap output;
    output.reserve(calibration.size());
    for (const auto& [key, range] : calibration) {
        auto device = static_cast<std::uint32_t>(key >> 8);
        if (device == left) {
            device = right;
            ++changed;
        } else if (device == right) {
            device = left;
            ++changed;
        }
        output[(static_cast<int>(device) << 8) | (key & 0xFF)] = range;
    }
    calibration.swap(output);
}

} // namespace

namespace AbsoluteControlDevices {

std::string StableRecordId(std::string_view persistentId) noexcept
{
    try {
        std::string hex;
        hex.reserve(32);
        for (const unsigned char ch : persistentId) {
            if (std::isxdigit(ch)) {
                hex.push_back(static_cast<char>(std::tolower(ch)));
            }
        }
        if (hex.size() == 32) return "device-" + hex;
        return std::format("device-{:016x}", Hash(persistentId));
    } catch (...) {
        return "device-invalid";
    }
}

void Session::Publish(const RuntimeDevice* devices, std::size_t count)
{
    devices_.clear();
    records_.clear();
    if (devices && count != 0) {
        const auto bounded = (std::min)(count, kMaximumDevices);
        devices_.assign(devices, devices + bounded);
    }

    records_.reserve(devices_.size());
    for (const auto& device : devices_) {
        std::size_t duplicateCount{};
        for (const auto& candidate : devices_) {
            if (SameProduct(device, candidate)) ++duplicateCount;
        }

        DeviceRecord record;
        record.recordId = StableRecordId(device.persistentId);
        record.label = device.productName.empty() ? device.instanceName : device.productName;
        if (record.label.empty()) record.label = "DirectInput device";
        record.summary = std::format(
            "#{} | {} axes | {} buttons | {}",
            device.deviceIndex, (std::min<std::uint32_t>)(device.axisCount, kAxisCount),
            device.buttonCount, device.connected ? "connected" : "unavailable");
        record.detail = std::format(
            "{}{}{}",
            device.instanceName.empty() ? "Identity " : device.instanceName + " | ",
            device.persistentId,
            duplicateCount > 1 ? " | duplicate product; index bindings can be reassigned" : "");
        if (!device.connected) record.flags |= 1U << 1;
        record.deviceIndex = device.deviceIndex;
        record.persistentId = device.persistentId;
        records_.push_back(std::move(record));
    }

    if (!Find(selectedRecordId_)) {
        selectedRecordId_ = records_.empty() ? "" : records_.front().recordId;
    }
    if (!Find(reassignmentTargetId_) || reassignmentTargetId_ == selectedRecordId_) {
        reassignmentTargetId_.clear();
    }
    ObserveCalibration();
}

const std::vector<DeviceRecord>& Session::Records() const noexcept
{
    return records_;
}

std::string_view Session::SelectedRecordId() const noexcept
{
    return selectedRecordId_;
}

std::string_view Session::ReassignmentTargetId() const noexcept
{
    return reassignmentTargetId_;
}

const RuntimeDevice* Session::SelectedDevice() const noexcept
{
    return Find(selectedRecordId_);
}

const CalibrationState& Session::Calibration() const noexcept
{
    return calibration_;
}

const RuntimeDevice* Session::Find(std::string_view recordId) const noexcept
{
    const auto found = std::ranges::find_if(devices_, [&](const auto& device) {
        return StableRecordId(device.persistentId) == recordId;
    });
    return found == devices_.end() ? nullptr : &*found;
}

bool Session::Select(std::string_view recordId) noexcept
{
    if (!Find(recordId)) return false;
    if (calibration_.active && recordId != selectedRecordId_) CancelCalibration();
    selectedRecordId_ = recordId;
    if (reassignmentTargetId_ == selectedRecordId_) reassignmentTargetId_.clear();
    return true;
}

bool Session::SelectReassignmentTarget(std::string_view recordId) noexcept
{
    const auto* source = SelectedDevice();
    const auto* target = Find(recordId);
    if (!source || !target || source == target || !SameProduct(*source, *target)) {
        return false;
    }
    reassignmentTargetId_ = recordId;
    return true;
}

bool Session::BeginCalibration() noexcept
{
    const auto* device = SelectedDevice();
    if (!device || !device->connected) return false;
    calibration_ = {};
    calibration_.active = true;
    calibration_.deviceRecordId = selectedRecordId_;
    calibration_.observedMinimum = device->rawAxes;
    calibration_.observedMaximum = device->rawAxes;
    return true;
}

void Session::CancelCalibration() noexcept
{
    calibration_ = {};
}

void Session::ObserveCalibration() noexcept
{
    if (!calibration_.active) return;
    const auto* device = Find(calibration_.deviceRecordId);
    if (!device || !device->connected) return;
    calibration_.activeAxisMask = 0;
    for (std::size_t axis = 0; axis < kAxisCount; ++axis) {
        calibration_.observedMinimum[axis] = (std::min)(
            calibration_.observedMinimum[axis], device->rawAxes[axis]);
        calibration_.observedMaximum[axis] = (std::max)(
            calibration_.observedMaximum[axis], device->rawAxes[axis]);
        if (calibration_.observedMaximum[axis] -
                calibration_.observedMinimum[axis] > kGhostThreshold) {
            calibration_.activeAxisMask |= 1U << axis;
        }
    }
}

std::size_t Session::CommitCalibration(CalibrationMap& calibration) noexcept
{
    if (!calibration_.active) return 0;
    const auto* device = Find(calibration_.deviceRecordId);
    if (!device) return 0;
    std::size_t saved{};
    for (std::size_t axis = 0; axis < kAxisCount; ++axis) {
        if ((calibration_.activeAxisMask & (1U << axis)) == 0) continue;
        const int key = (static_cast<int>(device->deviceIndex) << 8) |
                        (0x30 + static_cast<int>(axis));
        calibration[key] = {calibration_.observedMinimum[axis],
                            calibration_.observedMaximum[axis]};
        ++saved;
    }
    CancelCalibration();
    return saved;
}

std::size_t Session::ClearCalibration(CalibrationMap& calibration) noexcept
{
    const auto* device = SelectedDevice();
    if (!device) return 0;
    std::size_t removed{};
    for (std::size_t axis = 0; axis < kAxisCount; ++axis) {
        const int key = (static_cast<int>(device->deviceIndex) << 8) |
                        (0x30 + static_cast<int>(axis));
        removed += calibration.erase(key);
    }
    if (calibration_.deviceRecordId == selectedRecordId_) CancelCalibration();
    return removed;
}

ReassignResult Session::ReassignDuplicate(
    HotasBindingCatalog::BindingState& bindings,
    CalibrationMap& calibration) const
{
    ReassignResult result;
    const auto* source = SelectedDevice();
    const auto* target = Find(reassignmentTargetId_);
    if (!source || !target || source == target) {
        result.detail = "Choose two different connected records.";
        return result;
    }
    if (!SameProduct(*source, *target)) {
        result.detail = "Reassignment is limited to duplicate product identities.";
        return result;
    }

    for (std::size_t index = 0; index < bindings.size(); ++index) {
        auto binding = ParseBindingRef(bindings[index].c_str(), -1);
        if (!binding.HasIndex()) continue;
        if (binding.deviceIndex == static_cast<int>(source->deviceIndex)) {
            binding.deviceIndex = static_cast<int>(target->deviceIndex);
        } else if (binding.deviceIndex == static_cast<int>(target->deviceIndex)) {
            binding.deviceIndex = static_cast<int>(source->deviceIndex);
        } else {
            continue;
        }
        bindings[index] = FormatBindingRef(
            binding,
            HotasBindingCatalog::kTargets[index].captureKind ==
                HotasBindingCatalog::CaptureKind::Axis);
        ++result.bindingChanges;
    }
    SwapCalibration(calibration, source->deviceIndex, target->deviceIndex,
                    result.calibrationChanges);
    result.accepted = true;
    result.detail = std::format(
        "Reassigned {} fixed HOTAS bindings and {} calibration entries between #{} and #{}.",
        result.bindingChanges, result.calibrationChanges,
        source->deviceIndex, target->deviceIndex);
    return result;
}

} // namespace AbsoluteControlDevices

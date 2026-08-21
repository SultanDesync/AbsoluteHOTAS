#pragma once

#include "AbsoluteInputBusAPI.h"
#include "HotasBindingCatalog.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace AbsoluteControlDevices {

inline constexpr std::size_t kMaximumDevices = 64;
inline constexpr std::size_t kAxisCount = AbsoluteInputBusApi::kAxisCount;
inline constexpr std::int32_t kGhostThreshold = 5000;

using CalibrationMap =
    std::unordered_map<int, std::pair<long, long>>;

struct RuntimeDevice {
    std::uint32_t deviceIndex{};
    std::array<std::uint8_t, 16> instanceGuid{};
    std::array<std::uint8_t, 16> productGuid{};
    std::uint16_t vendorId{};
    std::uint16_t productId{};
    std::uint32_t axisCount{};
    std::uint32_t buttonCount{};
    std::string persistentId;
    std::string instanceName;
    std::string productName;
    bool connected{};
    std::array<std::int32_t, kAxisCount> rawAxes{};
    std::array<float, kAxisCount> normalizedAxes{};
};

struct DeviceRecord {
    std::string recordId;
    std::string label;
    std::string summary;
    std::string detail;
    std::uint32_t flags{};
    std::uint32_t deviceIndex{};
    std::string persistentId;
};

struct CalibrationState {
    bool active{};
    std::string deviceRecordId;
    std::array<std::int32_t, kAxisCount> observedMinimum{};
    std::array<std::int32_t, kAxisCount> observedMaximum{};
    std::uint32_t activeAxisMask{};
};

struct ReassignResult {
    bool accepted{};
    std::size_t bindingChanges{};
    std::size_t calibrationChanges{};
    std::string detail;
};

class Session {
public:
    void Publish(const RuntimeDevice* devices, std::size_t count);

    [[nodiscard]] const std::vector<DeviceRecord>& Records() const noexcept;
    [[nodiscard]] std::string_view SelectedRecordId() const noexcept;
    [[nodiscard]] std::string_view ReassignmentTargetId() const noexcept;
    [[nodiscard]] const RuntimeDevice* SelectedDevice() const noexcept;
    [[nodiscard]] const CalibrationState& Calibration() const noexcept;

    [[nodiscard]] bool Select(std::string_view recordId) noexcept;
    [[nodiscard]] bool SelectReassignmentTarget(std::string_view recordId) noexcept;
    [[nodiscard]] bool BeginCalibration() noexcept;
    void CancelCalibration() noexcept;
    [[nodiscard]] std::size_t CommitCalibration(CalibrationMap& calibration) noexcept;
    [[nodiscard]] std::size_t ClearCalibration(CalibrationMap& calibration) noexcept;

    [[nodiscard]] ReassignResult ReassignDuplicate(
        HotasBindingCatalog::BindingState& bindings,
        CalibrationMap& calibration) const;

private:
    [[nodiscard]] const RuntimeDevice* Find(std::string_view recordId) const noexcept;
    void ObserveCalibration() noexcept;

    std::vector<RuntimeDevice> devices_;
    std::vector<DeviceRecord> records_;
    std::string selectedRecordId_;
    std::string reassignmentTargetId_;
    CalibrationState calibration_{};
};

[[nodiscard]] std::string StableRecordId(std::string_view persistentId) noexcept;

} // namespace AbsoluteControlDevices

#include "PCH.h"

#include "AbsoluteControlProfiles.h"

#include "BindingRef.h"
#include <algorithm>
#include <format>
#include <unordered_set>

namespace {

std::string RecordIdFor(std::string_view name)
{
    constexpr std::uint64_t offset = 1469598103934665603ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    auto hash = offset;
    for (const unsigned char ch : name) {
        hash ^= ch;
        hash *= prime;
    }
    return std::format("profile-{:016x}", hash);
}

bool ValidMode(std::string_view mode) noexcept
{
    return mode == "momentary" || mode == "toggle" || mode == "selector";
}

std::string CanonicalButton(std::string_view raw)
{
    if (raw.empty() || raw == "-1" || raw == "(unbound)") return "(unbound)";
    const std::string owned{raw};
    const auto parsed = ParseBindingRef(owned.c_str(), -1);
    if (parsed.value < 1 || parsed.value > 144) return {};
    return FormatBindingRef(parsed, false);
}

} // namespace

namespace AbsoluteControlProfiles {

bool Session::Open(std::string& error)
{
    error.clear();
    const auto current = repository_.CurrentEditTarget();
    if (!Refresh(current, error)) return false;
    loaded_ = true;
    return true;
}

bool Session::Refresh(std::string_view preferredConfiguration, std::string& error)
{
    std::vector<Record> records;
    std::string mainTrigger;
    std::string mainMode;
    repository_.GetMainActivation(mainTrigger, mainMode);
    if (!ValidMode(mainMode)) mainMode = "momentary";
    records.push_back({
        .recordId = std::string(kMainRecordId),
        .configurationName = {},
        .label = "Main controls",
        .kind = "main",
        .filename = "AbsoluteHOTAS_Custom.ini",
        .keyboardShortcut = "(unbound)",
        .activationTrigger = CanonicalButton(mainTrigger),
        .activationMode = mainMode,
        .inheritsMain = false,
    });

    if (records.front().activationTrigger.empty()) {
        records.front().activationTrigger = "(unbound)";
    }

    std::unordered_set<std::string> ids{records.front().recordId};
    for (auto& summary : repository_.List()) {
        Record record{
            .recordId = RecordIdFor(summary.name),
            .configurationName = summary.name,
            .label = summary.name,
            .kind = summary.kind,
            .filename = summary.filename,
            .keyboardShortcut = summary.keyboardShortcut,
            .activationTrigger = CanonicalButton(summary.trigger),
            .activationMode = ValidMode(summary.mode) ? summary.mode : "momentary",
            .sequence = summary.sequence,
            .slot = summary.slot,
            .overrideCount = summary.overrideCount,
            .inheritsMain = summary.kind == "overlay",
        };
        if (record.activationTrigger.empty()) record.activationTrigger = "(unbound)";
        if (!ids.insert(record.recordId).second) {
            error = "Two HOTAS profiles produced the same stable Control record ID.";
            return false;
        }
        records.push_back(std::move(record));
    }

    const auto selected = std::ranges::find(
        records, preferredConfiguration, &Record::configurationName);
    selectedRecordId_ = selected == records.end()
        ? std::string(kMainRecordId) : selected->recordId;
    records_ = std::move(records);
    savedRecords_ = records_;
    savedOperationName_ = operationName_;
    pendingRecordId_.clear();
    ++generation_;
    return true;
}

const Record* Session::Selected() const noexcept
{
    const auto found = std::ranges::find(
        records_, selectedRecordId_, &Record::recordId);
    return found == records_.end() ? nullptr : &*found;
}

Record* Session::MutableSelected() noexcept
{
    const auto found = std::ranges::find(
        records_, selectedRecordId_, &Record::recordId);
    return found == records_.end() ? nullptr : &*found;
}

bool Session::Dirty() const
{
    if (!loaded_ || operationName_ != savedOperationName_ ||
        records_.size() != savedRecords_.size()) {
        return loaded_ && (operationName_ != savedOperationName_ ||
                           records_.size() != savedRecords_.size());
    }
    for (std::size_t index = 0; index < records_.size(); ++index) {
        if (records_[index].activationTrigger !=
                savedRecords_[index].activationTrigger ||
            records_[index].activationMode !=
                savedRecords_[index].activationMode) {
            return true;
        }
    }
    return repository_.HasEditTargetChanges();
}

SelectResult Session::Select(std::string_view recordId, std::string& error)
{
    error.clear();
    const auto found = std::ranges::find(records_, recordId, &Record::recordId);
    if (found == records_.end()) return SelectResult::NotFound;
    if (found->recordId == selectedRecordId_) return SelectResult::Selected;
    if (Dirty()) {
        pendingRecordId_ = found->recordId;
        return SelectResult::NeedsResolution;
    }
    return SelectNow(recordId, error) ? SelectResult::Selected : SelectResult::Failed;
}

bool Session::SelectNow(std::string_view recordId, std::string& error)
{
    const auto found = std::ranges::find(records_, recordId, &Record::recordId);
    if (found == records_.end()) {
        error = "The selected HOTAS profile is no longer available.";
        return false;
    }
    if (!repository_.LoadEditTarget(found->configurationName, error)) return false;
    selectedRecordId_ = found->recordId;
    pendingRecordId_.clear();
    ++generation_;
    return true;
}

bool Session::ResolveSwitch(SwitchChoice choice, std::string& error)
{
    error.clear();
    if (pendingRecordId_.empty()) return true;
    if (choice == SwitchChoice::Cancel) {
        pendingRecordId_.clear();
        return true;
    }
    const std::string target = pendingRecordId_;
    if (choice == SwitchChoice::Save) {
        if (!Apply(error)) return false;
    } else if (!Cancel(error)) {
        return false;
    }
    return SelectNow(target, error);
}

bool Session::SetActivationMode(std::string_view mode)
{
    auto* selected = MutableSelected();
    if (!selected || !ValidMode(mode)) return false;
    selected->activationMode = mode;
    return true;
}

BindingResult Session::SetActivationBinding(std::string_view raw)
{
    auto* selected = MutableSelected();
    if (!selected) return BindingResult::NotFound;
    const auto binding = CanonicalButton(raw);
    if (binding.empty()) return BindingResult::Invalid;
    if (binding != "(unbound)") {
        for (const auto& record : records_) {
            if (record.recordId != selected->recordId &&
                record.activationTrigger == binding) {
                return BindingResult::Conflict;
            }
        }
    }
    selected->activationTrigger = binding;
    return BindingResult::Ok;
}

BindingResult Session::ReassignActivationBinding(std::string_view raw)
{
    auto* selected = MutableSelected();
    if (!selected) return BindingResult::NotFound;
    const auto binding = CanonicalButton(raw);
    if (binding.empty() || binding == "(unbound)") return BindingResult::Invalid;
    bool previous{};
    for (auto& record : records_) {
        if (record.recordId != selected->recordId &&
            record.activationTrigger == binding) {
            record.activationTrigger = "(unbound)";
            previous = true;
        }
    }
    if (!previous) return BindingResult::NotFound;
    selected->activationTrigger = binding;
    return BindingResult::Ok;
}

bool Session::ClearActivationBinding()
{
    auto* selected = MutableSelected();
    if (!selected) return false;
    selected->activationTrigger = "(unbound)";
    return true;
}

bool Session::Apply(std::string& error)
{
    error.clear();
    if (repository_.HasEditTargetChanges() &&
        !repository_.SaveEditTarget(error)) {
        return false;
    }
    std::vector<WizardConfig::ProfileActivationUpdate> updates;
    for (std::size_t index = 0; index < records_.size(); ++index) {
        if (records_[index].activationTrigger ==
                savedRecords_[index].activationTrigger &&
            records_[index].activationMode ==
                savedRecords_[index].activationMode) {
            continue;
        }
        updates.push_back({
            records_[index].configurationName,
            records_[index].activationTrigger,
            records_[index].activationMode,
        });
    }
    if (!repository_.SaveActivations(updates, error)) return false;
    savedRecords_ = records_;
    savedOperationName_ = operationName_;
    ++generation_;
    return true;
}

void Session::RestoreDraft() noexcept
{
    records_ = savedRecords_;
    operationName_ = savedOperationName_;
    pendingRecordId_.clear();
}

bool Session::Cancel(std::string& error)
{
    error.clear();
    if (repository_.HasEditTargetChanges() &&
        !repository_.DiscardEditTarget(error)) {
        return false;
    }
    RestoreDraft();
    ++generation_;
    return true;
}

bool Session::CreateOverlay(std::string& error)
{
    if (operationName_.empty()) {
        error = "Enter a name for the new binding layer.";
        return false;
    }
    const std::string name = operationName_;
    if (!repository_.CreateOverlay(name, error) || !Refresh(name, error)) return false;
    operationName_.clear();
    savedOperationName_.clear();
    return SelectNow(selectedRecordId_, error);
}

bool Session::ExportMainFull(std::string& error)
{
    const auto* selected = Selected();
    if (!selected || !selected->configurationName.empty()) {
        error = "Select Main controls before exporting a full profile.";
        return false;
    }
    if (operationName_.empty()) {
        error = "Enter a name for the exported profile.";
        return false;
    }
    const std::string name = operationName_;
    if (!repository_.ExportFull(name, error) || !Refresh({}, error)) return false;
    operationName_.clear();
    savedOperationName_.clear();
    return true;
}

bool Session::ImportSelectedFull(std::string& error)
{
    const auto* selected = Selected();
    if (!selected || selected->kind != "full") {
        error = "Select an independent full profile to import as Main controls.";
        return false;
    }
    if (!repository_.ImportFull(selected->configurationName, error)) return false;
    return Refresh({}, error) && SelectNow(kMainRecordId, error);
}

bool Session::ResetMain(std::string& error)
{
    if (!repository_.ResetMain(error)) return false;
    return Refresh({}, error) && SelectNow(kMainRecordId, error);
}

} // namespace AbsoluteControlProfiles

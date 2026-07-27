#include <rbfsafe/rbfsafe.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace {

rbfsafe::PolicyCalibrationProfileInput profile_input() {
    rbfsafe::PolicyCalibrationProfileInput input;
    input.policy_id = "vla-policy-a";
    input.policy_model_digest = std::string(64, 'a');
    input.scope_id = "factory-cell-a";
    input.task_id = "shelf-pick";
    input.dataset_digest = std::string(64, 'b');
    input.method = "held-out-reliability-bins";
    input.method_version = "1";
    input.outcome_definition = "shield accepted or repaired proposal";
    input.state_uncertainty_unit = "normalized-joint-range-rms";
    input.action_uncertainty_unit = "normalized-joint-range-rms";
    input.bins = {{0.0, 0.5, 0.25, 500, 100}, {0.5, 1.0, 0.85, 500, 400}};
    return input;
}

} // namespace

int main(int argc, char** argv) {
    using namespace rbfsafe;
    if (argc != 3) {
        std::cerr << "usage: rbfsafe_calibration_lifecycle_quickstart "
                     "<new-profile-json> <new-lifecycle-json>\n";
        return 2;
    }
    auto profile = PolicyCalibrationProfile::create(profile_input());
    if (!profile)
        return 1;
    auto lifecycle = PolicyCalibrationLifecycle::create(profile.value());
    if (!lifecycle)
        return 1;
    PolicyCalibrationWindowInput window;
    window.window_id = "production-window-0";
    window.source_digest = std::string(64, 'c');
    window.bins = {{500, 100}, {500, 400}};
    auto report =
        lifecycle.value().assess(profile.value(), std::move(window), lifecycle.value().current_event_id());
    if (!report)
        return 1;
    auto activated =
        lifecycle.value().transition(profile.value(), lifecycle.value().current_event_id(),
                                     PolicyCalibrationLifecycleState::Active, "independent review approved");
    if (!activated || !profile.value().save(std::filesystem::path(argv[1])) ||
        !lifecycle.value().save(std::filesystem::path(argv[2]), profile.value())) {
        return 1;
    }
    std::cout << "profile=" << profile.value().id() << '\n'
              << "drift_status=" << policy_calibration_drift_status_name(report.value().status) << '\n'
              << "lifecycle_state=" << policy_calibration_lifecycle_state_name(lifecycle.value().state())
              << '\n'
              << "lifecycle_head=" << lifecycle.value().current_event_id() << '\n'
              << "deployment_ready=" << (lifecycle.value().deployment_ready() ? "true" : "false") << '\n'
              << "runtime_executable=false\n";
    return 0;
}

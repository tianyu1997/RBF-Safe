"""Create, assess, approve, and persist a calibration lifecycle."""

from __future__ import annotations

import argparse
from pathlib import Path

import rbfsafe

parser = argparse.ArgumentParser()
parser.add_argument("profile", type=Path, help="new calibration profile JSON")
parser.add_argument("lifecycle", type=Path, help="new calibration lifecycle JSON")
args = parser.parse_args()

profile_input = rbfsafe.PolicyCalibrationProfileInput()
profile_input.policy_id = "vla-policy-a"
profile_input.policy_model_digest = "a" * 64
profile_input.scope_id = "factory-cell-a"
profile_input.task_id = "shelf-pick"
profile_input.dataset_digest = "b" * 64
profile_input.method = "held-out-reliability-bins"
profile_input.method_version = "1"
profile_input.outcome_definition = "shield accepted or repaired proposal"
profile_input.state_uncertainty_unit = "normalized-joint-range-rms"
profile_input.action_uncertainty_unit = "normalized-joint-range-rms"
profile_input.bins = [
    rbfsafe.PolicyCalibrationBinInput(0.0, 0.5, 0.25, 500, 100),
    rbfsafe.PolicyCalibrationBinInput(0.5, 1.0, 0.85, 500, 400),
]
profile = rbfsafe.PolicyCalibrationProfile.create(profile_input)

window = rbfsafe.PolicyCalibrationWindowInput()
window.window_id = "production-window-0"
window.source_digest = "c" * 64
window.bins = [
    rbfsafe.PolicyCalibrationWindowBinInput(500, 100),
    rbfsafe.PolicyCalibrationWindowBinInput(500, 400),
]
lifecycle = rbfsafe.PolicyCalibrationLifecycle.create(profile)
report = lifecycle.assess(profile, window, lifecycle.current_event_id)
lifecycle.transition(
    profile,
    lifecycle.current_event_id,
    rbfsafe.PolicyCalibrationLifecycleState.ACTIVE,
    "independent review approved",
)
profile.save(args.profile)
lifecycle.save(args.lifecycle, profile)

print(f"profile={profile.id}")
print(f"drift_status={rbfsafe.policy_calibration_drift_status_name(report.status)}")
print(f"lifecycle_state={rbfsafe.policy_calibration_lifecycle_state_name(lifecycle.state)}")
print(f"lifecycle_head={lifecycle.current_event_id}")
print(f"deployment_ready={str(lifecycle.deployment_ready).lower()}")
print("runtime_executable=false")

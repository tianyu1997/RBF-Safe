"""Create a calibration profile and gate one policy proposal."""

from __future__ import annotations

import argparse
from pathlib import Path

import rbfsafe


parser = argparse.ArgumentParser()
parser.add_argument("profile", type=Path, help="new calibration profile JSON")
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
profile.save(args.profile)

robot = rbfsafe.SerialRobotModel(
    "calibration-2r",
    [
        rbfsafe.DhJoint(0.0, 1.0, 0.0, 0.0, rbfsafe.JointType.REVOLUTE),
        rbfsafe.DhJoint(0.0, 1.0, 0.0, 0.0, rbfsafe.JointType.REVOLUTE),
    ],
    [rbfsafe.Interval(-1.0, 1.0), rbfsafe.Interval(-1.0, 1.0)],
    [0.02, 0.02],
)
scene = rbfsafe.SceneSnapshot([], "calibration-empty-v1")
atlas = rbfsafe.AtlasBuilder().build(robot, scene, [[0.0, 0.0]]).atlas
metadata = rbfsafe.PolicyProposalMetadata()
metadata.policy_id = "vla-policy-a"
metadata.task_id = "shelf-pick"
metadata.episode_id = "quickstart"
metadata.sequence = 1
metadata.confidence = 0.9
metadata.state_uncertainty = 0.05
metadata.action_uncertainty = 0.05
proposal = rbfsafe.PolicyProposal(rbfsafe.JointDeltaAction([0.1, 0.0]), metadata)
options = rbfsafe.CalibratedPolicyGateOptions()
options.policy.minimum_confidence = 0.7
report = rbfsafe.CalibratedPolicySafetyGate().check_proposals(
    profile,
    "factory-cell-a",
    "a" * 64,
    robot,
    scene,
    atlas,
    [0.0, 0.0],
    [proposal],
    options,
)
application = report.applications[0]
print(f"profile={profile.id}")
print(f"samples={profile.sample_count}")
print(f"ece={profile.expected_calibration_error}")
print(f"raw_confidence={application.raw_metadata.confidence}")
print(f"conservative_confidence={application.conservative_confidence}")
print(f"selected={report.policy_report.selected_index == 0}")
print("runtime_executable=false")

"""Gate VLA-style policy proposals and persist training feedback."""

from __future__ import annotations

import sys
from pathlib import Path

import rbfsafe


def metadata(policy_id: str, confidence: float) -> rbfsafe.PolicyProposalMetadata:
    value = rbfsafe.PolicyProposalMetadata()
    value.policy_id = policy_id
    value.task_id = "demo-pick"
    value.episode_id = "episode-1"
    value.sequence = 1
    value.confidence = confidence
    value.state_uncertainty = 0.04
    value.action_uncertainty = 0.05
    value.observation_age_seconds = 0.01
    value.inference_latency_seconds = 0.02
    return value


def main() -> int:
    robot = rbfsafe.SerialRobotModel(
        "python-policy-demo",
        [rbfsafe.DhJoint(0.0, 1.0, 0.0, 0.0, rbfsafe.JointType.REVOLUTE)],
        [rbfsafe.Interval(-1.0, 1.0)],
        [0.02],
    )
    scene = rbfsafe.SceneSnapshot([], "python-policy-demo-v1")
    atlas = rbfsafe.AtlasBuilder().build(robot, scene, [[-0.5], [0.5]]).atlas
    proposals = [
        rbfsafe.PolicyProposal(rbfsafe.JointDeltaAction([0.2]), metadata("vla-primary", 0.92)),
        rbfsafe.PolicyProposal(rbfsafe.JointDeltaAction([0.1]), metadata("vla-fallback", 0.75)),
    ]
    options = rbfsafe.PolicyGateOptions()
    options.minimum_confidence = 0.7
    options.maximum_state_uncertainty = 0.2
    options.maximum_action_uncertainty = 0.2
    options.maximum_observation_age_seconds = 0.1
    options.maximum_inference_latency_seconds = 0.1
    options.selection_mode = rbfsafe.PolicySelectionMode.HIGHEST_CONFIDENCE
    report = rbfsafe.LearningPolicySafetyGate().check_proposals(
        robot, scene, atlas, [-0.5], proposals, options
    )
    if report.selected_index is None:
        print("no proposal passed both policy and geometric safety gates", file=sys.stderr)
        return 1
    selected = report.decisions[report.selected_index]
    print(
        f"selected_policy={selected.metadata.policy_id} "
        f"reason={rbfsafe.policy_gate_reason_name(selected.reason)}"
    )
    if len(sys.argv) == 2:
        rbfsafe.PolicyFeedbackDatabase.create(report.feedback).save(Path(sys.argv[1]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

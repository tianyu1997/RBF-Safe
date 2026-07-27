"""Create and assess a signed reviewed deployment profile."""

from __future__ import annotations

import argparse
from pathlib import Path

import rbfsafe


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("trust_history", type=Path)
    parser.add_argument("checkpoint", type=Path)
    parser.add_argument("reviewed_profile", type=Path)
    args = parser.parse_args()

    pair_a = rbfsafe.ed25519_key_pair_from_seed(bytes(range(1, 33)))
    pair_b = rbfsafe.ed25519_key_pair_from_seed(bytes(range(33, 65)))
    governance_pair = rbfsafe.ed25519_key_pair_from_seed(bytes(range(65, 97)))
    reviewer_a = rbfsafe.make_service_public_key(
        "review-safety",
        pair_a.public_key,
        1,
        0,
        rbfsafe.ServiceKeyState.ACTIVE,
        False,
        True,
        False,
    )
    reviewer_b = rbfsafe.make_service_public_key(
        "review-controls",
        pair_b.public_key,
        1,
        0,
        rbfsafe.ServiceKeyState.ACTIVE,
        False,
        True,
        False,
    )
    governance = rbfsafe.make_service_public_key(
        "trust-governance",
        governance_pair.public_key,
        1,
        0,
        rbfsafe.ServiceKeyState.ACTIVE,
        False,
        False,
        True,
    )
    rotation_policy = rbfsafe.ServiceTrustRotationPolicy()
    rotation_policy.minimum_signatures = 1
    bundle = rbfsafe.ServiceTrustBundle.create_with_rotation_policy(
        1, "", [reviewer_b, governance, reviewer_a], rotation_policy
    )
    history = rbfsafe.ServiceTrustHistory.create(
        args.trust_history, bundle, bundle.id
    )
    checkpoint_signature = rbfsafe.sign_service_trust_checkpoint(
        history,
        governance.service_id,
        governance.id,
        governance_pair.secret_key,
    )
    checkpoint = rbfsafe.assemble_service_trust_checkpoint(
        history, [checkpoint_signature]
    )
    checkpoint.save(args.checkpoint)

    constraints = rbfsafe.DeploymentRuntimeConstraints()
    constraints.maximum_observation_age_ns = 50_000
    constraints.maximum_command_latency_ns = 50_000
    constraints.maximum_control_period_ns = 2_000_000
    constraints.maximum_consecutive_missed_cycles = 1
    policy = rbfsafe.DeploymentReviewPolicy()
    policy.minimum_approvals = 2
    policy.require_distinct_services = True
    policy.required_roles = [
        rbfsafe.DeploymentReviewRole.CONTROLS,
        rbfsafe.DeploymentReviewRole.SAFETY,
    ]
    profile_input = rbfsafe.DeploymentProfileInput()
    profile_input.deployment_id = "cell-a"
    profile_input.robot_digest = "a" * 64
    profile_input.controller_digest = "b" * 64
    profile_input.platform_digest = "c" * 64
    profile_input.runtime_digest = "d" * 64
    profile_input.trust_root_bundle_id = bundle.id
    profile_input.trust_checkpoint_id = checkpoint.id
    profile_input.trust_bundle_id = bundle.id
    profile_input.trust_bundle_sequence = bundle.sequence
    profile_input.runtime_constraints = constraints
    profile_input.review_policy = policy
    profile = rbfsafe.DeploymentProfile.create(profile_input)
    approval_a = rbfsafe.sign_deployment_profile_approval(
        profile,
        reviewer_a.service_id,
        reviewer_a.id,
        rbfsafe.DeploymentReviewRole.SAFETY,
        pair_a.secret_key,
    )
    approval_b = rbfsafe.sign_deployment_profile_approval(
        profile,
        reviewer_b.service_id,
        reviewer_b.id,
        rbfsafe.DeploymentReviewRole.CONTROLS,
        pair_b.secret_key,
    )
    approval_set = rbfsafe.assemble_deployment_profile_approvals(
        profile, [approval_b, approval_a]
    )
    reviewed = rbfsafe.ReviewedDeploymentProfile.create(
        profile, approval_set, history, checkpoint, checkpoint.id
    )
    reviewed.save(args.reviewed_profile)

    snapshot = rbfsafe.DeploymentRuntimeSnapshot()
    snapshot.deployment_id = "cell-a"
    snapshot.robot_digest = "a" * 64
    snapshot.controller_digest = "b" * 64
    snapshot.platform_digest = "c" * 64
    snapshot.runtime_digest = "d" * 64
    snapshot.observation_age_ns = 10_000
    snapshot.command_latency_ns = 20_000
    snapshot.control_period_ns = 1_000_000
    snapshot.runtime_monitor_active = True
    snapshot.fail_closed_transport_active = True
    snapshot.authenticated_artifacts = True
    assessment = reviewed.assess(snapshot)

    print(f"trust_root={bundle.id}")
    print(f"checkpoint={checkpoint.id}")
    print(f"profile={profile.id}")
    print(f"approval_set={approval_set.id}")
    print(f"assessment={assessment.id}")
    print(
        "status="
        f"{rbfsafe.deployment_profile_assessment_status_name(assessment.status)}"
    )
    print(f"approvals={len(approval_set.approvals)}")
    print("runtime_executable=false")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

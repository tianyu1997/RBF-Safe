# Learning-policy safety gate

RBF-Safe 2.0 adds a deterministic policy gate between learned or VLA policy
proposals and the existing runtime shield. It filters stale or uncertain
proposals, asks `RuntimeShield` to certify or repair every eligible action,
selects one usable result, and emits aligned feedback records for offline
training and audit. It does not execute commands and never emits
`RuntimeExecutable` evidence.

Include `<rbfsafe/policy.h>` and link `RBFSafe::policy`. The aggregate
`RBFSafe::rbfsafe` target also includes this module. Python mirrors the stable
high-level API under `rbfsafe`.

## Proposal contract

Each `PolicyProposal` contains one `ShieldAction` and
`PolicyProposalMetadata`:

- `policy_id` and `task_id` are required, bounded identifiers;
- `episode_id` is optional and `sequence` is an application-assigned ordinal;
- `confidence` is finite and lies in `[0, 1]`;
- state/action uncertainty, observation age, and inference latency are finite
  non-negative values.

RBF-Safe deliberately does not assign units to uncertainty. A deployment must
use one documented calibration convention for every proposal in a batch and
configure matching thresholds. Age and latency are seconds. Metadata is
identity-bearing: changing it changes the proposal, decision, and feedback
SHA-256 IDs.

`PolicyGateOptions` supplies minimum confidence, maximum state/action
uncertainty, maximum observation age, maximum inference latency, a proposal
budget, a selection mode, and the complete nested `ShieldOptions`. Invalid,
duplicate, empty, over-budget, identity-mismatched, or cancelled batches fail
through `Result<T>` rather than producing partial feedback.

## Gate and selection order

`LearningPolicySafetyGate::check_proposals` performs these deterministic
steps:

1. validate options, robot/scene/Atlas identity, current state, metadata, and
   all actions;
2. reject proposals against confidence, state uncertainty, action uncertainty,
   observation age, then inference latency, in that fixed order;
3. pass every remaining proposal through `RuntimeShield`;
4. exclude shield-rejected actions from selection;
5. prefer `Accept` over `Repair`, then apply the configured selection mode;
6. mark at most one selected decision and emit one aligned feedback record per
   input proposal.

Selection modes are `InputOrder`, `HighestConfidence`, and
`LowestUncertainty`. Confidence/uncertainty ties are broken by the complementary
metric and then by input index. Input order is therefore always the final
tie-breaker. A report with no `selected_index` is a normal fail-closed result:
no proposal passed both gates.

The gate's synchronized telemetry counts batches, proposals, policy
rejections, shield outcomes, and selected accept/repair outcomes. Telemetry is
operational data only and is not certificate evidence.

## Feedback semantics

Every `PolicyFeedbackRecord` binds the robot and scene digests, proposal and
decision IDs, policy metadata, action type, requested/output target, repair
distance, reason, label, and evidence. Labels are:

| Label | Meaning |
|---|---|
| `SelectedAccepted` | Selected action was accepted unchanged by the shield |
| `SelectedRepaired` | Selected action is the shield's certified repair |
| `EligibleNotSelected` | Shield-usable action lost deterministic selection |
| `PolicyRejected` | Policy metadata failed a configured threshold |
| `ShieldRejected` | Metadata passed, but the geometric shield rejected it |

These are supervised feedback labels, not reward definitions. In particular,
`PolicyRejected` and `ShieldRejected` do not prove collision, and a repaired
target must not be presented to a learner as the originally requested action.
Consumers should keep requested and output targets distinct and retain the
identity fields when deriving datasets.

`PolicyFeedbackDatabase` validates uniqueness and record consistency, appends
under an explicit record budget, filters by policy/task/episode/label, reports
label counts, and saves or loads the independent checksummed schema described
in [the feedback format](policy-feedback-format.md).

## C++ example

```cpp
PolicyProposalMetadata metadata;
metadata.policy_id = "vla-primary";
metadata.task_id = "shelf-pick";
metadata.episode_id = "episode-1";
metadata.sequence = 42;
metadata.confidence = 0.92;
metadata.state_uncertainty = 0.04;
metadata.action_uncertainty = 0.05;
metadata.observation_age_seconds = 0.01;
metadata.inference_latency_seconds = 0.02;

std::vector<PolicyProposal> proposals{
    {JointDeltaAction{{0.1, -0.05}}, metadata},
};
PolicyGateOptions options;
options.minimum_confidence = 0.7;
options.maximum_state_uncertainty = 0.2;
options.maximum_action_uncertainty = 0.2;
options.selection_mode = PolicySelectionMode::HighestConfidence;

LearningPolicySafetyGate gate;
auto report = gate.check_proposals(robot, scene, atlas, current, proposals, options);
if (!report || !report.value().selected_index) return 1;
auto feedback = PolicyFeedbackDatabase::create(report.value().feedback);
if (!feedback || !feedback.value().save("policy-feedback")) return 1;
```

Complete examples are in `examples/policy_safety_quickstart.cpp` and
`examples/policy_safety_quickstart.py`.

## Deployment boundary

The gate assumes that proposal metadata is supplied honestly and calibrated
by the caller. It does not authenticate a policy, sensor timestamp, task ID,
or uncertainty estimator. It does not model controller dynamics, tracking
error, moving obstacles, sensor faults, deadlines, or hardware interlocks.
The existing runtime monitor can observe Atlas membership and deviation after
a shield decision, but it also remains below execution evidence.

A deployment must independently validate timing, state estimation, actuation,
emergency stopping, and the mapping from the shield's output trajectory to
controller commands. Persistent cross-task memory, online policy updates, and
deployment-profile `RuntimeExecutable` evidence remain future work.

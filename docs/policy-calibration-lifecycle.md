# Policy calibration drift and lifecycle

RBF-Safe 3.5 adds a deterministic post-deployment monitoring record around a
3.4 `PolicyCalibrationProfile`. It compares an operational outcome window with
the profile baseline, records the result in an append-only lifecycle, and lets
the calibrated policy gate fail closed unless the exact reviewed lifecycle
head is active.

This implements a software mechanism, not a universal drift test. The
[NIST AI RMF Playbook Measure function](https://airc.nist.gov/airmf-resources/playbook/measure/)
recommends monitoring how production metrics differ from pre-deployment
metrics and responding when operating assumptions no longer hold. NIST also
notes that deployed-AI monitoring methods and terminology remain immature in
its 2026 report on
[challenges to monitoring deployed AI systems](https://www.nist.gov/publications/challenges-monitoring-deployed-ai-systems-center-ai-standards-and-innovation).
RBF-Safe therefore exposes every threshold as deployment policy and does not
claim that its defaults are standardized or sufficient for a particular
robot.

## Observation windows

`PolicyCalibrationWindowInput` contains:

- a caller-defined stable `window_id`;
- a monotonically increasing sequence used by a lifecycle;
- a SHA-256 digest of the exact source observation set; and
- one `{samples, successes}` aggregate for every profile confidence bin.

The success label must use the profile's declared `outcome_definition`.
Counts are aggregated so raw production records need not be placed in the
lifecycle file. The source digest binds the aggregate to records retained by
the deployment system; RBF-Safe neither retrieves nor authenticates those
records.

`assess_policy_calibration_drift` derives:

- total variation distance between baseline and operational confidence-bin
  distributions;
- operational expected and maximum calibration error;
- baseline and operational overall success rates;
- non-negative overall success-rate drop; and
- the maximum non-negative per-bin success-rate drop.

For baseline bin fractions \(p_i\) and operational fractions \(q_i\), total
variation distance is:

```text
TV = 0.5 * sum_i |p_i - q_i|
```

It lies in `[0, 1]` when the operational window is non-empty. An empty window
is assigned `TV=1` but remains `InsufficientData`; this sentinel must not be
interpreted as a measured distribution.

The result status is:

- `InsufficientData` when the total sample floor is not met or a populated bin
  is below the configured per-bin floor;
- `DriftDetected` when a sufficiently sampled window exceeds any configured
  distribution, calibration-error, overall-drop, or per-bin-drop threshold;
- `Stable` otherwise.

Threshold comparisons are strict `>` checks. Empty operational bins
contribute to distribution distance but do not invent an outcome rate.
All counts are bounded to one trillion and profile bin limits still apply.

```cpp
PolicyCalibrationWindowInput window;
window.window_id = "production-window-42";
window.sequence = 42;
window.source_digest = source_records_sha256;
window.bins = {{500, 100}, {500, 400}};

PolicyCalibrationDriftOptions options;
options.minimum_total_samples = 1000;
options.minimum_bin_samples = 30;
options.maximum_total_variation_distance = 0.1;
options.maximum_expected_calibration_error = 0.1;
options.maximum_overall_success_rate_drop = 0.1;
options.maximum_bin_success_rate_drop = 0.2;

auto report = assess_policy_calibration_drift(profile, window, options);
```

## Lifecycle state machine

A `PolicyCalibrationLifecycle` is bound to one exact profile ID and starts in
`PendingReview`. Its states are:

```text
PendingReview -> Active
PendingReview -> Quarantined | Retired
Active        -> PendingReview | Quarantined | Retired
Quarantined   -> PendingReview | Retired
Retired       -> terminal
```

Activation requires a latest `Stable` assessment. A new `DriftDetected`
assessment automatically moves any non-retired lifecycle to `Quarantined`.
An `InsufficientData` assessment moves `Active` to `PendingReview`.
Neither a stable assessment nor software alone reactivates a pending or
quarantined profile: an explicit transition with review detail is required.
A quarantined lifecycle cannot transition directly to active.

Every assessment and manual transition:

- requires the caller's expected current event ID;
- receives the next deterministic sequence;
- binds its parent event ID;
- receives a deterministic SHA-256 event ID; and
- becomes the new optimistic-concurrency head.

Assessment windows must have strictly increasing caller sequences. The event
parent chain prevents different histories that reach the same state from
sharing a head identity.

```cpp
auto lifecycle = PolicyCalibrationLifecycle::create(profile).value();
auto assessed = lifecycle.assess(
    profile, window, lifecycle.current_event_id(), options);
auto activated = lifecycle.transition(
    profile,
    lifecycle.current_event_id(),
    PolicyCalibrationLifecycleState::Active,
    "independent deployment review approved");
```

## Fail-closed policy gating

`CalibratedPolicySafetyGate::check_proposals_guarded` requires:

1. a lifecycle that replays correctly against the exact profile;
2. `Active` state;
3. a latest `Stable` report; and
4. an expected lifecycle event ID equal to the current head.

It then performs all 3.4 profile quality checks and the ordinary geometric
shield checks. The returned batch report records the lifecycle head used.
The lifecycle does not elevate evidence and never creates
`RuntimeExecutable`.

The older `check_proposals` entry point remains available for offline and
stateless compatibility. Deployments that claim lifecycle enforcement must
use the guarded entry point and protect their trusted expected-head
configuration.

## Persistence and inspection

Schema 1 is a bounded JSON file with format
`rbfsafe-policy-calibration-lifecycle`. It stores the profile ID, state, head,
all drift reports, and the complete parent-linked event history. Integer
counts and sequences are decimal strings. Loading requires the corresponding
profile, recomputes every report from its source counts and thresholds,
replays every transition, and verifies every ID and parent link.

The default reader limits are 100,000 reports, 1,000,000 events, 1,000,000
total report bins, and 256 MiB. Saving uses a sibling temporary file and
refuses overwrite by default.

```bash
rbfsafe-inspect lifecycle.json \
  --calibration-profile policy-calibration.json

# Native tool:
rbfsafe-inspect lifecycle.json policy-calibration.json
```

Runnable examples are
[`examples/calibration_lifecycle_quickstart.cpp`](../examples/calibration_lifecycle_quickstart.cpp)
and
[`examples/calibration_lifecycle_quickstart.py`](../examples/calibration_lifecycle_quickstart.py).
The fixed files under `data/policy_calibration_lifecycle_schema1` and
`data/policy_calibration_profile_schema1` are synthetic interoperability data.

## Safety boundary

This monitor only sees the confidence histogram and declared binary outcomes.
It cannot detect arbitrary input-feature, state, perception, causal, or
environment shift. Delayed, missing, selected, or incorrectly defined
outcomes can bias every metric. Aggregate stability does not prove that any
individual action is correct.

The lifecycle has caller sequences but no trusted wall clock, automatic
expiry, cadence scheduler, incident service, or human-approval identity.
Its JSON is integrity-checked by deterministic recomputation and the event
chain but is not authenticated. Key management, authorization, append-only
remote storage, alert routing, rollback policy, observation retention,
hardware interlocks, and emergency stopping remain deployment
responsibilities.

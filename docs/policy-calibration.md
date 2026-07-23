# Policy calibration profiles

RBF-Safe 3.4 adds deterministic, reviewable confidence-calibration profiles
to the existing learning-policy gate. A profile records the held-out evidence
behind a policy confidence value and maps a raw confidence to a conservative
effective confidence before geometric shield checks. It never replaces the
shield and never creates `RuntimeExecutable` evidence.

The interpretation follows the standard calibration objective that reported
confidence should correspond to empirical correctness likelihood, as
described by [Guo et al.](https://proceedings.mlr.press/v70/guo17a.html).
RBF-Safe also records scope, model, dataset, method, outcome, and measurement
units because evaluation results must remain connected to their conditions
and limitations; this is consistent with the measurement and documentation
guidance in the [NIST AI RMF Measure function](https://airc.nist.gov/airmf-resources/airmf/5-sec-core/).

## Profile contract

`PolicyCalibrationProfileInput` binds:

- the exact `policy_id` and SHA-256 digest of the policy model bytes;
- an application-defined deployment `scope_id` and exact task ID;
- the SHA-256 digest of the held-out calibration dataset;
- calibration method and method version;
- a precise plain-language outcome definition;
- state- and action-uncertainty unit names; and
- contiguous confidence bins covering all of `[0, 1]`.

Each bin stores its interval, mean reported confidence, sample count, and
successful-outcome count. The first bin includes zero. Other bins are
left-closed and right-open; the final bin also includes one. Counts are stored
as decimal strings on disk, avoiding binary64 integer truncation. A profile
contains at most 4,096 bins and one trillion total samples; deployment readers
can impose smaller limits.

`PolicyCalibrationProfile::create` derives, rather than trusts:

- observed success rate `successes / samples`;
- absolute bin calibration error;
- sample-weighted expected calibration error (ECE);
- maximum bin calibration error; and
- a 95% Wilson score lower confidence bound for each bin.

The deterministic profile ID hashes only canonical source fields and counts.
Derived values are saved for review and recomputed during load. A mismatch is
reported as corrupted data. ECE is a compact diagnostic, not a complete
description of calibration quality, and the Wilson bound relies on the
meaningfulness of the supplied observations. Neither statistic establishes
independence, representativeness, or freedom from distribution shift.

## Conservative application

`CalibratedPolicySafetyGate::check_proposals` requires the expected scope and
model digest from trusted caller configuration. It also requires exact
policy/task agreement for every proposal and applies explicit minimum total
and per-bin sample counts plus maximum ECE and maximum-bin-error thresholds.

For a raw confidence `p`, the gate records:

```text
calibrated confidence   = observed success rate of the containing bin
conservative confidence = min(p, bin 95% Wilson lower bound)
```

Taking the minimum prevents calibration from upgrading the policy's own
confidence. A `CalibratedPolicyApplication` retains both raw and effective
metadata, the selected bin, its sample count, both calibrated values, and a
deterministic application ID bound to the proposed action. The effective
proposal then passes through `LearningPolicySafetyGate`; its normal confidence,
uncertainty, freshness, latency, geometric shield, selection, feedback, and
telemetry rules still apply.

```cpp
auto profile = PolicyCalibrationProfile::load("policy-calibration.json");
CalibratedPolicyGateOptions options;
options.minimum_total_samples = 1000;
options.minimum_bin_samples = 30;
options.maximum_expected_calibration_error = 0.1;
options.maximum_bin_calibration_error = 0.2;
options.policy.minimum_confidence = 0.7;

CalibratedPolicySafetyGate gate;
auto report = gate.check_proposals(
    profile.value(), "factory-cell-a", trusted_model_digest,
    robot, scene, atlas, current, proposals, options);
```

## Persistence and inspection

Schema 1 is a bounded single JSON file with format
`rbfsafe-policy-calibration-profile`. Loading enforces byte/bin budgets,
canonical coverage, identities, counts, all derived statistics, and the
profile ID. Saving uses a sibling temporary file, refuses overwrite by
default, and publishes with rename. The file is integrity-checked by its
deterministic digest but is not authenticated; applications requiring source
authentication must add a separately reviewed trust mechanism.

Inspect a profile or query one raw confidence:

```bash
rbfsafe-inspect policy-calibration.json --policy-confidence 0.9
# Native tool: rbfsafe-inspect policy-calibration.json 0.9
```

Runnable examples are
[`examples/calibrated_policy_quickstart.cpp`](../examples/calibrated_policy_quickstart.cpp)
and
[`examples/calibrated_policy_quickstart.py`](../examples/calibrated_policy_quickstart.py).
`data/policy_calibration_profile_schema1` is synthetic interoperability data,
not a deployment calibration.

## Safety boundary

A passing profile says only that recorded held-out observations satisfy the
configured aggregate gates under the declared outcome and scope. It does not
prove that a future proposal is correct, that raw uncertainties use valid
units, that the dataset represents live operation, or that the environment
has not drifted. Profiles contain no timestamps or automatic expiry in 3.4;
deployment systems must version datasets, monitor drift, revoke outdated
profiles, and choose thresholds through an independent safety review.

The calibrated gate does not authenticate model inference, sensor data,
timestamps, profile authors, or files. It does not model controller dynamics,
hardware interlocks, emergency stopping, or task success. Its output remains
below `RuntimeExecutable`, even when the nested shield accepts an action.

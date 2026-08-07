# Bounded execution sessions

RBF-Safe 3.11 adds a transport-neutral, narrowly scoped bridge between
certified geometric motion and caller-managed controller execution. Include
`<rbfsafe/modules/assurance.h>` and link `RBFSafe::execution`.

This API does not operate hardware, read a clock, or create an open-ended
permit. A `BoundedExecutionSession` has `Unknown` evidence and
`authorizes_execution()` is always false. Only
`authorize_command(index, configuration, dispatch_monotonic_ns)` can return a
single `RuntimeExecutable` value, and only for the exact reviewed command and
stored time window.

## Verified chain

A session binds all of the following:

1. An exact `SafeAtlas` identity, robot digest, scene digest, re-audited
   piecewise-linear command sequence, region sequence, and Atlas connectivity
   certificate.
2. An exact `ReviewedDeploymentProfile`, approval-set ID, caller-pinned trust
   root, signed checkpoint, trust head, and head sequence.
3. Distinct controller and runtime-monitor endpoint IDs and Ed25519 public
   keys. Endpoint keys are explicit session inputs; ordinary service
   publication permission is not treated as controller authority.
4. A fresh session nonce, command-count/duration/start-delay limits, and a
   strictly increasing command schedule beginning at offset zero.
5. A new execution-session approval quorum signed by the same exact
   service/key/role identities that approved the deployment profile.
6. An Ed25519 controller acknowledgement of the exact request, command
   sequence, endpoint identity, and accepted command count.
7. An independently signed runtime-monitor observation containing the exact
   deployment runtime snapshot, caller-supplied monotonic observation time,
   observation sequence, and `ArmedCertifiedSequence` state.

Construction repeats trajectory and route certification, trust and approval
verification, controller/monitor signature verification, deployment
conformance assessment, and observation-freshness checks. A sampled
trajectory, conformant profile, or signature alone cannot create a session.

## Exact command windows

Let `t0` be the monitor-signed observation time, `d_start` the reviewed
maximum start delay, `d_session` the bounded session duration, `o[i]` a
command's scheduled offset, and `d_latency` the conformant observed command
latency:

```text
session start deadline = t0 + d_start
session end            = t0 + d_session
command i start        = t0 + o[i]
command i end          = min(command i start + d_latency, session end)
command 0 end          = min(command 0 end, session start deadline)
```

All additions are overflow checked. The configuration comparison is exact
element-for-element equality with the signed command sequence. A wrong index,
wrong dimension, changed floating-point value, early time, late time, or
expired session returns no authorization. A successful
`ExecutionCommandAuthorization` binds the session, sequence, command digest,
index, supplied dispatch time, closed validity interval, and
`RuntimeExecutable` evidence. `open_ended()` is always false.

Authorization evaluation on the session remains deliberately stateless. It
does not record command progress, require earlier commands to have been
dispatched, or prevent the same exact command from being evaluated more than
once inside its window. RBF-Safe 3.12 adds `ExecutionLedger` for callers that
need persistent single-dispatch ordering, current-checkpoint revalidation,
signed controller completion, cancellation, expiration, and revocation
history. See [Revocation-aware execution ledger](execution-ledger-format.md).

The monotonic timestamp domain is supplied by the caller. The observation and
dispatch values must come from the same trustworthy monotonic clock domain.
RBF-Safe does not read or synchronize clocks.

## Persistence

`BoundedExecutionSession::save` writes one deterministic JSON document:

```text
format: rbfsafe-bounded-execution-session
schema: 1
id and monotonic bounds
request
command_sequence
approval_set
controller_acknowledgement
monitor_acknowledgement
```

Unsigned derived IDs and time bounds are recomputed during loading. Loading
also reopens the caller-supplied trust history against the caller-retained
checkpoint ID, recreates the reviewed profile, retrieves the exact historical
trust bundle, replays the command sequence against the supplied Atlas, and
recreates the complete session. It rejects symlinks and non-regular files,
unknown schemas, malformed or truncated JSON, signature/identity changes,
wrong Atlas/profile/trust anchors, and configured byte/count/dimension limits.

Saving uses a same-directory temporary file and atomic rename. Existing
destinations are rejected by default. Explicit overwrite stages the old file
and restores it if publication fails.

The fixed `data/bounded_execution_session_schema1` fixture contains only
synthetic public keys, signatures, Atlas data, and governance/session records.
It contains no private key and is not production deployment authority.

## Inspection

The Python inspection entry point verifies the whole chain:

```bash
rbfsafe-inspect session.json \
  --reviewed-profile profile.json \
  --execution-atlas atlas \
  --trust-history trust-history \
  --trust-checkpoint checkpoint.json \
  --expected-trust-root <root-id> \
  --expected-trust-checkpoint <checkpoint-id>
```

Add `--execution-command-index`, `--execution-configuration`, and
`--dispatch-monotonic-ns` together to evaluate one exact command. Omitting
them reports metadata only and creates no command authorization.

## Exclusions

RBF-Safe does not send commands, manage actuators, authenticate physical
devices, attest sensor provenance, measure tracking error, model dynamics,
guarantee real-time scheduling, synchronize clocks, automatically discover
post-creation key revocation, or monitor scene changes. Direct session
authorization also does not track progress or prevent replay; use the 3.12
ledger when those properties are required. Applications must fail closed on any
changed Atlas, profile, trust anchor, endpoint key, command byte, runtime
state, clock domain, or expired window and must independently enforce
controller, monitor, transport, hardware, and emergency-stop policy.

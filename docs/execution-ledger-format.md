# Revocation-aware execution ledger

RBF-Safe 3.12 adds a transport-neutral, append-only progress and authorization
ledger for a previously verified `BoundedExecutionSession`. Include
`<rbfsafe/execution_ledger.h>` and link `RBFSafe::execution`.

An `ExecutionLedger`, its summary, and its offline audit report all carry
`Unknown` evidence and never authorize execution. Only the optional
`ExecutionCommandAuthorization` inside a successful
`ExecutionLedgerCommandDecision` carries `RuntimeExecutable`, for one exact
command and its existing closed monotonic window.

## State machine

Every ledger starts with one `SessionOpened` record. A nonterminal ledger is
either `Open` or `AwaitingCompletion`.

```text
Open
  -> CommandAuthorized -> AwaitingCompletion
  -> SessionCancelled  -> Cancelled
  -> SessionExpired    -> Expired
  -> DependencyRevoked -> Revoked

AwaitingCompletion
  -> ControllerCompletion(Completed) -> Open or Completed
  -> ControllerCompletion(Failed/Rejected) -> Failed
  -> SessionCancelled / SessionExpired / DependencyRevoked -> terminal state
```

Commands are authorized only in signed sequence order. A command cannot be
authorized twice, skipped, or authorized while another command awaits
completion. The next command is unavailable until the exact outstanding
authorization receives an independently signed controller-completion
observation. Completion after the session end derives `Expired`, even when the
controller reports success.

`authorize_command` reopens and audits the ledger under a cross-process writer
lock, compares the caller's expected record head, verifies the caller-pinned
current signed trust checkpoint and history, rejects checkpoint rollback or
same-sequence forks, and checks every original reviewer key in the current
bundle. A missing, retired, revoked, sequence-invalid, or non-publication key
causes a terminal checkpoint-backed `ReviewerKey` revocation record and no
authorization.

Explicit dependency revocations can name only an exact session dependency:
reviewed profile, Atlas, scene, controller key, runtime-monitor key, reviewer
key, or a session/ledger checkpoint. Cancellation, expiration, and revocation
are terminal. RBF-Safe does not discover these events; the caller must supply
current authenticated state and fail closed when it cannot do so.

## Controller completion

`sign_execution_controller_completion` binds:

- the exact session, command sequence, authorization, command index, and
  command digest;
- controller service and endpoint-key identities;
- `Completed`, `Failed`, or `Rejected`;
- a caller-supplied monotonic completion time; and
- a caller-defined SHA-256 result digest.

The signature must match the exact controller endpoint embedded in the
session. A completion is an authenticated controller statement, not proof of
physical motion, tracking, hardware identity, or result semantics.

## Expected-head concurrency

Every mutation accepts `expected_current_record_id`. The implementation takes
an exclusive `.writer-lock`, reloads and audits the complete immutable chain,
then compares the on-disk head. A stale writer receives `IdentityMismatch` and
does not append. Record files are created exclusively and are never
overwritten. This supplies optimistic concurrency for cooperating local
processes without claiming a distributed consensus or transparency service.

## Schema 1 directory

```text
ledger/
  manifest.json
  records/
    00000000000000000000-<record-id>.json
    00000000000000000001-<record-id>.json
    ...
```

The manifest binds the exact session ID, command count, closed session bounds,
root record, ledger ID, and a canonical SHA-256 identity. Each record binds its
sequence, parent, ledger/session identities, type, caller-supplied monotonic
time, and exactly the applicable authorization, completion, checkpoint,
revocation, or cancellation detail. The filename includes the zero-padded
sequence and deterministic record ID.

Creation stages a complete root directory in a same-parent temporary path and
publishes it atomically. Appends stage one same-directory temporary record,
flush it, and rename it exclusively. Loaders tolerate abandoned `.tmp-`
entries but reject all other unexpected entries, symlinks, gaps, duplicate
sequences, parent changes, identity/checksum changes, unknown schemas,
truncation, configured byte/count/signature limits, cancellation, and any
record after a terminal event.

Offline audit recreates the bounded session using the exact historical trust
bundle and Atlas, replays each command authorization, verifies every stored
historical checkpoint and current reviewer state, verifies every controller
completion signature, and recomputes the state and all identities. The
application must retain the trust history and its newest checkpoint anchor;
an internally consistent older history cannot prove freshness by itself.

## Inspection

```bash
rbfsafe-inspect ledger \
  --execution-session session.json \
  --reviewed-profile profile.json \
  --execution-atlas atlas \
  --trust-history trust-history \
  --trust-checkpoint checkpoint.json \
  --expected-trust-root <root-id> \
  --expected-trust-checkpoint <checkpoint-id>
```

The fixed `data/execution_ledger_schema1` fixture is synthetic interoperability
data. It contains signed authorizations and completions but no private keys,
hardware evidence, clock evidence, or deployment authority.

## Explicit exclusions

The ledger does not send commands, read or synchronize clocks, discover key or
scene changes, authenticate physical devices, attest sensors, prove
transmission, measure tracking, validate dynamics, guarantee scheduling,
operate actuators, or implement an emergency stop. It is a deterministic
authorization-history and audit primitive for caller-managed systems.

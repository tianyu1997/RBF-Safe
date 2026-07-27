# Transparency log schema 1

RBF-Safe 3.13 adds a deterministic, locally persisted transparency log for
reviewed deployment anchors and independently signed runtime observations. The
log, every leaf, every checkpoint, every proof, and every audit report carries
`Unknown` evidence and never authorizes execution. It is an audit and
anti-rollback mechanism, not a physical-execution oracle.

## Caller-pinned log identity

`TransparencyLogIdentity` contains:

- schema 1 and a deterministic identity;
- a non-empty log namespace;
- the signer service and key IDs; and
- the exact 32-byte Ed25519 public key.

`TransparencyLog::open` requires the complete identity and the expected current
checkpoint ID from the caller. The manifest cannot select or replace these
pins. An empty expected checkpoint is valid only for an empty log.

## Deployment anchors

`DeploymentTransparencyAnchor::create` first verifies the caller-pinned signed
service-trust checkpoint against the supplied trust history. It then binds:

- reviewed profile and approval-set IDs;
- deployment, robot, controller, platform, and runtime identities;
- trust root, checkpoint, head bundle, and head record identities; and
- the exact trust-bundle sequence.

The anchor does not repeat deployment review or claim that the described
software is currently installed. Its inclusion proves only that the exact
canonical anchor was committed under a signed transparency checkpoint.

## Independent runtime observations

An `IndependentRuntimeObservation` binds one outstanding
`ExecutionCommandAuthorization` to:

- the exact session, ledger, authorization record, command sequence, command
  index, and command digest;
- a caller-supplied SHA-256 configuration digest;
- a structurally valid deployment runtime snapshot;
- a positive source observation sequence and caller-supplied monotonic value;
  and
- an armed, disarmed, or fault monitor state.

Creation is allowed only while that authorization is the current outstanding
ledger record and the observation lies inside its closed authorization window.

Each `RuntimeObservationAttestation` is an Ed25519 signature by an active,
publication-capable key in the supplied caller-pinned service trust bundle.
`RuntimeObservationPolicy` sets a minimum count, optional service
distinctness, and controller-service exclusion. The default excludes the
controller service so a controller completion cannot also satisfy the
independent-observer role. Canonical sets sort by `(service_id, key_id)` and
reject duplicate keys.

These checks authenticate source claims. They do not prove that a sensor was
calibrated, that an observed configuration was physically reached, that a
monotonic value came from trusted hardware, or that independent services did
not share a compromised upstream measurement.

## Merkle construction and checkpoints

Leaves are ordered by zero-based append sequence. Domain-separated SHA-256 is
used throughout:

```text
leaf_hash = SHA256("rbfsafe-transparency-merkle-leaf-v1\n" || leaf_id)
node_hash = SHA256("rbfsafe-transparency-merkle-node-v1\n" || left || right)
```

Adjacent hashes are paired from left to right. An unpaired final hash is
promoted unchanged to the next level. Every append creates an Ed25519-signed
`TransparencyLogCheckpoint` containing the exact log ID, tree size, root hash,
previous checkpoint ID, and signer identity. Checkpoint IDs include the
signature.

`TransparencyInclusionProof` carries the minimum ordered sibling list needed
for one leaf at the current checkpoint. Verification derives left/right
positions from the leaf index and level width.

`TransparencyConsistencyWitness` is deliberately simple and reviewable in
schema 1: it contains the complete ordered leaf-ID list for the newer tree.
Verification recomputes the old root from the exact prefix and the new root
from the complete list. This is an `O(n)` full-prefix witness, not a compact
RFC 6962 consistency proof. Load and caller budgets bound its practical use.
A future schema may add compact proofs without changing schema 1.

## Directory layout and publication

```text
manifest.json
records/
  00000000000000000000-<record-id>.json
  00000000000000000001-<record-id>.json
  ...
```

The immutable manifest stores only the log identity and a canonical checksum;
it does not store a mutable head. Each append atomically publishes one
immutable record containing both the leaf and its signed checkpoint. This
single-record design avoids a committed leaf without its checkpoint.

Publication uses:

- a cross-process `.writer-lock` directory;
- caller-supplied expected-checkpoint optimistic concurrency;
- complete fresh-log replay before mutation;
- complete candidate-log validation before writing; and
- a sibling temporary file followed by an atomic rename.

Opening rejects indirect roots, manifests, record directories, and records;
unexpected root/record entries; unknown schemas; malformed names or sequence
gaps; duplicate or reordered records; invalid identities, signatures, roots,
or chains; and configured resource-limit violations. Publication also enforces
the open log's record-count, per-observation/aggregate-attestation, record-byte,
and cancellation budgets. Abandoned `.tmp-` record entries are ignored.

## Rollback and transparency boundary

A caller that retains checkpoint `C2` will reject a directory rolled back to
`C1`. If the log directory, identity pin, public key, and expected checkpoint
are all rolled back together, RBF-Safe cannot detect the rollback.

A log signer can equivocate by signing different roots for the same tree size.
RBF-Safe verifies any supplied checkpoint, inclusion proof, and consistency
witness but does not provide a network service, witness cosigning, gossip,
global uniqueness, availability, trusted wall-clock time, or legal
non-repudiation. Deployments that require split-view detection must distribute
and compare signed checkpoint IDs through independent authenticated channels.

The fixed `data/transparency_log_schema1` fixture contains two synthetic
records: one reviewed deployment anchor and one two-source runtime-observation
set. It contains public interoperability material only.

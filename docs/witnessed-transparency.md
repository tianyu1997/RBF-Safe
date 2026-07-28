# Witnessed transparency and checkpoint gossip

RBF-Safe 3.14 adds a transport-neutral witness layer above the local
transparency log. It makes independently observed checkpoints comparable and
detects contradictory views without treating software statements as robot
execution authority.

Include `<rbfsafe/witness.h>` and link `RBFSafe::witness`. The aggregate
`RBFSafe::rbfsafe` target includes it.

## Trust and evidence boundary

A checkpoint witness is an Ed25519 statement by one active,
publication-capable key in a caller-supplied `ServiceTrustBundle`. The signed
message binds:

- log identity;
- checkpoint identity, tree size, and Merkle root;
- exact trust-bundle identity and sequence;
- witness service and key identity.

`TransparencyCheckpointWitnessPolicy` defaults to two distinct services and
excludes the transparency-log signer. Assembly sorts cosignatures
canonically, rejects duplicates, verifies every signature, and enforces the
quorum. The library never discovers witnesses or trust roots and never stores
secret keys.

All witness, gossip, conflict, report, record, and archive values carry
`EvidenceLevel::Unknown`; `authorizes_execution()` is always false. A valid
signature proves only that the corresponding software key signed the exact
message. It does not prove physical sensing, key custody, trustworthy time,
network freshness, controller tracking, or distributed consensus.

## Compact append-only consistency proof

`TransparencyLog::compact_consistency_proof(old_tree_size)` replaces the
schema-1 explicit leaf-list witness when a compact proof is desired. The proof
contains:

1. the complete-subtree frontier for the old tree; and
2. aligned complete subtrees covering the appended range.

The verifier reconstructs the old root, incrementally appends every supplied
subtree, and reconstructs the new root. It also verifies exact log,
checkpoint, size, root, structure, ordering, and deterministic proof identity
bindings. Public limits are 64 old-frontier nodes and 128 appended subtrees.
Proof size and verification work are logarithmic in the represented tree
ranges; the proof does not disclose historical leaf identities. The current
in-memory generator replays retained leaf IDs to derive the frontier and
subtree hashes, so generation work is linear in the retained range.

The earlier `TransparencyConsistencyWitness` remains readable and available
for compatibility. Its explicit ordered leaf list is not used by gossip
schema 1.

## Authenticated gossip

`sign_transparency_checkpoint_gossip` signs a complete witnessed checkpoint
and an optional compact proof. Its transport-neutral message additionally
binds:

- recipient service;
- sender service and key;
- sender sequence starting at one;
- parent gossip ID;
- exact trust-bundle ID and sequence.

Transport applications may exchange this value over any authenticated or
unauthenticated channel. The object signature, not the channel, provides its
content authentication. Replay, delivery, confidentiality, endpoint
authorization, and denial-of-service controls remain application concerns.

`audit_transparency_checkpoint_gossip` first authenticates every message and
witness quorum, rejects duplicate messages, and groups checkpoints by
identity. It then:

- reports `SameSizeEquivocation` when different signed checkpoints claim the
  same tree size;
- reports `InvalidConsistencyProof` when a signed compact proof cannot link
  its old and new checkpoints;
- builds a directed graph from valid compact proofs and checks reachability
  for every increasing checkpoint pair.

The deterministic result is:

| Status | Meaning |
|---|---|
| `Consistent` | Every increasing checkpoint pair is linked and no conflict was found |
| `Incomplete` | No conflict was found, but at least one increasing pair lacks a proof path |
| `SplitView` | Same-size equivocation or an invalid signed consistency proof was found |

`Incomplete` is not silently upgraded to consistency. `SplitView` is an audit
alert, not a collision certificate or execution decision. Message,
checkpoint, pair, and graph-step budgets plus cancellation bound the audit.

## Gossip archive schema 1

`TransparencyGossipArchive` persists authenticated messages in this directory
layout:

```text
archive/
  manifest.json
  records/
    00000000000000000000-<record-id>.json
    00000000000000000001-<record-id>.json
```

The manifest has format `rbfsafe-transparency-gossip-archive`, schema `1`,
the complete log identity, exact trust-bundle ID and sequence, library
version, and a canonical identity checksum. Each record has format
`rbfsafe-transparency-gossip-record`, schema `1`, a global sequence and parent
record ID, the pinned log/trust IDs, and the complete authenticated gossip
message.

The reader rejects unknown root entries, indirect archive/record paths,
unknown schema, malformed or oversized JSON, filename/sequence/parent
mismatch, identity mismatch, invalid signatures, invalid witness quorum, and
per-sender chain discontinuities. Per-sender chains are scoped by sender
service, sender key, and recipient. They start at sequence one and bind the
previous gossip ID.

Publication requires the caller-observed archive head. A cross-process
directory lock excludes concurrent writers, the archive is reopened under the
lock, and one complete immutable record is atomically renamed into place.
Existing records are never rewritten. Empty abandoned `.tmp-` directories are
ignored; other unexpected entries fail closed. Schema 1 pins one exact trust
bundle and does not rotate it in place.

Archive load options bound record count, witnesses, proof subtrees, manifest
and record bytes. Its audit applies separate unique-checkpoint, pair-check,
graph-step, and cancellation budgets.

## Quickstart and fixed fixture

The Python quickstart can create the log and gossip archive together:

```bash
python examples/transparency_log_quickstart.py \
  data/bounded_execution_session_schema1 \
  new-ledger new-transparency-log new-gossip-archive
```

`data/transparency_gossip_archive_schema1` is the deterministic two-record
interoperability fixture. Its caller pins are:

- log ID:
  `e77f9b5d98d731c0b2e6f41486c3c6870488962aa77d5c12fca4eb5e160655d4`;
- trust bundle:
  `3b295bc13d0831ace4bc8a73349dc87f249d09c238468c4058f506a94554c780`;
- archive head:
  `fd5ac959b484ada7ea2ce15e7cc8bccf41d8b6eaa368d9dafc2aedcdb0036514`.

Both C++ and Python inspectors require the complete caller-pinned log identity,
trust history/checkpoint, exact bundle, and archive head before replaying the
fixture.

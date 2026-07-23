# Policy feedback schema 1

`PolicyFeedbackDatabase::save` writes an independently versioned directory:

```text
policy-feedback/
├── manifest.json
└── records.json
```

This is not an Atlas, corridor, region database, version store, or model
checkpoint. Its schema number evolves independently from the RBF-Safe library
version.

## Manifest

`manifest.json` contains:

- `format`: `rbfsafe-policy-feedback`;
- `schema`: `1`;
- `library_version`: the writer's RBF-Safe version;
- `records`: the exact payload record count; and
- `payload_sha256`: SHA-256 of the complete `records.json` bytes.

The checksum covers formatting and the terminating newline. Readers validate
the manifest before parsing records and reject unknown format/schema pairs as
`IncompatibleFormat`.

## Payload

`records.json` has format `rbfsafe-policy-feedback-records`, schema `1`, and a
`records` array. Each entry stores:

- deterministic record, proposal, policy-decision, and optional
  shield-decision IDs;
- robot and scene SHA-256 identities;
- policy/task/episode IDs, decimal-string `sequence`, confidence,
  uncertainties, observation age, and inference latency;
- numeric action type, gate reason, feedback label, and evidence level;
- requested and output targets; and
- repair distance.

The unsigned 64-bit sequence is encoded as a decimal string so JSON number
rounding cannot change its identity. Enum values are part of schema 1 and must
not be inferred from display strings. Record IDs are SHA-256 of the canonical
record content excluding the ID itself. Loading recomputes every ID, validates
all referenced SHA-256 strings, rejects duplicate records, checks label/evidence
consistency, and rejects `RuntimeExecutable` evidence.

## Limits and failure behavior

`PolicyFeedbackLoadOptions` defaults to at most 1,000,000 records and
512 MiB of payload. Limits are checked before payload parsing where possible.
Callers handling untrusted databases should choose smaller application-specific
budgets. Invalid limits return `InvalidArgument`, exceeded limits return
`ResourceLimit`, malformed/checksum/identity failures return `CorruptData`, and
filesystem failures return `IoError`.

Saving writes a unique sibling temporary directory, completes both files, and
then renames it into place. Existing destinations are rejected unless
`SaveOptions::overwrite` is explicit. During overwrite, the prior directory is
staged as a sibling backup and restored if publication fails. Temporary and
backup directory names are implementation details and are not part of schema 1.

Byte-identical valid input records in the same order produce byte-identical
files across supported platforms. Record order is retained because it carries
batch/training alignment; the database does not silently sort observations.

When produced by `CalibratedPolicySafetyGate`, feedback metadata contains the
effective conservative confidence passed to the base policy gate. The aligned
`CalibratedPolicyApplication` carries the corresponding raw metadata and
profile ID in memory. Schema 1 feedback alone does not imply that calibration
was used and does not persist that raw/effective mapping.

## Inspection

The native tool prints aggregate label counts:

```bash
rbfsafe-inspect policy-feedback
```

The Python entry point also filters records:

```bash
rbfsafe-inspect policy-feedback --policy-id vla-primary \
  --task-id shelf-pick --feedback-label selected_repaired
```

Inspection validates the full database before returning any summary or record.

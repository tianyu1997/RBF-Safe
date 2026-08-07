# Safety memory directory format, schema 1

A safety memory is a versioned directory independent from every Atlas,
corridor, region-database, version-store, and policy-feedback schema.

```text
memory/
  manifest.json
  memory.json
```

`manifest.json` contains:

- `format`: `rbfsafe-safety-memory`;
- `schema`: `1`;
- the writing `library_version`;
- artifact and event counts; and
- `payload_sha256`, the SHA-256 of the exact `memory.json` bytes.

`memory.json` contains the `rbfsafe-safety-memory-records` format marker,
schema, next logical sequence, sorted artifact records, and chronological audit
events. Unsigned 64-bit generations and sequences are decimal strings so JSON
number precision cannot change their identities. Artifact and event IDs are
SHA-256 hashes of canonical content; lifecycle state and generations do not
change an artifact's stable ID.

## Validation and limits

The reader checks the manifest before parsing the payload. It enforces caller
configured payload, artifact, and event limits (defaults: 512 MiB, 1,000,000
artifacts, and 4,000,000 events), verifies the checksum, validates every enum,
digest, string, tag, deterministic ID, and duplicate, then replays the complete
event history. Replay proves registration sequence, state transitions,
generation counts, final states, and the next sequence are consistent.

Unknown schema or format markers return `IncompatibleFormat`. Checksum,
identity, ordering, history, or record corruption returns `CorruptData`.
Resource limits fail before allocating the declared collection.

Saving writes a unique sibling temporary directory, completes both files, then
publishes the directory. Existing destinations are rejected unless overwrite
is explicit; overwrite stages the old directory as a backup until publication
succeeds. The format stores locators as opaque metadata and never follows them
during load or validation.

## Compatibility

Schema 1 is the first persistent robot-memory format. There is no legacy cache
import and no implicit conversion from a policy-feedback database or Atlas
version store. Future schema migrations must write a new destination, preserve
the source, and replay equivalent lifecycle history before publication.

RBF-Safe 3.1 can place unchanged schema-1 directories inside an immutable
[`SafetyMemoryStore`](safety-memory-store.md). The wrapper has its own schema;
it does not alter or reinterpret these payload bytes.

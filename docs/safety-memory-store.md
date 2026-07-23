# Transactional safety-memory revision store

RBF-Safe 3.1 adds `SafetyMemoryStore`, an immutable revision history around
schema-1 `SafetyMemory` directories. It is intended for multiple industrial
processes that need fail-closed publication and historical reads without
sharing mutable C++ objects.

## Public workflow

```cpp
auto store = SafetyMemoryStore::create("robot-memory-store", memory);
if (!store)
    return store.error();

const auto expected = store.value().current_revision_id();
auto updated = memory.transition(artifact_id, generation,
                                 MemoryArtifactState::Stale,
                                 "scene maintenance");
if (!updated)
    return updated.error();

auto revision = store.value().publish(memory, expected);
if (!revision)
    return revision.error();
```

Each caller reads the current revision, performs work on its own
`SafetyMemory`, and supplies the revision it observed to `publish`. If another
writer committed first, publication returns `IdentityMismatch`; it never
overwrites or merges the newer history. Publishing unchanged memory is
idempotent and returns the existing head.

`load_current` and `load_revision` verify the referenced schema-1 memory
payload and its deterministic memory identity. `SafetyMemory::identity()`
hashes canonical artifact state/generation and the complete event-ID sequence;
it is an integrity identity, not a signature or authorization credential.

## Crash and concurrency model

The store uses an atomically created `.writer-lock` directory to serialize
writers across processes. A writer then:

1. reopens and validates the store;
2. compares the current revision to the caller's expected revision;
3. writes a new immutable schema-1 memory directory;
4. writes a temporary commit document; and
5. atomically renames it to a new, never-overwritten commit filename.

Readers derive the head from the highest strictly continuous commit. A crash
before step 5 leaves the previous head active; a crash after step 5 exposes the
complete new revision. An unreferenced revision directory is safe and can be
reused by a retry with identical content.

The writer lock has no automatic timeout. If a process dies while holding it,
an operator must first establish that no writer is active and then remove only
the exact `.writer-lock` directory. RBF-Safe deliberately does not guess that
a lock is stale, because doing so could admit concurrent writers.

These guarantees require a local filesystem that provides atomic directory
creation and same-filesystem rename. Network shares, cloud-synchronized
folders, and distributed filesystems are not a reviewed coordination backend;
deployments on them must provide an independently validated serialization
service.

## Store schema 1

```text
memory-store/
  manifest.json
  commits/
    00000000000000000000-<revision-id>.json
    00000000000000000001-<revision-id>.json
  revisions/
    <revision-id>/
      manifest.json
      memory.json
```

The root manifest binds the root revision with a canonical SHA-256 identity.
Every commit records a decimal-string sequence, revision ID, parent revision,
memory identity, schema, and its own canonical identity. Revision IDs bind the
sequence, parent, and memory identity. On open, filenames, IDs, checksums,
sequence continuity, parent links, revision paths, and the current payload are
validated under caller-configured metadata, revision, artifact, event, and
payload limits.

Commit and revision files are immutable. Store schema 1 is independent of the
safety-memory payload schema and every Atlas/corridor/feedback schema. Unknown
formats return `IncompatibleFormat`; broken chains or identities return
`CorruptData`; a held writer lock and configured count limits return
`ResourceLimit`.

Runnable examples are
[`examples/safety_memory_store_quickstart.cpp`](../examples/safety_memory_store_quickstart.cpp)
and
[`examples/safety_memory_store_quickstart.py`](../examples/safety_memory_store_quickstart.py).

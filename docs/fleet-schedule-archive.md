# Versioned fleet-schedule archives

RBF-Safe 3.2 adds `FleetScheduleArchive`, a deterministic linear history of
multi-robot reservation analyses. It persists the existing v3.0
`FleetScheduleReport` without turning that report into a geometric
`Certificate` or an execution authorization.

## Public workflow

Each version binds the exact `FleetSnapshot`, complete canonical reservation
report, `SafetyMemory::identity()`, parent version, and sequence number:

```cpp
auto archive = FleetScheduleArchive::create(fleet.fleet_id);
if (!archive)
    return archive.error();

auto root = archive.value().publish(fleet, memory, reservations, "");
if (!root)
    return root.error();

auto next = archive.value().publish(fleet, memory, revised_reservations,
                                    root.value().id);
if (!next)
    return next.error();

auto saved = archive.value().save("fleet-schedules");
```

The first publication requires an empty expected head. Later publications
require the exact current version ID. A stale expected head returns
`IdentityMismatch` and leaves the archive unchanged. Publishing the same
fleet, memory, and canonical report at the current head is idempotent.

`verify_version` requires the exact stored fleet snapshot and memory identity,
reruns `analyze_fleet_schedule`, and compares the deterministic report ID. It
therefore detects a source artifact that became stale or any mismatch between
the supplied operational context and the historical version.

## Schema 1

```text
fleet-schedules/
  manifest.json
  schedules.json
```

`manifest.json` records the format and schema, informational library version,
fleet/head identities, aggregate version/member/reservation/conflict/pair
counts, and the SHA-256 of `schedules.json`. The payload stores every version
in sequence order. JSON integer fields that may exceed binary64's exact range,
including ticks and sequences, are decimal strings.

The reader checks both byte integrity and semantics: schema, file sizes,
aggregate counts, SHA-256, canonical fleet members and reservations, snapshot,
reservation, report and version identities, conflict recomputation, sequence
continuity, parent links, and the active head. `FleetScheduleArchiveLoadOptions`
bounds metadata and payload bytes plus total versions, members, reservations,
conflicts, and pair evaluations before accepting the archive.

Saving writes a same-directory temporary tree and publishes it by rename.
Existing destinations are refused unless `SaveOptions::overwrite` is explicit.
Overwrite uses a temporary sibling backup so a failed publish can restore the
old directory. These guarantees assume reliable same-filesystem rename; they
do not provide distributed consensus for network or cloud-synchronized
storage.

## Safety-memory integration

A schedule version can be cataloged after publication as a
`MemoryArtifactType::FleetSchedule` whose content digest is the version ID and
whose locator points to the archive. Use `EvidenceLevel::Unknown`: the archive
preserves a coordination analysis and must not promote it to regional,
connectivity, or runtime evidence.

Registering the archive changes `SafetyMemory::identity()`. Consequently, a
version continues to verify against the earlier memory revision that supplied
its source artifacts. In multi-process deployments, retain that revision in a
`SafetyMemoryStore`, publish the archive version, then register the resulting
version in a later memory revision. This avoids a circular identity claim and
keeps historical replay exact.

## Explicit limits

`ConflictFreeUnderDeclaredEnvelopes` means only that caller-declared AABBs and
separation margins do not conflict in overlapping half-open logical tick
windows. The archive does not derive swept robot geometry, synchronize clocks,
model communication or controller latency, authenticate identities or
locators, sign records, coordinate distributed writers, or authorize motion.
Use independent runtime monitoring, hardware interlocks, and deployment risk
controls.

Runnable examples are
[`examples/fleet_schedule_archive_quickstart.cpp`](../examples/fleet_schedule_archive_quickstart.cpp)
and
[`examples/fleet_schedule_archive_quickstart.py`](../examples/fleet_schedule_archive_quickstart.py).
The committed `data/fleet_schedule_archive_schema1` directory is the fixed
cross-platform interoperability fixture.

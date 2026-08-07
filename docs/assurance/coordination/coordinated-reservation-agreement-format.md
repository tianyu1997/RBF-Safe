# Coordinated reservation agreement schema 1

RBF-Safe 4.6 writes one UTF-8 JSON file with format
`rbfsafe-coordinated-reservation-agreement` and schema `1`. The file is
independent of occupancy-bundle and rotating-history schemas.

## Envelope

```json
{
  "checksum": "<sha256>",
  "format": "rbfsafe-coordinated-reservation-agreement",
  "library_version": "4.6.0",
  "payload": { "...": "..." },
  "schema": 1
}
```

The checksum is SHA-256 over the canonical JSON encoding of `payload`.
`library_version` is diagnostic and does not participate in the agreement
identity.

The loader requires the exact envelope and payload field sets. Unknown schema
numbers fail with `IncompatibleFormat`; malformed known files, wrong
checksums, duplicate or unsorted participants, invalid identities, and
inconsistent semantic fields fail with `CorruptData` or the more specific
identity/argument status.

## Payload fields

The payload contains:

| Field | Meaning |
|---|---|
| `storage_schema` | Agreement value schema, exactly `1` |
| `id` | SHA-256 identity of the canonical semantic agreement |
| `protocol_id` | Application-owned coordination protocol name |
| `round` | Positive decimal round |
| `parent_agreement_id` | Empty only for round one; otherwise exact parent |
| `evaluation_tick` | Logical tick at which all publications were verified |
| `valid_from_tick`, `valid_through_tick` | Intersection of participant windows |
| `occupancy_bundle_id` | Exact common fleet-occupancy identity |
| `occupancy_report_id` | Exact common separation-report identity |
| `timeline_id` | Exact common logical timeline |
| `workspace_frame_id` | Exact common workspace frame |
| `minimum_separation` | Finite, nonnegative requested separation |
| `payload_digest` | SHA-256 of the exact common occupancy bytes |
| `payload_bytes` | Decimal length of the exact common occupancy bytes |
| `participants` | Canonically sorted non-empty participant array |

Unsigned 64-bit values are encoded as canonical decimal strings to avoid JSON
number precision loss. `minimum_separation` is encoded as a finite JSON
number using the repository's canonical floating-point formatter.

Each participant contains:

| Field | Meaning |
|---|---|
| `storage_schema`, `id` | Participant schema and deterministic identity |
| `deployment_id`, `occupancy_id` | Unique deployment and its exact occupancy |
| `stream_id`, `publisher_service_id`, `publisher_key_id` | Publication namespace and key |
| `publisher_sequence` | Positive decimal stream sequence |
| `trust_root_bundle_id`, `trust_head_bundle_id` | Retained trust prefix |
| `publication_trust_bundle_id` | Historical bundle that verified the publication |
| `publication_root_id`, `publication_head_id` | Retained publication prefix |
| `verified_publication_id` | Tick-specific verification identity |
| `payload_digest`, `payload_bytes` | Exact common payload binding |
| `valid_from_tick`, `valid_through_tick` | Publication's closed window |

Participants are ordered by deployment ID, then publisher service and stream.
Deployment IDs and publisher-service/stream pairs must be unique.

## Identity construction

The participant ID and agreement ID are SHA-256 hashes of domain-separated
canonical JSON semantic documents. The outer checksum and
`library_version` are excluded. Field changes, participant additions,
publisher or trust-head changes, payload changes, and round or parent changes
therefore produce new identities.

Implementations must use the public RBF-Safe constructors and loaders rather
than recreating identity serialization independently.

## Publication

`save(path)` writes a compact same-directory temporary file, flushes and
validates it, and then publishes the destination. Existing destinations are
rejected unless `SaveOptions.overwrite` is explicitly enabled. Symbolic-link
inputs and non-regular files are rejected.

Schema 1 stores the agreement only. The referenced occupancy payload and
rotating histories are separate artifacts and must be retained and supplied
to semantic replay. A successful file load verifies structure, checksum, and
the agreement's internal identity; only
`verify_coordinated_reservation_agreement` verifies the external histories
and exact occupancy bytes.

## Resource and compatibility policy

Loading applies `maximum_participants`, `maximum_payload_bytes`, cancellation,
regular-file, exact-field-count, string, and numeric bounds before returning
a value. No older history or standalone publication is implicitly enrolled
into an agreement. No future schema is interpreted as schema 1.

Schema and library versions are independent. RBF-Safe 4.6 writes schema 1;
future compatible library releases may continue to write it. A semantic
format change requires a new schema and an explicit migration policy.

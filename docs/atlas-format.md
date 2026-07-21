# Atlas directory format v1

## Directory layout

```text
atlas/
├── manifest.json
├── certificates.json
├── regions.bin
├── graph.bin
└── lect/
    └── nodes.bin
```

`manifest.json` records format and schema versions, library version, dimension,
robot/scene SHA-256 digests, record counts, and the SHA-256 of every payload.
`certificates.json` keeps the safety policy and identity records reviewable.

## Primitive binary encoding

All integers and IEEE-754 binary64 values are explicitly little endian. Files
have no alignment padding. `u8`, `u32`, and `u64` are unsigned widths in bits;
`f64` is the bit representation of a finite C++ `double`. A string is a `u32`
byte length followed by that many UTF-8 bytes.

No native C++ object layout is written to disk.

## `lect/nodes.bin`

Header:

| Field | Encoding |
|---|---|
| magic | 8 bytes: `RBFLECT1` |
| schema | `u32`, value `1` |
| dimension | `u32` |
| node count | `u64` |
| split strategy | `u8`, normalized-longest-axis is `0` |
| minimum normalized width | `f64` |
| root axes | `dimension` pairs of lower/upper `f64` |

Each node then stores its path string, leaf flag (`u8`), split dimension
(`u32`), and `dimension` lower/upper `f64` pairs. Nodes are stored in stable
tree insertion order; child keys are derived by appending `0` or `1`.

## `regions.bin`

Header: 8-byte magic `RBFREGN1`, schema `u32`, dimension `u32`, and region
count `u64`.

Each region stores region ID `u64`, certificate index `u64`, component ID
`u64`, source LECT path string, and `dimension` lower/upper `f64` pairs.

## `graph.bin`

Header: 8-byte magic `RBFGRPH1`, schema `u32`, and vertex count `u64`. The
remaining payload is deterministic CSR:

1. `vertex_count + 1` offset values as `u64`, beginning with zero;
2. total directed adjacency count as `u64`, equal to the last offset;
3. that many neighbor indices as sorted `u64` values.

The graph must be symmetric, contain no self edges, and use strictly increasing
neighbor lists. Each undirected edge therefore appears twice.

## `certificates.json`

The document has `format: "rbfsafe-certificates"`, `schema: 1`, and a
`certificates` array. Every record contains its deterministic certificate ID,
evidence level, robot and scene digests, clearance lower bound, and validation
policy (`algorithm`, `algorithm_version`, and `obstacle_padding`). v1 accepts
only `certified_region` records.

## Publication and loading

`save()` writes a uniquely named sibling temporary directory, flushes all
payloads, computes checksums, writes the manifest, validates publication state,
and only then renames the directory into place. Existing destinations are
rejected unless overwrite is explicit; overwrite uses a recoverable sibling
backup during publication.

`load()` validates the manifest before allocation, verifies every checksum,
enforces dimension/count/file-size limits, decodes explicit fields, rejects
non-finite or invalid bounds, checks certificate identity and graph symmetry,
and rejects truncated or trailing bytes.

## Compatibility

Schema v1 is independent of the C++ library version and legacy
RapidBoxForest/LECT caches. Unknown schemas return `IncompatibleFormat` rather
than being guessed. Any future incompatible layout requires a new schema and a
documented migration or explicit rejection path.

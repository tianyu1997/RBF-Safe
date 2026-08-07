# Region database schema 1

A saved `RegionDatabase` is a versioned directory independent from Atlas and
HiPaC corridor storage:

```text
database/
  manifest.json
  regions.json
```

`manifest.json` records format `rbfsafe-region-database`, schema `1`, library
version, C-space dimension, robot and scene SHA-256 identities, scene version,
record/certificate counts, and the SHA-256 checksum of `regions.json`.

`regions.json` is canonical deterministic JSON with format
`rbfsafe-region-database-records`. It stores:

- the complete certificate array, including subject and optional transition
  lineage fields;
- every stable record ID, certificate index, component, source string, and
  optional per-link workspace dependency; and
- type-specific geometry for AABB, OBB, Portal half-spaces, TrajectoryTube
  chains, zonotope generators, and Taylor linear/remainder coefficients.

Identifiers and generator counts are decimal strings so their full unsigned
64-bit values survive JSON implementations that represent numbers as binary64.
Finite geometry coefficients and bounded array counts are required.

## Publication

Saving writes and validates a unique sibling temporary directory before one
filesystem rename publishes it. Existing destinations are rejected unless
`SaveOptions::overwrite` is true. Overwrite stages the old directory under a
unique sibling name and restores it if publication fails.

## Load validation

The reader rejects:

- unknown manifest or payload schemas;
- missing files, malformed JSON, non-finite values, and truncated payloads;
- payloads larger than the fixed resource cap or counts beyond configured
  hard maxima;
- SHA-256 mismatches;
- duplicate record or certificate IDs and invalid certificate indices;
- certificate identities or geometry subject digests that do not match;
- invalid OBB bases, Portal half-spaces/witnesses, or higher-order arrays;
- missing parent cells/Portals in connectivity records; and
- component labels that disagree with the deterministically rebuilt graph.

The graph is derived in memory and is not serialized. A loaded database is
therefore never trusted merely because its component numbers look plausible.

Schema 1 has no compatibility relationship with legacy RapidBoxForest caches,
Atlas schemas 1/2, corridor schema 1, or version-store schema 1. Conversion is
explicit through the public import APIs.

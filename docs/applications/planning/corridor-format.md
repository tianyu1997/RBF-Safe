# Corridor schema 1

A saved `HipacCorridor` is a versioned directory:

```text
corridor/
├── manifest.json
└── corridor.json
```

`manifest.json` contains `format: "rbfsafe-corridor"`, schema number,
library version, dimension, robot and scene digests, record counts, and the
SHA-256 checksum of `corridor.json`.

`corridor.json` contains `format: "rbfsafe-corridor-records"`, schema number,
OBB region records, witness portal records, and their subject-bound
certificates. A region stores its center, row-major basis, half-widths,
component, source segment interval, entry, and exit. A portal stores both
region IDs and the shared witness configuration. Unsigned 64-bit IDs are JSON
strings so no precision is lost in readers whose number type is IEEE double.

The loader validates the manifest and resource limits before allocating large
arrays, verifies the payload checksum before parsing records, reconstructs
each OBB, recomputes every certificate identity and geometry subject digest,
checks portal membership in both adjacent cells, and recomputes connected
components. Unknown schemas return `IncompatibleFormat`; malformed or
identity-inconsistent records return `CorruptData`.

Saving uses a sibling temporary directory and publishes it only after both
files are complete. Existing destinations are rejected unless overwrite is
explicitly enabled. Overwrite stages the previous directory as a sibling
backup until the new directory has been published.

Corridor schema 1 is independent of Atlas schema 1. Neither format embeds or
interprets RapidBoxForest caches.

# Release procedure

## 1. Prepare

1. Move user-visible entries from `Unreleased` in `CHANGELOG.md` into the new
   version and date.
2. Confirm matching versions in `CMakeLists.txt`, `pyproject.toml`,
   `include/rbfsafe/version.h`, and `CITATION.cff`.
3. Review API, safety, input, storage, and compatibility documentation.
4. Confirm provenance for all migrated or derived code and fixtures.
5. Run `python tools/check_api_surface.py --root .`; any snapshot update must
   have an explicit compatibility review.

## 2. Validate

Run the CI-equivalent matrix from a clean checkout:

- Ubuntu 22.04/24.04 with GCC and Clang;
- ASan and UBSan with Clang;
- Windows with MSVC;
- Python 3.10-3.12 installed-wheel tests on Linux and Windows;
- independent downstream CMake `find_package(RBFSafe)` consumption;
- clang-format and warnings-as-errors.

Inspect the source tree for build products, absolute paths, paper assets,
legacy caches, generated Atlas directories, and old-library dependencies.
Run the C++ and Python quickstarts, save/load/query/update an Atlas,
save/load/query a generalized region database, and verify the fixed Atlas
schema-v1 compatibility fixture and schema-v2 payload hashes. Build the
certified roadmap and optimization quickstarts, exercise all four supported
OMPL planner kinds when that component is enabled, and run the MoveIt request,
response, IK, and constraint-sampler plugin tests in a sourced Jazzy workspace.
Run the runtime-shield quickstart and tests for all three action types,
accept/repair/reject outcomes, deterministic IDs, proposal selection,
telemetry, cancellation, and monitor classifications.
Run both policy-safety quickstarts and test every metadata rejection reason,
selection mode, duplicate handling, aligned feedback label, deterministic ID,
query filter, load limit, checksum failure, and native/Python inspection path.
Build `rbfsafe-release-benchmark`, run its 128-iteration smoke gate and
8192-iteration soak gate, and confirm zero false-safe point checks and an exact
match with the committed cross-platform logical digest. Inspect timing and
memory only as release diagnostics.

## 3. Package

1. Build source and wheel distributions with `python -m build`.
2. Inspect wheel contents; it must contain only the Python package, extension,
   metadata, and license.
3. Install each wheel into a clean environment and run `pip check` plus the
   Python test suite.
4. Install the CMake package to an empty prefix and rebuild `tests/consumer`.

## 4. Publish

1. Merge or commit the exact tested tree on `main`.
2. Create an annotated `vX.Y.Z` tag on that commit and push it.
3. Create a GitHub release from the changelog, attach supported wheels and the
   source archive, and identify the Atlas schema version.
4. Verify release downloads in clean Linux and Windows environments.

Atlas, corridor, region-database, version-store, policy-feedback, or
safety-memory schema changes require an
independent schema number, fixed-format fixtures, and a documented reader or
explicit incompatibility error. A library version change must never silently
reinterpret an existing storage schema.

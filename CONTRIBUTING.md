# Contributing

Thank you for helping improve RBF-Safe. Contributions should keep the
certification claim small, conservative, deterministic, and auditable.

## Before opening a change

Use a GitHub issue for substantial API, schema, or certification changes so
the intended safety claim can be reviewed before implementation. Security
vulnerabilities and suspected false-positive certificates must follow
[SECURITY.md](SECURITY.md), not a public issue.

## Development setup

RBF-Safe requires CMake 3.23 or newer and a C++20 compiler. For Python work,
use Python 3.10–3.12 and an isolated environment.

```bash
cmake -S . -B build \
  -DRBFSAFE_BUILD_TESTS=ON \
  -DRBFSAFE_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure

python -m venv .venv
python -m pip install --upgrade pip build pytest
python -m build --wheel
python -m pip install --force-reinstall dist/rbfsafe-*.whl
pytest tests/test_python.py
```

Run clang-format before submitting C++ changes:

```bash
clang-format -i $(find include src tests tools examples python \
  -type f \( -name '*.h' -o -name '*.cpp' \))
```

## Change rules

- Keep changes within one responsibility layer: `geometry`, `lect`, or
  `atlas`, unless an API change genuinely crosses layers.
- Public headers use standard-library value types and must not expose Eigen,
  JSON, pybind11, or storage implementation details.
- Expected failures use `Result<T>`. Assertions are reserved for internal
  programmer invariants.
- New certification logic must state the mathematical claim, conservative
  assumptions, identity inputs, and failure mode.
- Sampling may test or prioritize work but must never upgrade evidence to
  `CertifiedRegion`.
- Persistence changes require corruption tests, fixed-format regression
  coverage, and an explicit schema compatibility decision.
- Derived or materially reused work must update `docs/provenance.md` and retain
  applicable copyright notices.
- Do not commit build trees, wheels, caches, local paths, paper assets, or
  experiment outputs.

## Tests expected by change type

| Change | Minimum validation |
|---|---|
| Public API | Focused unit test plus downstream `find_package` consumer |
| Geometry/certification | Unit, golden differential, and containment property tests |
| LECT | Split, boundary lookup, overlap, stable-key, and persistence tests |
| Atlas | Duplicate/boundary seeds, budgets, cancellation, and connectivity tests |
| Persistence | Round trip, malformed inputs, checksums, and deterministic bytes |
| Python | Installed-wheel test; do not test only against the source tree |

## Pull requests

Use an imperative commit subject and keep commits reviewable. Pull requests
must describe the safety impact, affected modules, compatibility impact, and
commands used for validation. Complete the repository pull-request template.
All CI jobs must pass before merge.

# Examples

Python quickstarts remain directly under `examples/` so they can be run with
the paths shown in the user guides. C++ quickstarts are grouped by purpose:

```text
core/          geometry, Atlas, planning, IK, update, and shield
policy/        policy gates, calibration, and lifecycle monitoring
artifacts/     safety memory, attestations, transfer, and provenance
deployment/    public identity, reviewed deployment, bounded execution
coordination/  continuous occupancy and authenticated coordination
```

The CMake target names are unchanged. For example,
`examples/core/quickstart.cpp` is still built as `rbfsafe_quickstart`.

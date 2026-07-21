## Summary

Describe the user-visible outcome and why the change is needed.

## Safety and compatibility

- Evidence or certification semantics changed: yes / no
- Public C++ or Python API changed: yes / no
- Atlas schema or deterministic output changed: yes / no
- Migration or provenance documentation required: yes / no

Explain every “yes” answer, including the compatibility strategy.

## Validation

List the exact commands run and their results.

## Checklist

- [ ] Tests cover the new behavior and expected failures.
- [ ] Documentation and examples match the implementation.
- [ ] Public headers do not expose Eigen, JSON, or storage implementation types.
- [ ] New expected failures use `Result<T>` and a specific error code.
- [ ] No build output, local path, cache, credential, or private dataset is included.
- [ ] Substantially reused code is recorded in `docs/provenance.md`.

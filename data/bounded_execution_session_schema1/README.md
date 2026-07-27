# Synthetic bounded execution-session fixture

This directory is a deterministic RBF-Safe schema-1 interoperability fixture.
It contains a SafeAtlas, public trust history, signed checkpoint, reviewed
deployment profile, and one bounded execution session. All keys and signatures
are synthetic test data. No private key material is stored here, and none of
these files is a production trust anchor or permission to operate hardware.

The session itself has `Unknown` evidence and does not authorize execution.
Only an exact command index, exact configuration, and dispatch time inside the
stored monotonic window can produce a scoped `RuntimeExecutable` command
authorization after the entire fixture has been reverified.

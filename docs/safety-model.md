# Safety model

## Certified claim

`CertifiedRegion` means that, for every configuration in one C-space AABB,
each represented link's conservative workspace envelope is disjoint from
every AABB obstacle after the configured padding. Affine arithmetic encloses
endpoint locations; LinkIAABB encloses the segment between paired endpoint
envelopes plus the link radius.

Obstacle and envelope contact counts as overlap, so it cannot produce a
certificate. A positive reported clearance lower bound is measured between
the conservative link AABBs and obstacle AABBs.

## Evidence discipline

An overlap is `Undetermined`, not proof of collision. Atlas construction may
subdivide an undetermined cell, but branches that reach depth, width, node, or
cancellation limits remain uncertified. Samples may prioritize branches and
support regression tests; they never create or upgrade a regional certificate.

Region merging is allowed only when the rectangular union is independently
revalidated. Connectivity in v0.1 means two configurations belong to regions
in the same deterministic overlap/touch component. It does not produce a
continuous path, timing law, controller command, or execution guarantee.

## Identity and reuse

Certificates bind the canonical robot model, scene snapshot, validator name
and version, obstacle padding, evidence level, and clearance with SHA-256.
Changing any bound input invalidates direct reuse. Loaded Atlases must be
checked with `verify_compatible` against the current robot and scene.

Checksums detect accidental corruption and ordinary tampering; they are not
digital signatures and do not establish publisher authenticity.

## Assumptions

- Modified-DH parameters, joint limits, tool transform, link radii, units, and
  frame conventions accurately and conservatively describe the robot.
- Each represented link is conservatively covered by the straight segment
  between adjacent DH origins expanded by its configured radius.
- Every relevant static environment object is conservatively covered by a
  workspace AABB in the same coordinate frame and length unit.
- The scene does not change between certification and use.
- Supported compiler and floating-point modes preserve ordinary IEEE-754
  double behavior; unsafe fast-math transformations are not supported.

## Explicit exclusions in v0.1

- Robot self-collision is not checked.
- Joint bodies, cables, payloads, or end effectors are covered only if included
  by the supplied link radii and optional tool link.
- Dynamic obstacles, localization/calibration uncertainty, control error,
  deformation, and latency are not modeled automatically.
- AABB separation is the only workspace collision proof; no mesh, OBB, KDOP,
  portal, or swept-time validation is performed.
- `contains` and `connected` are not motion-planning or runtime-execution
  approvals.

RBF-Safe therefore does not replace emergency stops, independent collision
monitoring, controller limits, calibration checks, or application-specific
risk assessment.

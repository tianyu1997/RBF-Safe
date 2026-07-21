# Input formats

Robot and scene inputs use strict JSON schema 1. Unknown top-level fields are
currently ignored, but required fields, dimensions, finite values, and bounds
are validated. Schema changes are independent from Atlas schema changes.

## Robot JSON

```json
{
  "schema": 1,
  "name": "planar-2r",
  "joints": [
    {"alpha": 0.0, "a": 1.0, "d": 0.0, "theta": 0.0, "type": "revolute"}
  ],
  "joint_limits": [[-1.5, 1.5]],
  "link_radii": [0.05],
  "tool_frame": null
}
```

Each joint uses the modified-DH transform

```text
[ cosθ,       -sinθ,       0,       a ]
[ sinθ cosα,   cosθ cosα, -sinα, -d sinα ]
[ sinθ sinα,   cosθ sinα,  cosα,  d cosα ]
[ 0,           0,          0,       1 ]
```

For a revolute joint, the configuration value is added to `theta`. For a
prismatic joint, it is added to `d`. Angles are radians and lengths must use
one consistent unit; metres are recommended.

`joint_limits` has one closed interval per joint. `link_radii` has one
non-negative radius per transform-generated link: one per joint, plus one when
`tool_frame` is non-null. A tool frame is constant even though it uses the
same field shape as a joint.

The robot digest includes its name, joints, limits, radii, and tool frame.
Changing any of these values changes compatibility identity.

## Scene JSON

```json
{
  "schema": 1,
  "version": "workcell-2026-07-21",
  "obstacles": [
    {
      "id": "shelf-left",
      "lower": [0.4, -0.8, 0.0],
      "upper": [0.6, -0.2, 1.8]
    }
  ]
}
```

Obstacle IDs must be non-empty and unique. Each lower/upper array contains
three finite values and must satisfy `lower[i] <= upper[i]`. Obstacles are
sorted by ID before canonical hashing, so input array order does not change the
scene digest. The scene `version` is identity-bearing and must change whenever
the snapshot semantics change, even when geometry happens to remain equal.

RBF-Safe v0.1 supports only static workspace AABBs. Meshes, poses, dynamic
objects, self-collision models, and uncertainty models must be conservatively
converted outside the library and documented by the consumer.

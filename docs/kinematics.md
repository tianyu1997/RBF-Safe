# Kinematics and geometric Jacobian

`SerialRobotModel` uses the modified DH transform already used by the
IFK-AA envelope implementation:

\[
{}^{i-1}T_i =
R_x(\alpha_i)\,T_x(a_i)\,R_z(\theta_i + q_i)\,
T_z(d_i + q_i).
\]

Only the term associated with the joint type receives \(q_i\): revolute
joints add it to `theta`, while prismatic joints add it to `d`. The optional
tool frame is a fixed modified-DH transform. Joint limits are checked before
every point-kinematics or Jacobian evaluation.

## Point kinematics

- `forward_kinematics(q)` returns the base origin followed by every link-frame
  origin and the optional tool origin.
- `end_effector_pose(q)` returns workspace position and a normalized
  `x,y,z,w` quaternion.
- `end_effector_geometric_jacobian(q)` returns the analytic workspace-frame
  geometric Jacobian.

`GeometricJacobian` is always 6-by-N and uses row-major standard-library
storage. Rows 0–2 map joint rates to end-effector linear velocity; rows 3–5
map them to angular velocity. Revolute columns use
\(z_i \times (p_e-p_i)\) and \(z_i\). Prismatic columns use \(z_i\) and a
zero angular term. `at(row, column)` performs bounds checking.

```cpp
auto jacobian = robot.end_effector_geometric_jacobian(q);
if (!jacobian)
    return jacobian.error();

const double dx_dq0 = jacobian.value().at(0, 0);
```

```python
jacobian = robot.end_effector_geometric_jacobian(q)
assert jacobian.rows == 6
dx_dq0 = jacobian.at(0, 0)
```

The Jacobian is deterministic numerical kinematics, not collision evidence
and not an execution certificate. Consumers must still use certified regions,
corridors, trajectory audits, and exact runtime checks for the claims those
interfaces make.

## Conservative envelopes

`compute_endpoint_aabbs` exposes both endpoint generators. IFK-AA is the
default certified affine-arithmetic bound. CritSample mirrors the deterministic
RapidBoxForest boundary/`k*pi/2` candidate enumeration and is explicitly
non-certified because sampling may miss an interior extremum.

`compute_ifk_aa_link_envelope` evaluates interval forward kinematics over an
entire C-space AABB and returns one conservative workspace AABB per link. The
default `IfkAaLinkAabbValidator` signs `CertifiedRegion` evidence only when
every IFK-AA/LinkIAABB envelope is separated from every scene obstacle. Point
FK, Jacobian, and CritSample tests are regression evidence; they never upgrade
sampled results into region certificates.

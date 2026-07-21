# MoveIt 2 integration

`rbfsafe_moveit` is an optional ROS 2 Jazzy ament package tested against
MoveIt 2.12.4. It is separate from the core build and Python wheel. It exports
three pluginlib classes:

| Plugin | MoveIt base | Fail-closed action |
|---|---|---|
| `rbfsafe_moveit/CertifiedStartStateAdapter` | `PlanningRequestAdapter` | Reject a request whose start is not Atlas-covered |
| `rbfsafe_moveit/CertifiedTrajectoryAdapter` | `PlanningResponseAdapter` | Clear and reject a response unless its entire trajectory is `CERTIFIED` |
| `rbfsafe_moveit/SafeIkKinematicsPlugin` | `KinematicsBase` | Return only a `SafeConnected` single-tip IK solution |

MoveIt split preprocessing request adapters from postprocessing response
adapters. RBF-Safe uses the response interface for the final trajectory audit
so smoothing and time-parameterization changes can be checked after they run.

## Build

First install the RBF-Safe core, then build the ROS package:

```bash
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/install/core"
cmake --build build/core --parallel
cmake --install build/core

source /opt/ros/jazzy/setup.bash
colcon build --base-paths plugins/moveit2 \
  --packages-select rbfsafe_moveit \
  --cmake-args -DCMAKE_PREFIX_PATH="$PWD/install/core"
colcon test --packages-select rbfsafe_moveit
```

Source the resulting colcon install before starting MoveIt.

## Mandatory model mapping

The plugins load three immutable files/directories:

- `robot_model_path`: RBF-Safe DH JSON;
- `scene_path`: RBF-Safe AABB scene JSON; and
- `atlas_path`: a compatible saved Atlas.

`group_name` and `joint_names` are also required. The configured joint list
must exactly equal the MoveIt joint-variable order, and every finite MoveIt
position limit must match the DH limit within `1e-9`. The kinematics plugin
supports exactly one tip.

RBF-Safe cannot infer that a URDF collision model, base frame, and tip frame
are physically equivalent to the DH/capsule model. The operator must establish
and maintain that equivalence. Initialization or request processing fails on
any check RBF-Safe can perform; successful initialization is not an execution
certificate.

## Planning pipeline

Add the adapters to the pipeline vectors in processing order. Keep the
certified trajectory adapter last so it sees the final response:

```yaml
ompl:
  request_adapters:
    - rbfsafe_moveit/CertifiedStartStateAdapter
    - default_planning_request_adapters/ValidateWorkspaceBounds
    - default_planning_request_adapters/CheckStartStateBounds
    - default_planning_request_adapters/CheckStartStateCollision
  response_adapters:
    - default_planning_response_adapters/AddTimeOptimalParameterization
    - default_planning_response_adapters/ValidateSolution
    - rbfsafe_moveit/CertifiedTrajectoryAdapter

  robot_model_path: /absolute/path/to/robot.json
  scene_path: /absolute/path/to/scene.json
  atlas_path: /absolute/path/to/atlas
  group_name: arm
  joint_names: [joint_1, joint_2]
  tip_link: tool0
  maximum_audit_region_tests: 10000000
```

MoveIt passes the pipeline parameter namespace (`ompl` above) to each adapter.
Both RBF-Safe adapters therefore share the same immutable resource parameters.
If the audit is `PARTIAL`, `INVALID`, cancelled, over budget, malformed, or
identity-incompatible, the response trajectory is cleared and the error code
becomes `INVALID_MOTION_PLAN`.

## Kinematics plugin

Select the solver through the normal MoveIt kinematics configuration:

```yaml
robot_description_kinematics:
  arm:
    kinematics_solver: rbfsafe_moveit/SafeIkKinematicsPlugin
    kinematics_solver_timeout: 1.0
```

Provide RBF-Safe-specific parameters at node scope:

```yaml
rbfsafe:
  arm:
    robot_model_path: /absolute/path/to/robot.json
    scene_path: /absolute/path/to/scene.json
    atlas_path: /absolute/path/to/atlas
    group_name: arm
    joint_names: [joint_1, joint_2]
    tip_link: tool0
    position_tolerance: 0.0001
    orientation_tolerance: 0.001
    maximum_ik_iterations: 128
    maximum_region_attempts: 256
```

The plugin supports all single-pose `KinematicsBase` search overloads,
consistency limits, solution callbacks, and FK for the configured tip. A
timeout is checked before accepting the result; deterministic iteration and
region budgets provide the hard work bounds. Approximate IK is never returned.

## Deployment boundary

The scene used by MoveIt is not automatically converted into or compared with
the RBF-Safe AABB snapshot in v0.5. Updating the planning scene without loading
a matching Atlas makes the configured certificate stale even if MoveIt itself
continues planning. v0.6 is intended to add explicit scene versions,
invalidation, and local repair. Until then, deployment code must atomically
coordinate scene and Atlas selection and retain independent runtime safeguards.

# rbfsafe_moveit

`rbfsafe_moveit` is the optional ROS 2 Jazzy / MoveIt 2 integration for RBF-Safe. It is
deliberately built as a separate ament package so the RBF-Safe core and Python wheel keep
zero ROS dependencies.

The package exports four plugins. The gates are fail closed; the sampler is an
acceleration source whose output remains subject to the final gate:

- `CertifiedStartStateAdapter` accepts a request only when its start state is in the Atlas.
- `CertifiedTrajectoryAdapter` keeps a response only when `TrajectoryAuditor` reports
  `CERTIFIED`; partial, invalid, malformed, and incompatible responses are cleared.
- `CertifiedConstraintSamplerAllocator` draws only from the registered compatible
  Atlas and can bias proposals toward its certified roadmap.
- `SafeIkKinematicsPlugin` returns only pose-checked solutions with a
  `CertifiedConnectivity` Atlas route from the seed state.

The MoveIt joint variable order and finite position limits must exactly match the configured
DH model. The DH base and tool frames must also be physically aligned with the configured
MoveIt base and tip frames; v0.5 cannot infer or certify that semantic equivalence from URDF.
Consequently, the plugins do not issue `RuntimeExecutable` evidence.

See `config/rbfsafe_plugins.yaml` for the complete parameter layout. The kinematics plugin
reads `rbfsafe.<group_name>.*`; planning adapters read the namespace supplied by the planning
pipeline. All paths are required and the Atlas identity must match the loaded robot and scene.

Build after installing RBF-Safe and sourcing ROS 2 Jazzy:

```bash
source /opt/ros/jazzy/setup.bash
colcon build --packages-select rbfsafe_moveit \
  --cmake-args -DCMAKE_PREFIX_PATH=/path/to/rbfsafe/install
```

Add the kinematics plugin to `kinematics.yaml`:

```yaml
arm:
  kinematics_solver: rbfsafe_moveit/SafeIkKinematicsPlugin
```

Add both planning adapters to the corresponding request and response adapter chains. The
response adapter is the enforcement point for final planned trajectories; the request adapter
only validates the starting state.

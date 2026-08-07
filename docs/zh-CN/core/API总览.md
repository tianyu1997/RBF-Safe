# API 总览（v5.0）

> 英文原文：[API overview](../../core/api.md)

C++ 聚合头文件为：

```cpp
#include <rbfsafe/rbfsafe.h>
```

Python 包统一为：

```python
import rbfsafe
```

## 核心模型

- `Interval`、`Configuration`、`CspaceAabb`、`WorkspaceAabb`；
- `DhJoint`、`SerialRobotModel`、`Pose3d`、`GeometricJacobian`；
- `SceneObstacle`、`SceneSnapshot`；
- `Certificate`、`SafeRegion`、`EvidenceLevel`。

## 几何与分区

- `SerialRobotModel.forward_kinematics`；
- `end_effector_pose` 与 `end_effector_geometric_jacobian`；
- `compute_ifk_aa_link_envelope`；
- `LectTree`、`LectNodeKey`、`LectSnapshot`。

## Atlas

- `AtlasBuilder.build(robot, scene, samples, options)`；
- `SafeAtlas.contains`、`regions_at`、`nearest_region`；
- `connected`、`route`、`verify_compatible`；
- `save`、`load`。

## 应用层

- `TrajectoryAuditor`：连续轨迹覆盖审核；
- `HipacCorridorBuilder`：OBB/Portal 走廊；
- `SafeIkSolver`：区域内 Safe IK；
- `CertifiedSampler` 与 `CertifiedRoadmapBuilder`；
- `TrajOptRegionAdapter`、`ChompRegionAdapter`、`StompRegionAdapter`、`MpcRegionAdapter`；
- `RuntimeShield` 与 `LearningPolicySafetyGate`；
- `AtlasUpdater` 与 `AtlasVersionStore`。

## 结果与异常

C++ 调用先检查 `Result<T>`：

```cpp
auto result = atlas.route(q0, q1);
if (!result)
    return result.error();
```

Python 使用异常。`ValueError` 表示无效参数或维度，`OSError` 表示 I/O，`MemoryError` 表示资源限制；身份、格式、损坏、取消和内部错误对应 `RBFSafeError` 的具体子类。

所有加载接口都应使用显式资源上限；来自外部的 Atlas 必须调用 `verify_compatible`。

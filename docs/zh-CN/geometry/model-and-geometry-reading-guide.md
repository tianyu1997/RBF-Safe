# RBF-Safe：模型、几何与工作空间包络阅读指南

本文以当前版本代码为准，说明 RBF-Safe 如何从串联机器人模型、配置空间区域和障碍物场景，导出点运动学、保守端点包络、不同类型的工作空间连杆包络，以及区域安全结论。

核心主线是：

```text
SerialRobotModel + CspaceAabb + SceneSnapshot
                  ↓
端点 AABB 来源：IFK-AA（认证）或 CritSample（诊断）
                  ↓
每条连杆的 AABB / OBB / k-DOP / SupportHull
                  ↓
与 typed WorkspaceEnvelope 障碍物（可跨类型）进行保守分离检查
                  ↓
CertifiedFree 或 Undetermined
```

`CritSample` 生成的端点 AABB 不是认证来源；任何需要
`CertifiedRegion` 的路径必须使用 IFK-AA。

## 1. 推荐代码阅读顺序

以下顺序按“数据类型 → 数学对象 → 算法 → 认证语义 → 测试”展开。阅读完每一步再进入下一步。

### 第一部分：概念、类型与模型

1. `docs/zh-CN/geometry/运动学与雅可比矩阵.md`
2. `docs/envelope/workspace-envelopes.md`
3. `include/rbfsafe/modules/core.h`
4. `include/rbfsafe/modules/envelope.h`
5. `include/rbfsafe/modules/geometry.h`

目标：区分配置空间和工作空间；理解四种 workspace 包络、modified-DH 模型、`SceneObstacle` 的 typed envelope，以及各类型的公开接口。

### 第二部分：点运动学与基本包络形状

6. `tests/test_geometry.cpp` 的以下部分：
   - `Interval`、`WorkspaceEnvelope`、OBB、k-DOP 和 SupportHull 基础测试；
   - 模型加载、FK、末端位姿与 Jacobian 测试。
7. `src/geometry/model.cpp`：只读 `create`、`validate`、`forward_kinematics`、`end_effector_pose`、`end_effector_geometric_jacobian`。
8. `src/envelope/workspace_envelope.cpp`：先读 `WorkspaceObb`、`WorkspaceKdop`、`WorkspaceSupportHull` 的构造和 `support_point`，再读 `WorkspaceEnvelope::overlaps` 与 `distance_lower_bound`。

目标：理解单个配置如何变成各端点坐标；不同 workspace 几何如何通过 support mapping 统一处理。

### 第三部分：端点来源与连杆包络生成

9. `include/rbfsafe/modules/geometry.h`
10. `src/geometry/geometry.cpp`，依次阅读：
    - `validate_domain`、仿射标量和矩阵工具；
    - `compute_ifk_aa_endpoint_aabbs`；
    - `critical_candidates`、`capped_critical_candidates`、`compute_critical_sample_endpoint_aabbs`；
    - `typed_link_envelope`；
    - `compute_endpoint_aabbs`；
    - `compute_workspace_link_envelope`；
    - 两个验证器。
11. 回到 `tests/test_geometry.cpp`，阅读 endpoint source、四种 typed link envelope，以及 AABB/SupportHull 验证器对比的测试。

目标：明确哪条路径是认证路径，端点 AABB 如何生成，连杆如何被包装成 AABB、OBB、k-DOP 或 SupportHull。

### 第四部分：证书、Atlas 与变化影响

12. `include/rbfsafe/modules/geometry.h` 与 `src/geometry/certificate.cpp`
13. `include/rbfsafe/modules/atlas.h` 与 `src/atlas/atlas.cpp` 中的 `AtlasBuilder::build`
14. `include/rbfsafe/modules/atlas.h` 与 `src/atlas/scene_delta.cpp`
15. `tests/test_persistence.cpp`、`tests/test_dynamic.cpp`

目标：理解认证器虽然可用 typed envelope 做更紧的实时分离检查，但 `RegionValidation::envelope` 和 Atlas 兼容依赖仍保存每连杆的 enclosing AABB，以支持既有持久化和局部失效分析。

## 2. 三个空间与三类对象

### 配置空间（C-space）

配置是关节变量：

```cpp
using Configuration = std::vector<double>;
```

二自由度机器人的 `Configuration{0.2, -0.3}` 表示两个关节变量，而非三维位置。`CspaceAabb` 以每个关节的 `Interval` 表示一个 N 维轴对齐区域：

```cpp
CspaceAabb domain({{-0.2, 0.2}, {-0.3, 0.3}});
```

### 工作空间（workspace）

工作空间坐标是三维点：

```cpp
using WorkspacePoint = std::array<double, 3>;
```

障碍物和连杆包络都在该空间表达。现在场景障碍物不再固定为 AABB：

```cpp
struct SceneObstacle {
    std::string id;
    WorkspaceEnvelope bounds;
};
```

### 认证依赖（legacy AABB envelope）

`RegionValidation::envelope` 仍为：

```cpp
struct LinkEnvelope {
    std::vector<WorkspaceAabb> links;
};
```

这并不否定 typed envelope 验证；它表示 Atlas 证书依赖和既有持久化格式继续保留可移植的 per-link AABB 外包。

## 3. 机器人模型与 modified-DH

`SerialRobotModel` 拥有：

```cpp
std::vector<DhJoint> joints_;
std::vector<Interval> joint_limits_;
std::vector<double> link_radii_;
std::optional<DhJoint> tool_frame_;
```

使用 `SerialRobotModel::create` 创建模型。它会验证关节数、限制、半径、有限值和可选工具坐标系。

项目采用 modified-DH：

```text
T(i-1,i) = Rx(alpha_i) Tx(a_i) Rz(theta_i + q_i) Tz(d_i + q_i)
```

实际只有与关节类型对应的项会加入 `q_i`：

- 旋转关节：更新 `theta`；
- 移动关节：更新 `d`。

modified-DH 与 standard DH 的变换顺序不同，不能直接照搬其他库的 DH 参数。

## 4. 点运动学

### 正向运动学

`forward_kinematics(q)` 从单位矩阵开始累积每个关节变换：

```text
T(0,i) = T(0,1) T(1,2) ... T(i-1,i)
```

它返回基座、各连杆坐标系原点和可选工具原点。实现以行主序 4×4 矩阵保存变换，平移坐标位于索引 `3`、`7`、`11`。

```cpp
auto points = robot.forward_kinematics(q);
```

计算前会验证模型、配置维数、有限性和关节限制，所以结果是 `Result`。

### 末端位姿与 Jacobian

`end_effector_pose(q)` 返回位置及 `x,y,z,w` 四元数。

`end_effector_geometric_jacobian(q)` 返回 6×N Jacobian：前三行是线速度，后三行是角速度。对旋转关节：

```text
Jv_i = z_i × (p_e - p_i)
Jw_i = z_i
```

对移动关节：

```text
Jv_i = z_i
Jw_i = 0
```

测试使用中心差分近似 `dp/dq_i` 来独立检查 Jacobian。

## 5. 工作空间包络的统一类型

`WorkspaceEnvelope` 是 `std::variant` 风格的 tagged value：

```cpp
using WorkspaceEnvelopeValue = std::variant<
    WorkspaceAabb,
    WorkspaceObb,
    WorkspaceKdop,
    WorkspaceSupportHull>;
```

四种类型如下：

| 类型 | 几何描述 | 特点 |
|---|---|---|
| `WorkspaceAabb` | 三轴 lower/upper | 最简单、兼容旧 Atlas 依赖 |
| `WorkspaceObb` | 中心、正交基、三半宽 | 可沿连杆方向旋转，减少斜向空隙 |
| `WorkspaceKdop` | 成对投影区间 | 支持标准 6/14/18/26-DOP 或自定义方向 |
| `WorkspaceSupportHull` | 支持点凸包加球形半径 | 两端点加半径可表达 capsule，无需网格 |

所有类型提供：

```cpp
valid();
enclosing_aabb();
support_point(direction);
overlaps(other);
distance_lower_bound(other);
```

`overlaps()` 的语义是“不能排除相交”。只有在 support mapping 找到并验证分离方向时，才返回 `false`。搜索无法证明分离时，保守返回 `true`。这保证 `CertifiedFree` 不依赖未经证明的“无碰撞”。

## 6. 两种端点 AABB 来源

无论最终连杆选择哪种 workspace 类型，第一步都会为每条连杆生成一对端点 AABB：

```text
[link 0 proximal, link 0 distal, link 1 proximal, link 1 distal, ...]
```

由：

```cpp
compute_endpoint_aabbs(robot, domain, options)
```

生成。

### IFK-AA：认证来源

`EndpointAabbSource::IfkAa` 在完整 C-space box 上做区间正向运动学和仿射算术传播。它保留一阶变量相关性，对三角函数和乘法加入保守余项，并向外舍入。

结果：

```text
source = IfkAa
certified = true
evaluated_configurations = 0
```

这是区域认证唯一可用的端点来源。

### CritSample：非认证诊断来源

`EndpointAabbSource::CritSample` 为每个关节枚举：

```text
区间下界、区间上界、内部 k·π/2 候选点
```

宽度小于 `0.01` rad 的区间只取中点；组合数超过 8192 时，候选数最多的维度会缩减为 `{lower, center, upper}`。算法对候选笛卡尔积逐点运行 FK，并以样本端点扩张 AABB。

结果：

```text
source = CritSample
certified = false
evaluated_configurations > 0
```

原因是采样无法排除未取样的内部极值。因此它只能用于：

- 包络紧度对比；
- 性能与回归研究；
- 诊断与可视化；
- 与旧参考实现比对。

它不能支持 `CertifiedRegion` 证据，不能被用作 Atlas 认证路径。

## 7. 从端点 AABB 构造每连杆包络

`compute_workspace_link_envelope` 先根据 `endpoint_aabb_source` 得到近端和远端端点盒，再根据 `workspace_envelope_type` 调用 `typed_link_envelope`。

统一需要包含：

```text
两个端点盒的所有可能位置
加上 link_radius + obstacle_padding
```

各形状构造为：

| 输出类型 | 构造方法 |
|---|---|
| AABB | 两端点盒的轴对齐并集，再按半径扩张 |
| OBB | 用确定性的连杆对齐正交基，将两个端点盒的所有角点投影并向外扩张 |
| k-DOP | 投影所有端点盒角点到 k-DOP 方向，每个 slab 按半径扩张 |
| SupportHull | 取所有端点盒角点的凸包，再做半径对应的球形 Minkowski 扩张 |

如果端点盒来自 IFK-AA，上述四种形状都能作为保守连杆包络参与认证；如果端点盒来自 CritSample，形状可能更紧，但整个结果明确标记为非认证。

## 8. 两个 API 家族：不要混用认证语义

通用 API：

```cpp
compute_workspace_link_envelope(robot, domain, options)
```

尊重 `options.endpoint_aabb_source`。调用者必须检查：

```cpp
result.endpoint_bounds_certified
```

兼容/认证 API：

```cpp
compute_ifk_aa_link_envelope(robot, domain, options);
compute_ifk_aa_workspace_link_envelope(robot, domain, options);
```

它们都会强制：

```cpp
endpoint_aabb_source = IfkAa
```

即使调用者把选项设为 `CritSample`，这两个 API 也不会接受该不安全来源。

其中 `compute_ifk_aa_link_envelope` 保留旧 API，返回 `LinkEnvelope`，即每连杆 AABB；`compute_ifk_aa_workspace_link_envelope` 返回 typed `WorkspaceLinkEnvelope`。

## 9. 单点碰撞与区域验证

### 单点碰撞

`configuration_is_collision_free` 对单个配置运行 FK。每条真实连杆被表示为“两个端点加半径”的 `WorkspaceSupportHull`（即 capsule 形式），并与 typed scene obstacles 进行保守重叠检查。

这说明单点路径现在也不再强制把连杆简化为 AABB。

### 区域验证

旧验证器：

```cpp
IfkAaLinkAabbValidator
```

使用认证 IFK-AA 和 link AABB。

新验证器：

```cpp
IfkAaWorkspaceEnvelopeValidator
```

使用认证 IFK-AA 和选定的 AABB、OBB、k-DOP 或 SupportHull。它以 typed shape 与 typed obstacle 进行分离检查，因此可能减少 AABB 的假重叠。

两者都只在所有连杆包络与全部障碍物可证明分离时返回：

```text
CertifiedFree
```

否则返回：

```text
Undetermined
```

`Undetermined` 不等于已证明碰撞，只表示保守流程未能证明分离。

兼容性细节：`IfkAaWorkspaceEnvelopeValidator` 将每个 typed link envelope 的 `enclosing_aabb()` 写入 `RegionValidation::envelope`。因此证书、Atlas 持久化和 scene-delta 局部失效仍使用 AABB 依赖；实际认证时使用的算法名会区分 AABB、OBB、k-DOP 或 support hull。

## 10. 实验建议

按以下顺序做实验，每一步记录：包络类型、端点来源、`endpoint_bounds_certified`、包络体积或 enclosing AABB、验证状态和 clearance 下界。

1. 对同一个小 `CspaceAabb`，用 IFK-AA 分别生成 AABB、OBB、26-DOP 和 SupportHull；确认四者均为认证来源。
2. 在斜向细长连杆附近放一个小障碍物：AABB 可能 `Undetermined`，SupportHull 可能 `CertifiedFree`。
3. 保持形状不变，改成 CritSample；观察 `endpoint_bounds_certified == false`，并确认它不能进入认证器。
4. 增大 C-space box：观察 IFK-AA 端点盒变大、最终包络变松、认证可能变为 `Undetermined`。
5. 增大 `obstacle_padding`：观察所有类型都更难证明分离。
6. 对 k-DOP 尝试 6、14、18、26；注意仅支持这些标准 `k` 值。

## 11. 阅读验收问题

完成后应能回答：

1. C-space AABB 和 workspace envelope 分别描述什么？
2. `WorkspaceEnvelope` 的四个变体各适合哪种几何？
3. support mapping 为什么能统一实现保守分离测试？
4. `overlaps() == true` 为什么不代表已证明碰撞？
5. IFK-AA 为何可用于区域认证？
6. CritSample 为什么明确不可用于区域认证？
7. 为什么 CritSample 会选择 `k*pi/2` 候选点，又为什么仍不充分？
8. endpoint AABB 与最终 per-link envelope 是什么关系？
9. `compute_workspace_link_envelope` 与 `compute_ifk_aa_workspace_link_envelope` 的认证差别是什么？
10. 新验证器为何仍把 enclosing AABB 存进 `RegionValidation`？
11. 单点碰撞检测为什么使用 SupportHull capsule？
12. 为何 OBB/k-DOP/SupportHull 能减少 AABB 的假重叠，但不能放宽保守认证语义？

最重要的结论是：

> 当前版本已支持 workspace AABB、OBB、k-DOP 与 SupportHull。形状选择影响分离测试的紧度；端点来源决定能否认证。IFK-AA 的端点盒可支撑四种形状的区域认证，CritSample 只能生成非认证的诊断包络。Atlas 为兼容性仍持久化每连杆的 AABB 外包，而不是丢弃 typed 验证带来的更紧分离结果。

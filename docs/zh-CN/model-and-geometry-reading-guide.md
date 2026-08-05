# RBF-Safe 模型与几何源码阅读指南

本文面向具备基础 C++ 知识的读者，介绍 RBF-Safe 如何从串联机器人模型和关节配置，得到点运动学结果以及配置空间区域的保守安全结论。

核心问题是：

> 给定机器人关节角的一整块范围，RBF-Safe 如何证明该范围内所有机器人姿态的连杆包络都与场景障碍物分离？

## 1. 推荐阅读顺序

1. `docs/zh-CN/kinematics.md`
2. `include/rbfsafe/types.h`
3. `include/rbfsafe/model.h`
4. `tests/test_geometry.cpp`
5. `src/model.cpp`
6. `include/rbfsafe/geometry.h`
7. `src/geometry.cpp`

阅读时沿两条调用链理解。

单点检查：

```text
机器人模型 + 单个配置 q
        ↓
modified-DH 正向运动学
        ↓
各连杆端点的工作空间位置
        ↓
单点连杆 AABB 与障碍物 AABB 比较
```

区域认证：

```text
机器人模型 + C-space 区域
        ↓
区间/仿射算术传播
        ↓
每条连杆在整个区域内的保守 workspace AABB
        ↓
与所有障碍物 AABB 比较
        ↓
CertifiedFree 或 Undetermined
```

检查一个配置安全，不等于证明一整个连续配置区域安全。

## 2. 配置空间与工作空间

### 2.1 配置空间

配置空间（C-space）的坐标是机器人关节变量。项目中：

```cpp
using Configuration = std::vector<double>;
```

对于二自由度机器人：

```cpp
Configuration q{0.2, -0.3};
```

表示 `q0 = 0.2`、`q1 = -0.3`，而不是末端执行器的笛卡尔坐标。

`Interval` 表示一个一维闭区间：

```cpp
Interval interval{-0.5, 0.5};
```

它提供有效性、宽度、中心、包含和重叠判断。`CspaceAabb` 为每个关节保存一个 `Interval`：

```cpp
CspaceAabb domain({
    {-0.2, 0.2},
    {-0.3, 0.3},
});
```

即：

```text
q0 ∈ [-0.2, 0.2]
q1 ∈ [-0.3, 0.3]
```

主要接口包括：

```cpp
domain.dimension();
domain.valid();
domain.contains(configuration);
domain.overlaps(other);
domain.volume();
domain.center();
```

### 2.2 工作空间

工作空间是机器人实际运动所在的三维空间。`WorkspaceAabb` 表示三维轴对齐盒：

```cpp
struct WorkspaceAabb {
    std::array<double, 3> lower;
    std::array<double, 3> upper;
};
```

例如：

```cpp
WorkspaceAabb obstacle{
    {0.4, -0.2, -0.2},
    {1.2,  0.2,  0.2},
};
```

表示 `x`、`y`、`z` 三个方向上的闭区间。它提供有效性、重叠判断和盒间距离下界。

二者不能混淆：

| 配置空间 | 工作空间 |
|---|---|
| 坐标是关节变量 | 坐标是三维位置 |
| `CspaceAabb` | `WorkspaceAabb` |
| 描述一组机器人姿态 | 描述连杆或障碍物占据范围 |

## 3. 串联机器人模型

`SerialRobotModel` 主要拥有：

```cpp
std::string name_;
std::vector<DhJoint> joints_;
std::vector<Interval> joint_limits_;
std::vector<double> link_radii_;
std::optional<DhJoint> tool_frame_;
```

它们分别表示名称、modified-DH 关节、关节限制、连杆半径和可选工具坐标系。

二连杆模型可以这样创建：

```cpp
auto robot = rbfsafe::SerialRobotModel::create(
    "planar-2r",
    {
        {0.0, 1.0, 0.0, 0.0, rbfsafe::JointType::Revolute},
        {0.0, 1.0, 0.0, 0.0, rbfsafe::JointType::Revolute},
    },
    {{-1.5, 1.5}, {-1.5, 1.5}},
    {0.05, 0.05});
```

推荐使用 `create()`，因为它在返回模型之前执行 `validate()`。验证内容包括：

- 名称非空；
- 关节数量为 1～64；
- 关节、限制和连杆半径的数量一致；
- 参数为有限数；
- 关节区间有效；
- 连杆半径非负；
- 可选工具帧有效。

## 4. modified-DH

项目使用 modified-DH 变换：

```text
T(i-1,i) = Rx(alpha_i) Tx(a_i) Rz(theta_i + q_i) Tz(d_i + q_i)
```

但只有与关节类型相应的项接收 `q_i`：

- 旋转关节：`theta_i + q_i`，`d_i` 不变；
- 移动关节：`d_i + q_i`，`theta_i` 不变。

`DhJoint` 为：

```cpp
struct DhJoint {
    double alpha;
    double a;
    double d;
    double theta;
    JointType type;
};
```

modified-DH 与 standard DH 的变换顺序不同，其他软件中的参数不能未经核对直接复制到本项目。

刚体位姿用 4×4 齐次矩阵表示。实现采用长度为 16 的行主序数组，平移位于索引 `3`、`7`、`11`。

## 5. 正向运动学与末端位姿

`forward_kinematics(q)` 从单位变换开始，逐关节右乘 modified-DH 变换并记录坐标系原点：

```text
T(0,i) = T(0,1) T(1,2) ... T(i-1,i)
```

返回值包含基座原点、各连杆坐标系原点以及可选工具原点：

```cpp
Result<std::vector<std::array<double, 3>>>
forward_kinematics(std::span<const double> configuration) const;
```

二关节且无工具帧时，通常返回三个点。计算前会验证模型、配置维数、有限性和关节限制。

`end_effector_pose(q)` 返回：

```cpp
struct Pose3d {
    std::array<double, 3> position;
    std::array<double, 4> orientation; // x, y, z, w
};
```

其位置应与 `forward_kinematics(q).back()` 一致。

## 6. 几何 Jacobian

对于 N 自由度机器人，几何 Jacobian 为 6×N：

```text
[vx vy vz wx wy wz]^T = J(q) q_dot
```

前 3 行映射末端线速度，后 3 行映射末端角速度。

对旋转关节 `i`：

```text
Jv_i = z_i × (p_e - p_i)
Jw_i = z_i
```

对移动关节：

```text
Jv_i = z_i
Jw_i = 0
```

`tests/test_geometry.cpp` 使用中心差分验证解析 Jacobian 的线速度部分：

```text
dp/dq_i ≈ [p(q_i + h) - p(q_i - h)] / (2h)
```

这是一种重要测试方法：用独立的数值近似检查解析实现。

## 7. 单点碰撞检查

```cpp
Result<bool> configuration_is_collision_free(
    const SerialRobotModel& robot,
    const SceneSnapshot& scene,
    std::span<const double> configuration,
    double obstacle_padding = 0.0);
```

函数先计算一个配置的 FK，再用每条连杆的两个端点构造工作空间 AABB，并向各轴扩大 `link_radius + obstacle_padding`。如果任何连杆 AABB 与场景障碍物 AABB 重叠，返回 `false`。

这是单点结果，而且 AABB 相交本身也是保守条件。它不能证明一整块配置区域安全。

## 8. 区域连杆包络

区域验证使用：

```cpp
Result<LinkEnvelope> compute_ifk_aa_link_envelope(
    const SerialRobotModel& robot,
    const CspaceAabb& domain,
    const EnvelopeOptions& options = {});
```

其中：

```cpp
struct LinkEnvelope {
    std::vector<WorkspaceAabb> links;
};
```

第 `i` 个 AABB 必须覆盖第 `i` 条连杆在整个配置区域中的所有可能位置：

```text
对任意 q ∈ domain：Link_i(q) ⊆ envelope.links[i]
```

包络允许偏大，但不能遗漏真实位置。

## 9. IFK-AA 与保守估计

实现中的 `AffineScalar` 记录中心、一阶相关项和保守余项，可近似写成：

```text
x = x0 + a0 e0 + a1 e1 + ... + remainder,
其中 ei ∈ [-1, 1]
```

关节区间 `[l, u]` 写为：

```text
m = (l + u) / 2
delta = (u - l) / 2
q = m + delta e
```

旋转关节的 `sin` 和 `cos` 在区间中心进行一阶展开，并加入随 `delta²` 增长的非线性余项。区间越宽，余项和最终 workspace 包络通常越大；区域切分后，包络通常会收紧。

仿射算术还会保留共享变量的一阶相关性，从而避免一部分普通区间算术的过度膨胀。乘法、三角函数和浮点计算仍需要保守余项。

实现使用 `std::nextafter` 将下界向负无穷、上界向正无穷扩展，防止浮点舍入把理论包络错误地缩小。

## 10. 区域验证语义

默认验证器为：

```cpp
IfkAaLinkAabbValidator
```

它先计算每连杆 workspace AABB，再与所有场景障碍物 AABB 比较。结果只有：

```cpp
enum class ValidationDisposition {
    CertifiedFree,
    Undetermined,
};
```

`CertifiedFree` 表示所有保守连杆包络都与所有障碍物分离，因此整个配置区域可以获得区域安全结论。

`Undetermined` 表示至少一个保守包络与障碍物 AABB 重叠，当前算法无法证明整个区域安全。它不表示已经证明发生碰撞。

```text
包络与障碍物分离
    → 真实连杆必然分离
    → CertifiedFree

包络与障碍物重叠
    → 真实连杆可能碰撞，也可能只是包络过松
    → Undetermined
```

`clearance_lower_bound` 是所有连杆包络与障碍物之间距离的保守下界。空场景中实现将其报告为 `0.0`。

`Result` 失败与 `Undetermined` 不同：前者表示输入或计算无效，后者表示计算正常完成但没有获得证明。

## 11. 当前 workspace 包络的几何类型

当前公开 `LinkEnvelope` 固定保存：

```cpp
std::vector<WorkspaceAabb> links;
```

因此默认 IFK-AA、Zonotope/Taylor 高阶传播、Atlas 依赖、动态失效检查和连续占用模块，最终保存或消费的每连杆 workspace 包络都是 AABB。

项目中的 OBB、Portal、Zonotope 和 Taylor 主要是配置空间区域表示：

- `CspaceObb` 是旋转的配置空间单元；
- `Portal` 表示配置空间中相邻凸区域的交集；
- `CspaceZonotope` 和 `CspaceTaylorRegion` 表示带相关性的配置空间集合；
- OBB 验证目前先取其配置空间 AABB 外包，再调用 IFK-AA + LinkIAABB；
- Zonotope/Taylor 可以在传播阶段保留更多配置变量相关性，但输出仍是每连杆 `WorkspaceAabb`。

当前仓库没有 workspace OBB、KDOP 或 SupportHull 连杆包络后端。`docs/migration-map.md` 将 `KDOP` 和 `SupportHull` 明确列为 deferred。

## 12. 如何阅读几何测试

`tests/test_geometry.cpp` 可分为以下契约：

1. `Interval` 的有效性、宽度、包含和重叠；
2. 模型验证、JSON 加载和稳定摘要；
3. FK 与末端位姿一致性；
4. 解析 Jacobian 与数值差分一致；
5. 随机采样的所有 FK 端点均落在区域包络内；
6. 空场景得到 `CertifiedFree`；
7. 包络与障碍物重叠时得到 `Undetermined`；
8. 不完整包络不能形成区域证书。

随机采样测试只能发现实现回归，不是无限连续区域的证明。证明依赖保守传播算法及其误差界。

## 13. 实践程序

下面的核心实验依次打印 FK，并扩大配置区域观察验证状态和包络变化：

```cpp
#include <rbfsafe/rbfsafe.h>

#include <iomanip>
#include <iostream>
#include <vector>

const char* name(rbfsafe::ValidationDisposition value) {
    using rbfsafe::ValidationDisposition;
    return value == ValidationDisposition::CertifiedFree
               ? "CertifiedFree"
               : "Undetermined";
}

int main() {
    using namespace rbfsafe;

    auto created = SerialRobotModel::create(
        "planar-2r",
        {{0.0, 1.0, 0.0, 0.0, JointType::Revolute},
         {0.0, 1.0, 0.0, 0.0, JointType::Revolute}},
        {{-1.5, 1.5}, {-1.5, 1.5}},
        {0.05, 0.05});
    if (!created) {
        std::cerr << created.error().describe() << '\n';
        return 1;
    }
    const auto& robot = created.value();

    const std::vector<Configuration> configurations{
        {0.0, 0.0}, {0.2, -0.3}, {0.6, 0.2}, {-0.6, -0.2}};
    for (const auto& q : configurations) {
        auto fk = robot.forward_kinematics(q);
        if (!fk) {
            std::cerr << fk.error().describe() << '\n';
            continue;
        }
        std::cout << "q=(" << q[0] << ", " << q[1] << ")\n";
        for (std::size_t i = 0; i < fk.value().size(); ++i) {
            const auto& p = fk.value()[i];
            std::cout << "  p[" << i << "]=("
                      << p[0] << ", " << p[1] << ", " << p[2] << ")\n";
        }
    }

    const SceneSnapshot scene(
        {{{"nearby-box"}, {{1.55, 0.35, -0.20}, {1.85, 0.65, 0.20}}}},
        "geometry-learning-v1");
    IfkAaLinkAabbValidator validator;

    for (double half_width : {0.02, 0.05, 0.10, 0.20, 0.40, 0.80}) {
        CspaceAabb domain({{-half_width, half_width},
                            {-half_width, half_width}});
        auto validation = validator.validate(robot, scene, domain);
        if (!validation) {
            std::cerr << validation.error().describe() << '\n';
            continue;
        }
        std::cout << "half_width=" << half_width
                  << " result=" << name(validation.value().disposition)
                  << " clearance=" << validation.value().clearance_lower_bound
                  << '\n';
        for (std::size_t i = 0; i < validation.value().envelope.links.size(); ++i) {
            const auto& box = validation.value().envelope.links[i];
            std::cout << "  link[" << i << "] lower=("
                      << box.lower[0] << ", " << box.lower[1] << ", " << box.lower[2]
                      << ") upper=(" << box.upper[0] << ", " << box.upper[1] << ", "
                      << box.upper[2] << ")\n";
        }
    }
}
```

实验应重点观察：

- 区域扩大时每连杆 workspace AABB 如何变化；
- `clearance_lower_bound` 是否下降；
- 状态是否从 `CertifiedFree` 变成 `Undetermined`；
- 中心点无碰撞时，大区域是否仍可能无法认证；
- 增大 `obstacle_padding` 是否使认证更严格。

## 14. 阅读验收问题

完成本阶段后，应能回答：

1. `Configuration`、`CspaceAabb` 和 `WorkspaceAabb` 分别表示什么？
2. modified-DH 中旋转关节和移动关节分别修改哪个参数？
3. FK 如何通过矩阵连乘得到端点？
4. Jacobian 的前 3 行和后 3 行分别表示什么？
5. 中心差分如何验证解析 Jacobian？
6. 单点无碰撞为什么不能证明区域安全？
7. `LinkEnvelope` 必须满足什么包含关系？
8. 配置区间增大为何通常使包络变松？
9. 为什么浮点上下界需要向外舍入？
10. `CertifiedFree` 与 `Undetermined` 的严格含义是什么？
11. 为什么切分区域可能使部分子区域获得认证？
12. 项目中的 C-space OBB 与 workspace link AABB 有什么区别？

核心结论是：

> RBF-Safe 不依靠采样猜测连续区域安全，而是计算覆盖该配置区域内所有姿态的保守连杆 workspace AABB；只有这些包络与场景障碍物明确分离时，才返回 `CertifiedFree`。

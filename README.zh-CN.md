# RBF-Safe

[English](README.md) | 简体中文

RBF-Safe 是一个 C++20 与 Python 开源库，用于在机器人配置空间中生成、保存、查询和复用保守的几何安全证书。当前版本为 **4.7.0**，支持串联 modified-DH 机器人、工作空间 AABB 障碍物、确定性 LECT 分区、认证 AABB/OBB 区域、HiPaC 走廊、连通性查询、轨迹审核、Safe IK、规划与优化适配器，以及动态安全记忆。

项目采用 MIT 许可证。核心库不依赖 RapidBoxForest 的源码、构建产物或旧缓存格式。

## 安全语义

RBF-Safe 将“几何计算结果”和“可执行授权”严格分开：

- `Unknown`：没有形成可复用安全结论；
- `PointChecked`：仅检查了一个配置点；
- `CertifiedRegion`：整个配置空间区域已由保守算法认证；
- `CertifiedConnectivity`：区域之间存在经过认证的连通关系；
- `RuntimeExecutable`：仅在精确命令、身份、时间窗、审查配置和控制器确认全部闭合时使用。

采样、可视化、Jacobian、外部碰撞检查或成功的规划器输出都不会自动升级为区域证书。请先阅读[安全模型](docs/zh-CN/安全模型.md)。

## 主要能力

- modified-DH 点 FK、末端位姿、解析 6×N 几何 Jacobian；
- IFK-AA + LinkIAABB 保守连杆包络和区域验证；
- 公共 LECT、AABB Atlas、OBB Atlas、Portal、TrajectoryTube、Zonotope 与一阶 Taylor 区域；
- Atlas 保存、加载、身份校验、区域查询、确定性路由和连通分量；
- OBB/Portal/HiPaC 认证走廊；
- 连续分段线性轨迹的解析覆盖审核；
- Safe IK、OMPL、MoveIt 2、TrajOpt、CHOMP、STOMP 与 MPC 消费接口；
- VLA/学习策略安全门、动作 shield、校准与反馈记录；
- 场景差分、证书失效、局部修复和不可变 Atlas 版本历史；
- 持久安全记忆、多机器人占用、协调预留、身份信任、透明日志和可验证来源。

## 安装

### Python

```bash
python -m pip install rbfsafe
```

从源码构建：

```bash
python -m pip install .
```

### C++

```bash
cmake -S . -B build \
  -DRBFSAFE_BUILD_TESTS=ON \
  -DRBFSAFE_BUILD_EXAMPLES=ON \
  -DRBFSAFE_BUILD_TOOLS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix install
```

下游项目可使用：

```cmake
find_package(RBFSafe 4.7 REQUIRED)
target_link_libraries(my_target PRIVATE RBFSafe::rbfsafe)
```

完整说明见[安装指南](docs/zh-CN/安装指南.md)。

## Python 快速开始

```python
from pathlib import Path
import rbfsafe

robot = rbfsafe.SerialRobotModel.from_json("data/planar_2r.json")
scene = rbfsafe.SceneSnapshot.from_json("data/empty_scene.json")

options = rbfsafe.BuildOptions()
options.maximum_depth = 24
options.maximum_nodes = 1_000_000
options.threads = 1

result = rbfsafe.AtlasBuilder().build(
    robot,
    scene,
    [[0.0, 0.0], [1.0, -1.0]],
    options,
)
atlas = result.atlas
atlas.save(Path("atlas"))

loaded = rbfsafe.SafeAtlas.load(Path("atlas"))
loaded.verify_compatible(robot, scene)
print(loaded.contains([0.0, 0.0]))
print(loaded.connected([0.0, 0.0], [1.0, -1.0]))

jacobian = robot.end_effector_geometric_jacobian([0.0, 0.0])
print(jacobian.rows, jacobian.columns)
```

## C++ 快速开始

```cpp
#include <rbfsafe/rbfsafe.h>

#include <iostream>

int main() {
    auto robot = rbfsafe::SerialRobotModel::from_json("data/planar_2r.json");
    auto scene = rbfsafe::SceneSnapshot::from_json("data/empty_scene.json");
    if (!robot || !scene)
        return 1;

    rbfsafe::BuildOptions options;
    auto built = rbfsafe::AtlasBuilder{}.build(
        robot.value(), scene.value(), {{0.0, 0.0}, {1.0, -1.0}}, options);
    if (!built)
        return 1;

    const auto& atlas = built.value().atlas;
    std::cout << atlas.contains(rbfsafe::Configuration{0.0, 0.0}) << '\n';
    return 0;
}
```

## 命令行检查与可视化

```bash
rbfsafe-inspect atlas --query 0.0 0.0
rbfsafe-inspect atlas --plot slice.png --dims 0 1
```

图形仅展示已经保存的证书，不能独立证明碰撞安全。

## 文档

- [中文文档总览](docs/zh-CN/README.md)
- [项目阅读路线](docs/zh-CN/项目阅读路线.md)
- [完整英文文档索引](docs/README.md)
- [入门教程](docs/zh-CN/快速开始.md)
- [API 总览](docs/zh-CN/API总览.md)
- [体系结构](docs/zh-CN/体系结构.md)
- [运动学与 Jacobian](docs/zh-CN/运动学与雅可比矩阵.md)
- [轨迹审核](docs/zh-CN/轨迹审核.md)
- [动态更新](docs/zh-CN/动态更新.md)
- [中文变更日志](CHANGELOG.zh-CN.md)

`docs/zh-CN/` 提供精选核心指南；详细 schema、历史格式和审计协议保留英文规范。公共标识符、JSON 字段、错误码和命令行参数不会翻译。

## 验证状态

4.7.0 发布门禁覆盖 Linux/Windows、GCC/Clang/MSVC、ASan/UBSan、Python wheel、独立 CMake consumer、OMPL 与 MoveIt。项目范围清单将原始工程计划映射到 18 项需求和 63 条公共 API、行为测试及文档证据。

## 贡献

提交改动前请阅读 [CONTRIBUTING.zh-CN.md](CONTRIBUTING.zh-CN.md)、[SECURITY.zh-CN.md](SECURITY.zh-CN.md) 和[发布流程中文版](docs/zh-CN/发布流程.md)。修改中文索引所列核心指南时，应同步更新对应的中文文件。

## 许可证

RBF-Safe 使用 [MIT License](LICENSE)。第三方组件与权属信息见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 和[来源说明](docs/zh-CN/来源说明.md)。

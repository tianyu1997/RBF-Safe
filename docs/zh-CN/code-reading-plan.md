# RBF-Safe 源码阅读方案与计划

本文面向只有基础 C++ 经验、希望深入理解 RBF-Safe 的读者。阅读不按目录顺序进行，而是围绕以下核心调用链展开：

```text
机器人模型 → 几何安全验证 → 配置空间划分 → 安全区域证书
          → SafeAtlas 查询 → 轨迹审核
```

先理解这条主线，再扩展到 Safe IK、规划、动态更新、运行时 Shield，以及身份、信任和审计模块。

## 1. 项目概览

RBF-Safe 是一个 C++20 / Python 机器人安全库。其核心工作包括：

1. 建立机器人模型和障碍物场景；
2. 在机器人的配置空间中划分区域；
3. 用保守几何算法证明某些区域不会碰撞；
4. 将安全区域组织成 `SafeAtlas`；
5. 查询配置是否安全以及配置之间是否认证连通；
6. 审核连续轨迹是否被认证区域覆盖；
7. 在此基础上支持 Safe IK、规划、动态更新和运行时安全控制。

核心依赖关系可以简化为：

```text
基础类型与机器人模型
        ↓
几何包络与碰撞验证
        ↓
LECT 配置空间划分树
        ↓
Certificate 安全证书
        ↓
SafeAtlas 安全区域集合
        ↓
轨迹审核 / Safe IK / 规划 / Shield
```

身份、信任、远程传输、审计日志、部署和多机器人协调属于后续系统安全层，不宜作为第一阶段入口。

## 2. 通用阅读方法

每个模块按照以下顺序阅读：

1. 文档：模块解决什么问题；
2. 示例：外部用户如何调用；
3. 公开头文件：模块提供哪些类型和接口；
4. 单元测试：接口应表现出什么行为；
5. `.cpp` 实现：功能具体如何完成；
6. 修改输入或增加测试；
7. 总结数据如何流入、如何流出以及何时失败。

为每个重要类制作一张记录卡：

```text
类名：
责任：
输入：
输出：
核心成员：
保持的不变量：
可能失败的情况：
被谁调用：
调用了谁：
```

为重要函数记录：

```text
函数：
前置条件：
主要步骤：
成功结果：
错误结果：
是否修改对象：
```

## 3. 第一阶段：最低限度补足 C++（第 1 周）

### 第 1 天：项目结构与编译模型

阅读：

- `CMakeLists.txt`
- `examples/quickstart.cpp`
- `include/rbfsafe/rbfsafe.h`

理解 `.h` 与 `.cpp`、`#include`、命名空间、CMake target、库与可执行程序。CMake 首次只需关注构建选项、`rbfsafe_geometry`、`rbfsafe_lect`、`rbfsafe_atlas`、`rbfsafe_quickstart` 和测试入口。

### 第 2 天：值、引用和对象

重点学习：

- `const`
- `const T&`
- `T*`
- 栈对象
- `std::move`
- 对象生命周期
- 成员函数末尾的 `const`

在 `examples/quickstart.cpp` 中辨认：

```cpp
const SceneSnapshot scene(...);
robot.value();
built.value().atlas;
const auto& atlas = built.value().atlas;
```

对每一处代码回答：是否复制、由谁拥有、是否可以修改、何时失效。

### 第 3 天：项目常用标准库

重点学习 `std::vector`、`std::string`、`std::span`、`std::optional`、`std::filesystem::path`、固定宽度整数、范围 `for` 和 lambda。特别理解 `std::span<const double>` 只借用连续数据而不拥有或修改数据。

### 第 4 天：错误处理

精读 `include/rbfsafe/result.h` 和 quickstart 中的错误分支。理解 `Result<T>`、`Result<void>`、`StatusCode`、`error()` 和 `value()`，以及为什么读取成功值前必须检查结果。

### 第 5～7 天：类、枚举和现代 C++ 接口

精读 `include/rbfsafe/types.h` 与 `include/rbfsafe/model.h`。学习 `struct`、`class`、访问控制、构造函数、`enum class`、静态工厂函数、默认参数、封装和对象不变量。

阶段练习：

1. 给 `quickstart.cpp` 每行添加中文解释；
2. 修改种子配置；
3. 增加障碍物；
4. 打印自由度与配置空间；
5. 故意提供错误参数并观察 `Result`。

## 4. 第二阶段：读通核心主链（第 2～4 周）

### 第 2 周：模型与几何

阅读顺序：

1. `docs/zh-CN/kinematics.md`
2. `include/rbfsafe/types.h`
3. `include/rbfsafe/model.h`
4. `tests/test_geometry.cpp`
5. `src/model.cpp`
6. `include/rbfsafe/geometry.h`
7. `src/geometry.cpp`

重点理解配置空间、工作空间、modified-DH、正向运动学、Jacobian、连杆包络、保守估计和区域碰撞验证。

实践：创建二连杆机器人，打印若干配置的正向运动学结果；构造配置空间盒并验证，逐渐扩大盒子观察结果变化。

### 第 3 周：LECT 划分与安全证书

阅读顺序：

1. `include/rbfsafe/lect.h`
2. `tests/test_lect.cpp`
3. `src/lect.cpp`
4. `include/rbfsafe/certificate.h`
5. `src/certificate.cpp`

重点追踪根节点创建、节点 key、切分维度、`split()`、`locate()`、`overlap_leaves()`，以及 `LectTree` 与 `LectSnapshot` 的差别。

理解 `Unknown`、`PointChecked`、`CertifiedRegion`、`CertifiedConnectivity` 和 `RuntimeExecutable`，以及几何检查成功为何不等同于运行授权。

实践：手工绘制二维分区树，模拟切分、定位和区域重叠查询，并在 `test_lect.cpp` 中增加测试。

### 第 4 周：Atlas 构建、查询和轨迹审核

阅读顺序：

1. `include/rbfsafe/atlas.h`
2. `tests/test_atlas.cpp`
3. `src/atlas.cpp` 中的 `AtlasBuilder::build`
4. `SafeAtlas::contains`
5. `SafeAtlas::connected`
6. `include/rbfsafe/trajectory.h`
7. `src/trajectory.cpp`
8. `tests/test_trajectory.cpp`

将 `AtlasBuilder::build` 分为以下阶段理解：

```text
验证输入 → 建立 LECT → 处理种子 → 验证候选区域
        → 必要时继续切分 → 收集认证区域
        → 建立邻接关系 → 计算连通分量 → 构造 SafeAtlas
```

实践：扩展 quickstart，打印构建统计、区域数量、`contains()`、`connected()`、轨迹覆盖率和未认证区间；加入障碍物后比较结果。

## 5. 第三阶段：按方向深入（第 5～7 周）

### 路线 A：运动规划与控制安全

```text
Safe IK → Planning → OMPL adapter → Runtime Shield → Policy safety
```

依次阅读对应文档、公开头文件、测试和实现。适合研究机器人规划、IK、学习策略安全和运行时干预。

### 路线 B：动态环境

```text
SceneDelta → 证书失效 → 局部 LECT 修复 → Atlas 版本历史
```

重点理解场景变化如何影响旧证书、为何可以局部修复、版本之间如何关联，以及哪些区域可以继承。

### 路线 C：区域、走廊和优化

```text
AABB Atlas → OBB / Portal → HiPaC Corridor → Optimization constraints
```

适合偏计算几何与轨迹优化的方向。

### 路线 D：系统安全与审计

```text
memory → trust → remote → identity → deployment
       → execution → transparency → witness
```

重点包括哈希身份、HMAC、Ed25519、不可变记录、父子链、回滚防护、信任轮换、持久化格式和 fail-closed 设计。建议完成核心几何主线后再学习。

## 6. 八周时间表

| 周次 | 主题 | 建议投入 | 阶段产出 |
|---|---|---:|---|
| 1 | 项目结构与必要 C++ | 7～10 小时 | 注释版 quickstart、C++ 语法清单 |
| 2 | 模型、FK、几何验证 | 8～12 小时 | 几何调用图、验证实验程序 |
| 3 | LECT 与证书 | 8～10 小时 | 手绘分区树、新增 LECT 测试 |
| 4 | Atlas 和轨迹审核 | 10～12 小时 | 核心调用链说明、修改版示例 |
| 5 | 持久化与动态更新 | 8～10 小时 | Atlas 文件结构图、更新流程图 |
| 6 | Safe IK 与规划 | 8～12 小时 | Safe IK 示例、规划模块总结 |
| 7 | Shield 或走廊优化 | 8～12 小时 | 选定方向的小型实验 |
| 8 | 总结与源码改动 | 约 10 小时 | 模块图、阅读报告、小型改动 |

每天可采用 60～90 分钟节奏：复习 10 分钟，文档或头文件 15 分钟，测试 20 分钟，实现 25 分钟，实验和总结各 10 分钟。

## 7. 构建、测试与调试

初期只启用核心、示例、工具和测试：

```powershell
cmake -S . -B build `
  -DRBFSAFE_BUILD_TESTS=ON `
  -DRBFSAFE_BUILD_EXAMPLES=ON `
  -DRBFSAFE_BUILD_TOOLS=ON `
  -DRBFSAFE_BUILD_PYTHON=OFF `
  -DRBFSAFE_BUILD_OMPL=OFF

cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

只运行核心测试：

```powershell
ctest --test-dir build -C Debug -R "geometry|lect|atlas|trajectory" --output-on-failure
```

推荐断点：

- `SerialRobotModel::create`
- `SerialRobotModel::forward_kinematics`
- `IfkAaLinkAabbValidator::validate`
- `LectTree::split`
- `AtlasBuilder::build`
- `SafeAtlas::contains`
- `TrajectoryAuditor::audit`

## 8. 完成核心阶段的检查问题

完成核心阶段后，应能不看源码回答：

1. RBF-Safe 证明的“安全”具体是什么？
2. 点检查和区域认证为何不同？
3. 如何把关节角区域映射为连杆工作空间包络？
4. 算法的保守性来自哪里？
5. LECT 为什么需要递归切分？
6. 种子配置怎样影响 Atlas 构建？
7. 安全区域如何形成连通图？
8. 为什么两个安全点不一定认证连通？
9. 为什么轨迹不能只检查采样点？
10. `RuntimeExecutable` 为什么不能由普通几何结果直接得到？

最终形成一篇阅读报告，至少包含项目目标、核心数据类型、Atlas 构建调用链、安全证据层级、查询与轨迹审核、保守性的来源和仍未解决的问题。

# OMPL 适配器（v4.7）

> 英文原文：[OMPL adapter](../ompl-adapter.md)

可选目标 `RBFSafe::ompl` 面向 OMPL 实向量状态空间，核心库和 Python wheel 不依赖 OMPL。

## 低层接口

- 状态有效性：状态必须位于兼容 Atlas 的认证区域；
- 连续运动有效性：整条插值段必须被认证区域并集覆盖；
- 认证采样器：从区域内部采样，不从未知空间补采；
- 空间边界：由机器人的关节限制建立。

## 高层规划

有预算的 helper 配置上游 OMPL 的 RRT、RRT*、PRM 和 BIT*。规划器返回路径后，RBF-Safe 会独立审核连续覆盖；规划成功但审核失败时不会返回认证路径。

OMPL 的碰撞检查回调、离散 motion validator 或规划器近似解不能替代区域证书。适配器默认失败关闭，并限制状态维度、采样次数、规划时间/工作量和输出 waypoint 数。

构建和测试命令、支持的 OMPL 版本以及 Windows/Linux 查找规则以英文原文为准。

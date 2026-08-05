# MoveIt 2 集成（v4.7）

> 英文原文：[MoveIt 2 integration](../moveit2.md)

MoveIt 2 插件位于 `plugins/moveit2/rbfsafe_moveit`，面向 ROS 2 Jazzy/MoveIt 2.12.x，与核心构建及 Python wheel 分离。

## 插件

| 插件 | 作用 |
|---|---|
| `CertifiedStartStateAdapter` | 拒绝不在 Atlas 中的起始状态 |
| 规划响应适配器 | 独立审核 MoveIt 返回的轨迹 |
| 约束采样器 | 从认证区域生成关节状态 |
| Safe IK `KinematicsBase` | 使用 RBF-Safe IK 和连通证据 |

插件通过参数加载机器人、场景和 Atlas，并在初始化时执行身份兼容性检查。文件丢失、参数错误、维度不符、未认证起点、部分轨迹或无连接 IK 都会失败关闭。

RBF-Safe 不替代 MoveIt 的控制器管理、规划场景监视器或时间参数化。MoveIt 碰撞环境和 RBF-Safe 场景必须由部署方保持一致；插件不能自行证明传感器场景的新鲜度。

请在已 source 的 Jazzy 工作空间中使用 `colcon build`，并运行 pluginlib 加载测试确认四个类可发现、可构造。

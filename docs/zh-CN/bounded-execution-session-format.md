# 有界执行会话

> 英文原文：[Bounded execution sessions](../bounded-execution-session-format.md)

`BoundedExecutionSession` 把精确认证命令序列重新绑定到经审查部署配置、控制器/监视器 key、签名运行观测和闭合单调时间窗。

创建会话不会直接授权执行，会话证据仍为 `Unknown`。只有对某条精确命令执行查询，并满足会话审批、前序状态、evaluation tick、控制器 endpoint acknowledgement 和全部依赖检查时，才返回窄范围 `RuntimeExecutable`。

API 不读取时钟、不发送命令、不操作硬件，也不创建开放式许可。任何字节、序号、时间窗、key、配置、证书或场景变化都会使精确授权失配。

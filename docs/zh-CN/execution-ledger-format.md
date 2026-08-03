# 撤销感知执行账本

> 英文原文：[Revocation-aware execution ledger](../execution-ledger-format.md)

`ExecutionLedger` 为已验证有界会话保存追加式授权与进度记录。每条命令必须先获得精确授权，再由控制器签名完成，账本才能推进到下一条。

每次推进重新验证调用方固定的当前信任检查点、审查 key、会话依赖和闭合时间窗。取消、过期、证书/配置/key 撤销或完成失败会记录为终态，不会悄然跳过命令。

离线审核重算父链、授权、完成签名和摘要。账本中的“completed”是控制器签名声明，不是 RBF-Safe 观测到物理动作。

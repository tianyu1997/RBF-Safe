# 服务信任历史 schema 1 与 2

> 英文原文：[Service trust-history schemas 1 and 2](../service-trust-history-format.md)

信任历史目录保存 caller-pinned root、每个 bundle、后继授权记录、manifest 和当前 head。schema 1 对应单签名 bundle 链；schema 2 对应 quorum bundle 链。

追加要求预期 head 和有效后继授权，采用 writer 锁与原子提交。打开时从固定 root 重放每个父子关系、签名和轮换策略，并应用 bundle/record/authorization/byte 上限。

历史是本地线性审计记录，不是远程信任分发协议。仅凭目录中的 manifest 不能检测完整目录回滚，必须固定外部 head 或检查点。

# 公共密钥服务身份

> 英文原文：[Public-key service identities](../public-service-identities.md)

`RBFSafe::identity` 使用调用方固定的 Ed25519 服务 key、单调信任包和离线验证，为跨服务制品与协调声明提供公共身份。

`ServicePublicKey` 绑定 service、key bytes、epoch、状态和允许的用途。信任包具有确定性 ID 与父摘要；轮换后继必须由前驱策略允许的 key 对精确后继签名。

schema 3 支持最小签名数和不同服务 quorum。库不从网络发现根、不自动选择最新包，也不把 key ID 等同于组织身份。调用方必须固定 root，并通过外部 head 或签名检查点检测目录整体回滚。

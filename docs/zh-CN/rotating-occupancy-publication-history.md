# 信任轮换占用发布历史

> 英文原文：[Trust-rotating occupancy publication histories](../rotating-occupancy-publication-history.md)

`RotatingOccupancyPublicationHistory` 允许一个占用流随着已授权 `ServiceTrustHistory` 轮换发布密钥，同时保留每条发布当时的完整信任上下文。

每条占用发布必须由其历史 bundle 中的有效 key 验证；新提交只能使用当前 trust head，禁止把后来的密钥向后应用。trust 和 publication 两条父链分别固定 root/head 并分别检测分叉。

比较历史时会区分信任链关系和发布链关系。签名检查、检查点固定、历史扩展或双链一致都只产生可审计软件证据，不产生控制器授权。

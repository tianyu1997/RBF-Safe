# 协调占用预留协议

> 英文原文：[Coordinated reservation agreements](../coordinated-reservation-agreements.md)

RBF-Safe 4.6 要求每个唯一部署都通过独立的信任轮换历史发布完全相同的分离车队占用 payload，才能组装一致预留协议。

协议固定 protocol、round、父协议、evaluation tick、payload/bundle/report、时间线、坐标系、有效窗，以及每个参与方的部署、stream、publisher key、trust/publication root/head 前缀。

组装时要求参与方集合完整且唯一、payload 字节一致、报告为声明包络下分离、有效窗覆盖评估 tick，并重放每条历史。后续重放允许历史向前扩展，但固定前缀必须完全匹配；fork、替换或后向信任会失败。

“一致”不是分布式共识、租约或实时控制器互锁。协议和所有签名历史保持非授权 `Unknown`。

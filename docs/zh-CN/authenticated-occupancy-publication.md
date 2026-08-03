# 认证占用发布

> 英文原文：[Authenticated occupancy publication](../authenticated-occupancy-publication.md)

`RBFSafe::coordination` 对一个已有的连续车队占用文件生成 Ed25519 签名发布。模块只读取并验证一次精确字节，不重新计算占用。

发布绑定 payload SHA-256/长度、bundle 与报告 ID、stream、publisher service/key、信任包、父发布、单调序号、闭合逻辑 tick 有效窗、时间线和坐标系。

离线验证要求调用方固定 stream、publisher、trust bundle、预期父发布和评估 tick，并检查密钥当前策略、签名、payload 替换和有效窗。有效签名只证明指定密钥发布了这些字节，结果仍为 `Unknown`。

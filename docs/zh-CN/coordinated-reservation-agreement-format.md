# 协调预留协议 schema 1

> 英文原文：[Coordinated reservation agreement schema 1](../coordinated-reservation-agreement-format.md)

协议保存为单个规范化 UTF-8 JSON 文件，格式与占用 bundle、信任历史和发布历史 schema 独立。

文件包含协议 payload、排序参与方、全部固定身份、父协议/round 和文件级 SHA-256。确定性 ID 由规范化 payload 导出。

加载会限制文件大小和参与方数量，拒绝重复部署/stream、非排序记录、无效摘要、空身份、时间窗错误、协议父关系错误、校验和错误和符号链接。加载只验证协议自身；完整重放仍需要调用方提供对应参与历史和精确占用文件。

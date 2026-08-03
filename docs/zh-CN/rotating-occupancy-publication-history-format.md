# 信任轮换占用历史格式

> 英文原文：[Trust-rotating occupancy publication-history format](../rotating-occupancy-publication-history-format.md)

目录在占用发布文件之外嵌入完整 `trust-history/`，并保存 manifest、records、publications 与去重 payloads。

manifest 固定信任 root/head、发布 root/head、stream、publisher、timeline、frame 和计数。打开时先完整验证信任历史，再逐条使用历史时点 bundle 验证发布。

加载限制 bundle、授权、发布、payload、记录和总字节数；拒绝后向信任使用、非当前 head 提交、两条链的 fork/rollback、意外条目、符号链接和内容篡改。

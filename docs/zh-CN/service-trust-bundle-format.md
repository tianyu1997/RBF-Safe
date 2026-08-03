# 服务信任包 schema 1、2 与 3

> 英文原文：[Service trust-bundle schemas 1, 2, and 3](../service-trust-bundle-format.md)

信任包是规范化 JSON，包含 schema、序号、父包 ID、服务 key 列表、轮换策略和确定性包 ID。

- schema 1：早期 key 记录；
- schema 2：单签名轮换用途；
- schema 3：显式 quorum 策略和授权集合。

后继必须保持 schema 规则、严格递增序号、精确父 ID、唯一 service/key、有效 epoch/状态和经过允许的轮换授权。读取器兼容历史 schema，但写入行为由对应创建 API 决定。

文件有效不代表其根可信；根包必须由调用方在目录之外固定。

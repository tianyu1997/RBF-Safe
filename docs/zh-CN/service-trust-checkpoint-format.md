# 服务信任检查点 schema 1

> 英文原文：[Service trust-checkpoint schema 1](../service-trust-checkpoint-format.md)

检查点是一个紧凑的签名 head 锚，声明某个固定 root 的信任历史已重放到精确 record/bundle/sequence。

它包含历史 root/head、签名 tick、签名者集合、策略身份和确定性 ID。验证要求调用方提供信任根与允许的检查点 key/quorum，并确认本地重放 head 精确匹配。

检查点不发现根、不选择“最新”记录、不建立 wall-clock 新鲜度，也不能替代完整历史中的后继授权验证。

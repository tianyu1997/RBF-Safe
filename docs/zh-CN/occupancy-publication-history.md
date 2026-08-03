# 占用发布历史

> 英文原文：[Occupancy publication histories](../occupancy-publication-history.md)

`OccupancyPublicationHistory` 保存同一 stream/publisher/trust 作用域下的精确占用字节、签名发布和不可变记录链。

创建和追加要求固定 root、当前 head 与预期父发布；writer 锁和预期 head 拒绝并发旧写者。每条记录绑定 payload 文件、发布 ID、父记录和序号。

打开历史时完整重放签名、payload 摘要、bundle/报告、有效窗、序列和父链。历史比较返回 identical、forward extension、reverse extension 或 fork。

目录整体回滚只能通过调用方外部保存的预期 head 发现；本地历史不是远程复制或共识协议。

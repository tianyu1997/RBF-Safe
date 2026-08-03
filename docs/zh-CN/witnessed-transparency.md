# 见证透明度与检查点 Gossip

> 英文原文：[Witnessed transparency and checkpoint gossip](../witnessed-transparency.md)

`RBFSafe::witness` 在本地透明日志之上交换和比较由独立见证者观察到的签名检查点。

紧凑前缀一致性证明连接不同树大小；检查点 cosignature quorum 表明多个固定身份观察到同一 head；gossip archive 保存父链接观测并构建证明图。

审计报告区分一致、证明不足和 split-view 冲突。矛盾 root、不可达证明路径、签名/信任不匹配或调用方固定 head 不一致均失败关闭。

gossip 传输仍由调用方实现。多个见证者一致只说明日志视图一致，不代表机器人状态或命令执行正确。

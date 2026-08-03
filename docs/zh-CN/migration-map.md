# 迁移映射

> 英文原文：[Migration map](../migration-map.md)

RapidBoxForest 仅作为只读行为参考。RBF-Safe 不依赖其源码路径、构建产物、Git 历史、缓存或运行时二进制。

## 主要处置

- Interval、modified-DH、IFK-AA/LinkIAABB：按新值类型和 `Result<T>` 重写，并用黄金数据验证行为；
- LECT：保留确定性分区思想，重写为稳定路径键、公共查询和新快照；
- SafeBoxForest 的安全盒验证/查询语义：按 Atlas 职责选择性迁移；
- 旧 journal、文本页、SBF adapter、实验缓存、规划 forest、HiPaC 旧布局：不直接复制；
- CritSample、MC、GCPC、KDOP、SupportHull 和论文实验流水线：不进入新核心。

后续版本在新架构上独立实现走廊、动态更新、规划、策略、安全记忆和部署层。任何实质复用均记录在[来源说明](provenance.md)并保留 MIT 权属。

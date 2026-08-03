# Schema 支持与迁移

> 英文原文：[Schema support and migrations](../schema-migrations.md)

库 SemVer 与存储 schema 相互独立。RBF-Safe 4.7 可读取从 0.x 起发布的独立格式，但不会把 RapidBoxForest 旧缓存解释为 RBF-Safe 数据。

## 规则

- 每种格式拥有自己的 `format` 和数值 `schema`；
- `library_version` 仅用于审计写入者，不决定读取兼容性；
- 未知 schema 默认拒绝，不能猜测字段；
- 迁移先完整验证旧文件，再通过公共对象模型写出新格式；
- 不允许原地覆盖唯一副本；
- 身份、证据等级和父链语义不得在迁移中提升。

Atlas、走廊、区域数据库、安全记忆、策略、信任、执行和占用格式彼此独立。一个格式升级不会自动要求其他格式升级。

精确“读/写 schema”矩阵见英文原文；固定历史 fixtures 在 Linux/Windows 上用于互读和损坏检测。

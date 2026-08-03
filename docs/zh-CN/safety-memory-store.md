# 事务化安全记忆修订仓库

> 英文原文：[Transactional safety-memory revision store](../safety-memory-store.md)

`SafetyMemoryStore` 在 schema-1 安全记忆之上提供不可变修订历史，适合多个进程以失败关闭方式发布。

每个修订保存完整安全记忆、父修订 ID、序号和提交记录。写入者必须提供外部保留的预期 head；旧 head、并发锁、重复序号或父关系不匹配会被拒绝。

发布使用临时目录、独占 writer 锁和提交后原子 head 更新。崩溃时只有完整提交记录可见。历史读取可以验证任意修订，但回滚不会删除后续记录。

本地文件锁和提交顺序不构成分布式共识；远程复制需要额外协议和信任锚。

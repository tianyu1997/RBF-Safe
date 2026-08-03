# 版本与兼容性

> 英文原文：[Versioning and compatibility](../versioning.md)

RBF-Safe 库版本使用 `MAJOR.MINOR.PATCH`：

- `MAJOR`：允许经过文档化的不兼容公共 API 变化；
- `MINOR`：向后兼容地增加公共能力；
- `PATCH`：修复缺陷，不改变预期契约。

CMake、`include/rbfsafe/version.h`、Python 包、MoveIt package、CITATION 和测试 consumer 必须使用相同库版本。磁盘文件中的 schema 单独递增。

4.7.0 在 4.6 基础上附加解析几何 Jacobian 和项目范围追踪，不修改已有 schema。调用方不应根据版本字符串跳过文件格式、身份或证据验证。

预发布使用 SemVer 后缀；正式发布的 tag、changelog、wheel 和 CMake package 必须来自同一已测试提交。

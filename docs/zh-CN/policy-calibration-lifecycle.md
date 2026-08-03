# 校准漂移与生命周期

> 英文原文：[Policy calibration drift and lifecycle](../policy-calibration-lifecycle.md)

校准生命周期把运行结果窗口与已审查的 `PolicyCalibrationProfile` 比较，并保存不可变父链接历史。

状态覆盖稳定、样本不足、漂移、隔离和经审查恢复。检测到漂移会自动隔离；恢复需要显式审查记录，不能由新的好结果自动激活。

发布要求调用方提供预期 head，以拒绝并发旧写者和分叉。策略门只接受精确匹配且当前为 active/stable 的历史 head。所有统计与记录仍是 `Unknown` 证据，不授权动作执行。

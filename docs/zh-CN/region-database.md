# 统一区域与证书数据库

> 英文原文：[Unified region and certificate database](../region-database.md)

`RegionDatabase` 为无法全部放入矩形 `SafeAtlas` 的认证几何提供统一查询层。它不会取代 Atlas 或 HiPaC，而是导入其输出并保留原始证书主题。

## 区域族

- C-space `AABB`；
- 定向 `OBB`；
- 共享见证 `Portal`；
- `TrajectoryTube`；
- `Zonotope`；
- 一阶 `Taylor` 区域。

每条记录包含稳定区域 ID、几何载荷、机器人/场景摘要、证书引用和来源元数据。不同几何类型的相交与包含必须由相应实现精确验证，不能仅靠包围盒近似签发连通证书。

## 查询和导入

数据库提供点包含、范围重叠、邻接、连通分量和证书查找。Atlas AABB、OBB Atlas 与 HiPaC 走廊可导入公共模型，重复稳定 ID 会确定性去重或显式拒绝冲突。

Zonotope 与 Taylor 记录表达高阶几何，但只有携带与主题匹配的有效证书时才是认证区域。单独构造几何对象不会产生证据。

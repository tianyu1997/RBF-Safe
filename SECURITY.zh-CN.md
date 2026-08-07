# 安全与安全性问题报告

[English](SECURITY.md) | 简体中文

## 支持版本

安全与正确性修复面向最新带 tag 的 5.x minor；条件允许时请在当前 `main` 上复现。

## 私密报告

解析器漏洞、校验和/身份绕过、内存安全缺陷或疑似 false-positive 区域/连通证书不要创建公开 issue。请使用 GitHub 私密漏洞报告：

<https://github.com/tianyu1997/RBF-Safe/security/advisories/new>

尽可能提供：版本/commit、平台、机器人与场景、配置盒与 `BuildOptions`、意外证书或畸形文件、最小可重复示例、确定性和预期影响。

涉及传输/公共 key/占用历史/协调预留时，可提供经过脱敏的请求、服务、bundle、root/head、序号、tick、payload 摘要与记录 ID。**绝不要提交共享密钥、Ed25519 seed/private key、生产轨迹或敏感占用数据。**

维护者会尽快确认完整报告并私下协调验证和修复；当前不承诺固定响应时间 SLA。

## 安全范围

RBF-Safe 证书是绑定已记录机器人、场景、算法和参数的软件几何声明，不能替代控制器限制、急停、独立碰撞监视、标定和应用风险评估。详见[安全模型中文版](docs/zh-CN/core/安全模型.md)。

占用发布历史只能保护所保留目录与调用方 pins。完整目录回滚必须依赖单独回滚域中的外部 head；fork 审核只比较所提供视图，不是 gossip 或全局共识。

信任轮换历史仍依赖调用方保留 trust root/head 或检查点，以及 publication root/head。旧签名检查点不会自动变得新鲜。

协调预留协议只证明显式参与历史对精确 payload 的一致发布。它不证明 peer 已收到、全局最新、时钟同步、网络共识、控制器 admission、预留执行或物理动作。所有这些结果保持非授权 `Unknown`。

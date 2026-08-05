# 贡献指南

[English](CONTRIBUTING.md) | 简体中文

感谢帮助改进 RBF-Safe。所有贡献都应保持认证声明范围小、保守、确定且可审计。

## 提交改动前

重大公共 API、schema 或认证逻辑变化应先创建 GitHub issue，说明数学声明、保守假设、身份输入和失败模式。安全漏洞或疑似 false-positive `CertifiedRegion`/连通结果不要公开提交，请遵循 [SECURITY.zh-CN.md](SECURITY.zh-CN.md)。

## 开发环境

```bash
cmake -S . -B build \
  -DRBFSAFE_BUILD_TESTS=ON \
  -DRBFSAFE_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure

python -m venv .venv
python -m pip install --upgrade pip build pytest
python -m build --wheel
python -m pip install --force-reinstall dist/rbfsafe-*.whl
pytest tests/test_python.py
```

C++ 改动提交前运行格式化，并执行仓库一致性检查：

```bash
clang-format -i $(find include src tests tools examples benchmarks python plugins \
  -type f \( -name '*.h' -o -name '*.cpp' \))

python tools/check_api_surface.py --root .
python tools/check_project_scope.py --root .
python tools/check_documentation.py --root .
```

## 改动规则

- 尽量只修改一个组件或职责层；公共契约确需跨模块时，保持 `docs/architecture.md` 规定的单向依赖；
- 公共头文件使用标准库值类型，不暴露 Eigen、JSON、pybind11 或存储实现；
- 预期失败返回 `Result<T>`；断言只用于内部程序员不变量；
- 采样可用于测试或优先级，但绝不能升级为 `CertifiedRegion`；
- 持久化变化必须有损坏测试、固定格式回归和明确兼容性决定；
- 派生或实质复用代码更新 `docs/provenance.md` 并保留版权；
- 修改 `docs/zh-CN/README.md` 所列核心指南时，同时更新 `tools/check_documentation.py` 登记的中文版本；详细英文规范不要求逐篇翻译；
- 不提交 build、wheel、缓存、本地路径、论文资产或实验输出。

## 最低测试

| 改动 | 最低验证 |
|---|---|
| 公共 API | 聚焦单测与独立 `find_package` consumer |
| 几何/认证 | 单元、黄金差分与包络包含性质测试 |
| LECT | 分裂、边界、重叠、稳定键与持久化 |
| Atlas | 重复/边界样本、预算、取消与连通性 |
| 持久化 | 往返、畸形输入、校验和与确定性字节 |
| Python | 安装后 wheel 测试，不能只测源码树 |

PR 必须说明安全影响、模块、兼容性和验证命令。所有 CI 作业通过后才能合并。

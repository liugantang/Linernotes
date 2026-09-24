# 贡献指南 (Contributing to AiMusic)

感谢关注与支持 AiMusic！我们欢迎并鼓励社区贡献代码、文档与反馈建议。

在参与贡献前，请通读以下内容及相关规范。

---

## 1. 开发流程

AiMusic 采用基于任务（Task-based）的迭代开发模式：

- **开发规划**：所有规划中的功能均在 [ROADMAP.md](docs/ROADMAP.md) 中细化为编号任务（如 `0.8b`、`2.1` 等）。在着手实现前，请先确认任务范围与验收标准。
- **开发规范**：详细的代码架构、命名规范、依赖边界、日志记录与测试规范请参见 **[开发规范 (docs/DEVELOPMENT.md)](docs/DEVELOPMENT.md)**。请务必遵守其中的技术约束（如 C++20 标准、模块单向依赖、音频不出本机等）。

---

## 2. 提交信息规范 (Commit Message Format)

提交信息遵循 [Conventional Commits](https://www.conventionalcommits.org/) 规范，主要描述使用中文：

```
<type>(<scope>): <简述，不超过 50 字>

<可选正文：为什么这么做、重要的取舍、背景说明>

Refs: ROADMAP <任务编号>
```

### Type 类型

| Type | 用途 |
|---|---|
| `feat` | 新功能 |
| `fix` | 缺陷修复 |
| `refactor` | 不改变外部行为的代码重构 |
| `perf` | 性能优化 |
| `test` | 仅测试代码相关 |
| `docs` | 仅文档变动 |
| `build` | 构建系统或依赖变动 |
| `ci` | CI 配置与脚本 |
| `chore` | 其他杂项变动 |

### Scope 模块范围

常用的 scope 包括：`player`, `library`, `db`, `ui`, `ai`, `butler`, `nlq`, `audio`, `guide`, `dj`, `archive`, `core` 等。

**示例**：
```
feat(library): 读取标签时保留 Latin-1 原始字节

Refs: ROADMAP 2.3
```

---

## 3. 提交 PR 前的自检命令

在发起 Pull Request 之前，请在本地运行并确保以下检查全部通过：

```bash
# 1. 编译构建
cmake --preset debug && cmake --build --preset debug

# 2. 运行所有单元测试
ctest --preset debug

# 3. 代码格式化检查（若未通过可运行 ./scripts/format.sh 自动修复）
./scripts/format.sh --check

# 4. clang-tidy 静态代码检查
./scripts/tidy.sh
```

如涉及新增/修改功能，请同时补充对应的单元测试（位于 `tests/` 目录下）。

---

## 4. 报告问题与提出需求

- **报告 Bug**：请通过 GitHub Issues 提交 [Bug Report](.github/ISSUE_TEMPLATE/bug_report.yml)。
  - 请详细描述复现步骤、预期行为与实际行为。
  - 请附上系统与版本信息，以及相关日志。日志目录位置可通过 `aimusic --print-paths` 命令输出查阅。
- **功能建议**：请通过 GitHub Issues 提交 [Feature Request](.github/ISSUE_TEMPLATE/feature_request.yml)，阐明应用场景与预期交互方式。

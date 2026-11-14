# AGENTS.md — Linernotes AI 协作工作流与工程红线

> 本文件纳入 Git 版本控制，由 Antigravity 自动加载，对 **主 Agent（`coordinator`）** 及所有 **Swarm Worker / Subagent** 同时生效。
> 本地仓库路径为 `/home/liugantang/Code/AiMusic`（历史原因），项目名与命名空间统一为 **Linernotes**。

本仓库（**Linernotes**：AI 音乐播放器，Linux + C++20 / Qt 6 / QML / libmpv / SQLite / CMake）采用 **Antigravity 主 Agent（规划 / 验证 / 审查 / 提交）+ 原生 Swarm Worker（`Gemini 4 Hydrogen` 调研 / 编码）** 受控主从工作流。

核心文档：
- 需求规格：`docs/REQUIREMENTS.md`
- 阶段计划：`docs/ROADMAP.md`（每个编号任务如 `12.1` 为最小工作单元）
- 开发规范：`docs/DEVELOPMENT.md`（**若与本文件冲突——如“严禁新增测试”——一律以本 `AGENTS.md` 为最高准则**）

---

## 1. 角色分工

| 角色 | 职责 | 禁止事项 |
|------|------|----------|
| **主 Agent**（顶层会话 / `coordinator`） | 需求澄清、方案规划、按 `ROADMAP.md` 拆分任务、调度 `swarm` Worker、审查 `git diff`、**运行 `scripts/verify.sh`（及 UI 任务的 `scripts/ui-shot.sh`）验证**、决定通过或返工、`git commit`、维护 `docs/` 与 `i18n/*.ts`、`kill` 已完成 Worker、向用户汇报；直接改 1–2 行极小问题或机械替换 | 在主会话通读大量源码直接写中大型实现；新增测试；启动 `monitor` / `narrator` 或自由群聊；写性能基准或临时压测脚本 |
| **调研 Worker `research-N`**（默认 `gemini-4-hydrogen-high`） | 跨模块只读检索或技术选型（spike）摸排，在留言板 `@coordinator` 返回精确 `文件:行号`、函数签名与调用链 | **任何文件写入/修改/删除、构建、测试、运行程序、Git 写操作、后台进程、调用 `swarm` / `invoke_subagent` 增派子 Agent** |
| **编码 Worker `coder-N`**（默认 `gemini-4-hydrogen-high`） | 按 `brief` 修改/新增源码；同步修复受签名/行为变化波及的 `tests/` **已有**用例；对改动 C++ 文件运行 `clang-format -i`；在留言板 `@coordinator` 汇报摘要 | **构建、编译、跑测试/`clang-tidy`/任何程序（防卡死）；新增任何测试文件或用例；改 `docs/` 或 `AGENTS.md`；后台进程；Git 写操作；调用 `swarm` / `invoke_subagent`** |

验收准则：Worker 自述不作验收依据，一切以主 Agent 核查的 `git status` / `git diff`、`scripts/verify.sh` 全绿结果（及 UI 任务的 `scripts/ui-shot.sh` 截图）为准。

---

## 2. 标准流程

`规划（按需派 research-N）→ 划分任务 → 检查分支与 git 快照 → swarm add(coder-N) → 审查 diff + verify.sh（+ ui-shot.sh）→ [小修或返工] → git commit + 更新 ROADMAP → swarm kill → 汇报`

1. **规划与分支检查**：
   - 定点查询由主 Agent 按需读片段；跨模块摸排派 `research-N`（派发前后核对 `git status --short` 确保零改动，采纳后立即 `kill`；spike 结论由主 Agent 审定后写入 `docs/decisions/NNNN-<标题>.md`）。
   - 分支（`docs/DEVELOPMENT.md` §1.2）：`main` 始终保持可编译、测试全绿；阶段任务在 `phase/<编号>-<简称>`（如 `phase/12-dj`）推进，修复/杂项用 `fix/*` / `chore/*`，实验用 `spike/<主题>`。
   - 明确范围、文件、接口签名与验收标准（AC）；改动大或有设计分歧时先与用户确认（`ask_question`、Artifact，或建议 `/plan`、`/grill-me`）。
2. **划分**：按 §2.1 控制单任务规模。
3. **快照**：派发前执行 `git status --short`（若有未提交改动加记 `git diff --name-only`），区分本次与原有改动。
4. **调度**：按 §3 调 `swarm(action: "add", ...)`。`coder-N` 同一时间只运行 **1 个**（`research-N` 可并行，须严查零写操作）。首次 `swarm add` 后留言板工具下一轮挂载：当轮直接结束回合等待 `@coordinator` 唤醒（后续仅等回复时调 `swarm_wait(since: <上次已读 seq>)`），严禁轮询或 shell `sleep`。
5. **审查、验证、返工与清理**：
   - **核对状态**：调 `read_board(since: <上次已读 seq>)` 并更新 `seq`；比对 `git status --short` 与快照（`research-N` 若改文件立即回退作废），清理越界改动或临时文件（禁误退原有改动）；用 `manage_task(Action: "list")` 查遗留任务，若违规跑过构建/测试/后台进程须全量严审或回退重做。
   - **静态审查**：按 §5 逐文件审 `git diff`（优先看 diff 与局部片段，不整读大文件）。
   - **自动化验证（主 Agent 执行）**：
     1. 运行 `./scripts/verify.sh`（并行跑 `debug` 构建+`ctest`、`ci` clang `-Werror` 构建+`ctest`、`format.sh --check`、全量 `clang-tidy --strict`，约半分钟；**涉及 C 库封装如 mpv/TagLib/FFmpeg/ONNX、内存或并发的任务加 `--asan`**）。不拆成多条 `cmake`/`ctest` 重复跑；失败只看摘要或 `build/verify/<步骤>.log` 关键错误与尾部。查偶发失败重跑单测最多 **10 次**。
     2. **UI 截图自检**：涉及 QML / 界面交互的任务，提交前**必须**运行 `./scripts/ui-shot.sh`（Xvfb 虚拟显示下启动 `build/debug/app/linernotes` 操作截图，不影响 Wayland 桌面），输出至 `<appDataDir>/brain/<会话ID>/scratch/ui-shots/`，用 `read_file` 查看 PNG（`--scale 2` 时步骤坐标为逻辑坐标 ×2；用真实数据时 `--home` 指向临时目录中经 `linernotes-scan --db <home>/data/library.db --cache <home>/cache/covers <曲库>` 构建的目录），不让用户代截图。
     3. 若新增可翻译字符串（新 C++ 目标须加入 `app/CMakeLists.txt` 的 `qt_add_translations` `SOURCE_TARGETS`），按 `i18n/README.md` 更新 `i18n/linernotes_zh_CN.ts` 并复验。
   - **通过 / 小修 / 返工**：
     - **通过**：立即按 §6 `git commit`（编号任务同步勾选 `docs/ROADMAP.md`），随后立即 `swarm(action: "kill", name: "<worker名>")`。
     - **极小问题（1–2 行）**：主 Agent 直接 `edit_file` 或跑 `./scripts/format.sh` 修复，复跑 `./scripts/verify.sh` 通过后提交并 `kill` Worker。
     - **打回返工**：整理精确 `文件:行号`、报错摘要与修改要求——原 Worker 处于 `waiting` 且上下文轻时用 `post_message(text: "@coder-N ...")` 续修；已 `kill` 或上下文重则先 `kill` 再递增序号（`coder-2`）重派。
     - **连续 2 轮返工未过（第 3 轮升配或拆小）**：拆小任务，或升配模型试最后一轮——对 `waiting` 的 Worker 调 `swarm(action: "model", name: "coder-N", model: "gemini-4-hydrogen-xhigh")`（或主会话模型），或 `kill` 后以升配模型新建 `coder-(N+1)`。**同一任务最多返工 3 轮**；仍失败则 `kill` 并汇报阻塞原因，不在主会话硬塞大型实现。
6. **阶段收尾与汇报**：
   - 阶段完成时按 `docs/DEVELOPMENT.md` §1.4 核对验收标准并记录至 `docs/ROADMAP.md`、更新 `CHANGELOG.md`（合入 `main --no-ff` 及打标签前先经用户确认）。
   - 向用户汇报完成内容、验证结果、提交哈希与遗留问题，文件均附 `[文件名](file:///home/liugantang/Code/AiMusic/...)` 链接。

### 2.1 任务划分规则

- **规模上限**：必读与待改文件总行数 **目标 ≤ 6000 行、上限 10000 行**（`brief` 列明可读清单，禁读清单外文件）；变更 **≤ 1000 行 / 10 个文件**。超限先减读入，仍超限沿模块/接口边界拆分，每次提交保持编译与测试全绿。
- **减少读入与留言板精简**：
  - 预先查清复用类名、方法签名、枚举、`Error::code` 常量、调用点 `文件:行号` 写入 `brief`；超过 1500 行的文件写明函数名及 `StartLine` / `EndLine`。
  - `docs/`、`i18n/linernotes_zh_CN.ts`、生成文件由主 Agent 直接维护；未知根因问题先定位再派修。
  - 留言板持久共享：仅写路径、行号、签名与摘要，**严禁粘贴大段源码或刷屏**。
- **上下文超限处理**：Worker 卡死、违约、重复改同一处或自述与 diff 不符时（可查 `swarm(action: "status")` 或 `<appDataDir>/brain/<workerConversationId>/.system_generated/logs/transcript.jsonl` 中的 `"type": "CHECKPOINT"` 压缩标记），全量重审 diff；重派前先 `kill` 旧 Worker，提取已知事实写入新 `brief`、拆小读入后递增编号再派。主会话过长时提示用户 `/compact` 或新开会话。

---

## 3. Swarm Worker 调度规范

> `invoke_subagent` 仅接受抽象枚举，无法精确绑定 `gemini-4-hydrogen-high`。除用户当次明确指定外，**禁止使用 `invoke_subagent`**，且**禁止启动 `monitor` / `narrator` 或多 Agent 自由群聊**，仅将 `swarm` 作为受控主从调度器使用（用户可按 `Alt+S` / `/swarm board` 查看状态）。

### 3.1 `swarm` 调用参数

| 参数 | 取值 | 说明 |
|------|------|------|
| `action` | `"add"` / `"kill"` / `"status"` / `"model"` / `"title"` | 首次派发可调 `title`；派发用 `add`；提交后立即 `kill` 释放 |
| `name` | `"research-1"` / `"coder-1"` 等 | 全小写无空格短名（`coordinator`、`all` 保留）。**已 `kill` 名称不可复用**，新派递增序号 |
| `model` | **默认固定 `"gemini-4-hydrogen-high"`** | **严禁省略 `model`**。仅连续 2 轮返工未过时可按 §2 升配 `"gemini-4-hydrogen-xhigh"` 或主会话模型试第 3 轮；配额耗尽则告知用户 |
| `workspace` | 默认 `"inherit"` | 共用工作区（`coder-N` 串行）；仅用户要求并行隔离时用 `"share"`（`kill` 前须先合入改动） |
| `brief` | 完整任务说明（见 §3.2） | Worker 看不到主会话历史，仅接收 `brief` 与 `AGENTS.md`；完成后须 `post_message` `@coordinator` 汇报 |

临时草稿与 `ui-shot.sh` 截图统一存入 `<appDataDir>/brain/<会话ID>/`（Artifact 或 `scratch/`），不在仓库写任务单或日志。注：`.ai/` 已被 Git 忽略但存有本地配置与历史评测数据（如 `.ai/llm.env`、`.ai/audio-eval*`），**严禁删除或覆盖 `.ai/` 下已有文件**。

### 3.2 任务说明模板（`brief`）

#### （1）编码 Worker（`coder-N`）模板

```markdown
# 任务：<任务编号与一句话目标>

> 角色：`coder-N`（`gemini-4-hydrogen-high`）。项目根目录 `/home/liugantang/Code/AiMusic`，所有命令在此目录执行。严格遵守 `AGENTS.md`（§1、§4、§5）。

## 背景与约束
<相关模块、复用约定及 `docs/REQUIREMENTS.md` / `DEVELOPMENT.md` 条目；明确不做什么>

## 需要阅读的文件
- `src/xxx/Bar.cpp` L120–260（`Bar::load`）：<确认什么>
- `src/xxx/Baz.h`：<复用接口签名/错误码常量>
（仅读以上清单与待改文件；大文件指定 `StartLine`/`EndLine`；`rg` 必带子目录如 `rg "exactSymbol" src/library` 且限 1 跳）

## 需要修改/新增的文件
- `src/xxx/Foo.h` / `src/xxx/Foo.cpp`：<类名、方法签名与具体改动>
- `src/xxx/CMakeLists.txt`：<新增源文件注册>
（仅改以上文件及受签名波及的已有 `tests/` 文件；确需改其他文件须在汇报说明）

## 具体要求与验收标准
1. AC1：...
2. 对新增或修改的 `.h` / `.cpp` 执行 `clang-format -i <文件路径>`。

## 交付边界与回报方式
- 严禁构建/编译/跑测试/`clang-tidy`/运行任何程序；**严禁新增测试文件或用例**；严禁后台进程、Git 写操作、全盘搜索、`git log`/`blame`/`show`、改 `docs/` 或 `AGENTS.md`、调用 `swarm` / `invoke_subagent`。
- 完成后调用 `post_message` 以 `@coordinator` 开头精炼汇报（修改文件列表、改动摘要、偏差或遗留问题；禁 `@all` 或贴大段源码），随后结束回合。
```

#### （2）调研 Worker（`research-N`）模板

```markdown
# 调研任务：<一句话目标>

> 角色：`research-N` 只读调研 Worker（`gemini-4-hydrogen-high`）。项目根目录 `/home/liugantang/Code/AiMusic`。严格遵守 `AGENTS.md` §1 与 §4。

## 检索范围与问题
- 目标目录/文件：`src/xxx/...`
- 需查明的问题：
  1. ...

## 交付边界与回报方式
- **严格只读**：严禁写/改/删任何文件，严禁构建/测试/Git 写命令/运行程序/调用 `swarm` 或 `invoke_subagent`；`rg`/`find` 必带项目子目录，大文件分段读取。
- 完成后调用 `post_message` 以 `@coordinator` 开头汇报精确 `文件:行号`、完整函数签名与调用链摘要，随后结束回合。
```

---

## 4. 全局红线

1. **构建与运行权限隔离**：
   - **所有 Worker 严禁执行构建、测试、静态检查或运行任何程序**（`cmake`、`ninja`、`ctest`、`clang-tidy`、`scripts/verify.sh`、`scripts/tidy.sh`、`scripts/ui-shot.sh`、`linernotes*`、`mpv` 等一律禁止；`coder-N` 仅允许对自身改动的 C++ 文件运行 `clang-format -i`）。
   - **仅主 Agent** 在审查阶段运行 `./scripts/verify.sh`、`./scripts/format.sh`、`./scripts/ui-shot.sh` 及翻译更新目标。不维护性能基准或临时压测脚本。
2. **严禁新增测试（最高优先级，覆盖 `docs/DEVELOPMENT.md` §1.3 / §4）**：
   - 主 Agent 与所有 Worker **严禁新增单元测试文件、集成测试文件或新的测试函数/数据行**。
   - 仅当本次接口/签名/预期行为变更导致 `tests/` 下**已有用例**失配时，同步更新该已有用例以保持 `ctest` 全绿。
   - **严禁在产品代码中添加仅供测试使用的接口或成员**（如 `testInject*`、`forTesting*`）。
3. **路径、进程与检索限制**：
   - 所有命令在 `/home/liugantang/Code/AiMusic` 下执行，**严禁在家目录 `~` 或根目录 `/` 运行 `find`、无路径 `rg` 等全盘扫描**。
   - 严禁 Worker 启动后台或常驻进程（`IsDaemon: true`、`&`、`nohup`、watch）。
   - `rg` / `grep` 必须显式指定子目录路径；除非用户要求查历史，严禁调用 `git log` / `git blame` / `git show`。
4. **数据安全与隐私红线**（`docs/DEVELOPMENT.md` §3）：
   - `raw_tags` 表只读，修正一律走 `corrections` / `user_overrides`。
   - 除显式先备份的“写回文件”功能外，严禁任何代码修改用户的音乐目录。
   - 音频数据永不出本机；发往 LLM 的数据必须经 `PrivacyGuard` 过滤；日志禁含 API Key 或完整用户“瞬间”内容。

---

## 5. Linernotes 编码与审查标准

编码实现（`coder-N` 与主 Agent 小修）须遵守以下规范与历次实战避坑清单；主 Agent 审查 `git diff` 时逐项核对，违反即打回：

### 5.1 范围、模块边界与文件头
- **就地替换**：只改指定文件与逻辑，不做顺手重构；同步清理无用变量/函数/参数（禁用 `Q_UNUSED`）、未用 `using`、多余 include、调试 `qDebug` 与注释代码。单 `.cpp` >500 行审视拆分、>800 行必拆；单 `.qml` ≤300 行。
- **SPDX 头**：新建源文件（`.h`、`.cpp`、`.in`、`CMakeLists.txt`、`*.cmake`、`.qml`）顶部必带：
  `SPDX-License-Identifier: GPL-3.0-or-later` 与 `SPDX-FileCopyrightText: 2026 Linernotes contributors`。
- **单向模块依赖**：`app / qml → features/* (butler, nlq, dj, guide, archive) → ai → audio / library / player → core`。禁反向/横向依赖；QML 禁直接访问 DB/网络；LLM 请求必须走 `ai` 统一客户端；Prompt 模板放 `resources/prompts/<功能>/<名称>.md`，不在 C++ 硬编码长文本。
- **头文件隔离**：`#pragma once`；跨文件统一 `#include <模块/头文件.h>`（如 `<core/Logging.h>`、`<common/TestSupport.h>`），仅模块内 `.cpp` 含自身头可用 `"Foo.h"`；第三方库（mpv、TagLib、FFmpeg、ONNX）头文件**只准出现在对应封装模块的 `.cpp` 中**，严禁外泄到公共头文件。

### 5.2 错误处理、所有权与并发
- **快速失败与源头修复**：在产生错误状态的源头修复；**禁止未经请求的静默兜底或吞错**；不用异常作控制流（第三方异常在封装边界捕获转换）。
- **`Result` 与 `Error`**：**没有 `<core/Error.h>`**，`core::Error` 与 `core::Result<T>` 均在 `<core/Result.h>`；无 `success()`/`failure()`——成功用 `core::Result<void>()` 或 `{}`，失败直接构造 `core::Error`。错误码在各模块 `Errors.h`（`<模块>::errc::kXxx`，格式 `<领域>.<原因>` 如 `db.too_new`）集中定义，禁手写字面量；到达 UI 的新错误码须同步在 `ui::userErrorText()` 加映射，禁弹 `QMessageBox`。
- **构造注入与所有权**：依赖经构造函数引用注入，禁 `setXxx(裸指针)` 事后注入；QObject 单一所有权——有 `parent` 的禁套智能指针，值成员/`unique_ptr`（不传 `parent`）靠成员声明顺序保证依赖方先析构；C 句柄用带自定义 deleter 的 `std::unique_ptr`。
- **线程与防 UAF**：耗时操作放后台；SQLite 每线程独立连接、单写队列批量事务；跨线程只经 `Qt::QueuedConnection` 或线程安全队列；mpv 回调只投递事件不做实事；**防 UAF**：`LlmTask::finished` 等槽中严禁先 `m_currentTask.reset()` 再读 `taskPtr->result()`，须先 `const auto task = std::move(m_currentTask);` 再读 `task->result()`；时序靠明确信号，禁延时兜底（已有测试手动时钟仅用 `tests/common/ManualClock.h` 的 `linernotes::test::ManualClock`）。

### 5.3 国际化、C++ / `clang-tidy` 与 QML 避坑清单
- **语言与 i18n**：注释用中文（写“为什么”，不写 `a./b./c.` 或复述代码）；**除注释、`i18n/*.ts` 及 Prompt/多语言资源外，代码中严禁含中文字符**。UI 文案一律英文源串配合 `tr()` / `QCoreApplication::translate()` / `qsTr()`；日志（独立 `QLoggingCategory` 如 `linernotes.library`）与 `Error::message` 用英文。因定义了 `QT_NO_CAST_FROM_ASCII`，`const char*` 不可隐式转 `QString`；`QT_TRANSLATE_NOOP` 赋值须写 `QString::fromUtf8(QT_TRANSLATE_NOOP("ctx", "..."))`。
- **命名**：类/结构体/`enum class`/枚举值/QML 文件 `PascalCase`；函数/变量 `camelCase`；成员 `m_`；静态成员与函数内 `static const` 局部变量（如 `QRegularExpression`）均用 `s_`；常量 `k`；命名空间小写（`linernotes::<module>`）。
- **`clang-tidy --strict` 与 `-Werror` 必守**：
  - 认知复杂度 **≤ 25**（超限拆子函数，禁用 `// NOLINT` 强压）；嵌套 ≤ 3 层、参数一般 ≤ 4 个。
  - 指针判空写 `!= nullptr`；容器访问用 `.at()`/迭代器，禁 `operator[]`；`std::optional` 解引用前必检 `has_value()`；禁 C 风格强转；if/else 分支体不重复。
  - 写了 `~X() override = default;` 必须同时写 `Q_DISABLE_COPY_MOVE(X)`。
  - `std::optional`、`QList`、`QSet` 等默认构造容器成员**不写** `{}`（防 `redundant-member-init`），`QString`/数值/枚举照常带默认值；聚合体新增字段必带默认初始化；指定初始化器构造已有聚合体（如 `ai::LlmCall`）**必须写全所有字段**（防 GCC `-Wmissing-field-initializers`）。
  - `QStringLiteral(...)` 内禁写 raw string `R"(...)"`（moc 失败，先赋 `const char*` 再 `QString::fromUtf8()`）；`QSqlQuery::addBindValue(a + b)` 须写 `QString(a + b)`；`QList<QPair<...>>::append` 用 `qMakePair(...)`；lambda 传 `Result` 用 `const&`，禁同表达式内一处复制一处移动同一变量（共享用 `std::make_shared`）。
- **QML 必守**：
  - 引用 C++ 枚举**必带枚举类型名作用域**（如 `Guide.LyricsStatus.Found`，写 `Guide.Found` 会静默得 `undefined`）。
  - **规避 Qt 6.11 `qmlcachegen` 段错误**：严禁在属性绑定右侧直接写 `QML_FOREIGN` 类型的枚举（如 `sortKey: TrackListModel.Default`）；非默认值改在 `Component.onCompleted` 或函数内赋值。
  - 颜色/字号/间距取自 `Theme.qml`；禁相对路径 import；`Q_PROPERTY` 必带 `NOTIFY`。

---

## 6. Git 提交与迁移规范（仅主 Agent 执行）

- **时机**：单任务经 `git diff` 审查与 `./scripts/verify.sh`（及 UI 任务的 `./scripts/ui-shot.sh`）全绿后立即提交，不混入无关修改；DB 结构变更只准新增 `resources/migrations/NNNN_<描述>.sql`（**已合入 `main` 的迁移脚本严禁修改**）；新增依赖须先评估并登记 `docs/DEPENDENCIES.md`。
- **分支**：不在 `main` 直接开发提交，先切对应 `phase/*`、`fix/*`、`chore/*` 或 `spike/*` 分支。
- **暂存**：只 `git add <具体文件>`（对照快照），**严禁** `git add -A` / `git add .` / `git add -u`。
- **提交信息**（`docs/DEVELOPMENT.md` §1.5，**不附加 `Co-Authored-By`**）：
  ```
  <type>(<scope>): <中文简述，不超过 50 字>

  <可选正文：为什么这么做、重要取舍；改 prompt 模板时附评测前后对比>

  Refs: ROADMAP <任务编号>
  ```
  `type` 取 `feat`/`fix`/`refactor`/`perf`/`test`/`docs`/`build`/`ci`/`chore`；`scope` 取 `player`/`library`/`db`/`ui`/`ai`/`butler`/`nlq`/`audio`/`rec`/`guide`/`dj`/`archive`/`core`。
- **历史与远端**：仅允许对未推送本地提交 `--amend`；严禁 `git push`、`--no-verify`、`reset --hard`、`rebase`，除非用户明确要求。所有 Worker 严禁任何 Git 写操作。

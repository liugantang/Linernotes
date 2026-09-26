# Linernotes — Claude 工作指南

> 本地目录名仍为 `AiMusic`（历史原因），项目名与代码中统一使用 Linernotes。

Linernotes：AI 音乐播放器，C++20 / Qt 6 / libmpv。先读：
- 需求：`docs/REQUIREMENTS.md`
- 分阶段计划：`docs/ROADMAP.md`
- 开发规范（流程、提交、代码、单测）：`docs/DEVELOPMENT.md` —— 所有工作都必须遵守

## 分工：Claude 规划与审查，Gemini 执行

Claude 的调用成本高，Gemini 便宜。因此：

- **Claude 负责**：拆分任务、编写任务说明、审查 Gemini 的产出、决定返工或通过、提交代码、维护 docs。
- **Gemini（通过 `agy` CLI）负责**：按任务说明写代码、写测试、编译、运行测试。
- Claude **不要自己写实现代码**；只有在审查时发现的极小问题（改一两行），直接改比再委托一次更省时，才可以自己改。
- Claude 节省 token：审查时优先看 `git diff` 和测试结果，按需读相关文件片段，不要整读大文件；命令输出只看尾部或 grep 关键信息。

### 每个任务的流程（对应 ROADMAP 中的一个编号任务）

1. **写任务说明**：在 `.ai/tasks/<任务编号>.md`（该目录已被 git 忽略）写清：
   - 目标与范围（做什么、明确不做什么）
   - 涉及的文件/模块，需要新增或修改的接口（给出类名、关键方法签名）
   - 关键设计要求与约束（引用 REQUIREMENTS / DEVELOPMENT 的相关条目）
   - 必须编写的测试用例清单
   - 验收标准：统一为 `scripts/verify.sh --quick` 通过，以及需要满足的行为（不要在任务说明里罗列 ci/asan/tidy 等命令，执行者按 COMMON.md 的“验证”一节执行）
   - 固定要求：先阅读 `docs/DEVELOPMENT.md`；**不要执行 git commit / 切换分支**；只改与任务相关的文件；完成后运行构建和 `ctest`，在最终回复中汇报修改的文件列表、测试结果和遗留问题
2. **委托执行**（执行时间长，放到后台运行，Bash 的 `run_in_background: true`）：
   ```bash
   cd /home/liugantang/Code/AiMusic && agy -p "项目根目录是 /home/liugantang/Code/AiMusic，所有命令都在此目录下执行。请阅读 /home/liugantang/Code/AiMusic/.ai/tasks/<任务编号>.md 并完成其中的任务。" \
       --dangerously-skip-permissions --model gemini-3.7-flash-high \
       --print-timeout 60m > .ai/logs/<任务编号>.log 2>&1
   ```
3. **审查**：看 `git status`、`git diff`；自己运行**一次** `scripts/verify.sh`（并行跑 debug + ci 构建与 ctest、格式、全量 `clang-tidy --strict`，约半分钟；涉及 C 库封装、内存/线程的任务加 `--asan`）。不要只信汇报，但也不要拆成多条命令重复跑。对照任务说明与 DEVELOPMENT.md 检查范围、设计、测试是否充分、有无越界修改。
4. **返工**：问题写进 `.ai/tasks/<任务编号>.review.md`，然后再次委托：
   ```bash
   cd /home/liugantang/Code/AiMusic && agy -p "项目根目录是 /home/liugantang/Code/AiMusic，所有命令都在此目录下执行。请阅读 /home/liugantang/Code/AiMusic/.ai/tasks/<任务编号>.review.md，按审查意见修改。" \
       --dangerously-skip-permissions --model gemini-3.7-flash-high \
       --print-timeout 60m > .ai/logs/<任务编号>-r<N>.log 2>&1
   ```
   同一任务连续两轮返工仍不通过：换 `--model gemini-3.1-pro-high` 再试，或把任务拆小；仍不行再向用户说明情况。
5. **提交**：审查通过后由 Claude 按 DEVELOPMENT.md 1.5 的格式提交，并在 ROADMAP 中标记任务完成。
6. **阶段收尾**：按 DEVELOPMENT.md 1.4 核对阶段验收标准，合并到 `main` 并打标签。

### 其他

- **prompt 中必须写明项目绝对路径和任务文件的绝对路径**：agy 的 shell 可能从家目录启动，曾因 `find . -name 0.1.md` 扫描整个家目录而卡住十几分钟。若 agy 长时间无文件改动，用 `pstree -ap <pid>` 查看是否有卡住的子进程。

- 较大的技术选型或验证（spike）也可以交给 Gemini 调研，但结论由 Claude 审查后写入 `docs/decisions/`。
- 可用模型用 `agy models` 查看。
- 重复运行测试（查偶发失败、稳定性验证）最多 10 次，任务说明中也按此上限要求。

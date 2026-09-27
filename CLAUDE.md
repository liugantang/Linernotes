# Linernotes — Claude 工作指南

> 本地目录名仍为 `AiMusic`（历史原因），项目名与代码中统一使用 Linernotes。

Linernotes：AI 音乐播放器，C++20 / Qt 6 / libmpv。先读：
- 需求：`docs/REQUIREMENTS.md`
- 分阶段计划：`docs/ROADMAP.md`
- 开发规范（流程、提交、代码、单测）：`docs/DEVELOPMENT.md` —— 所有工作都必须遵守

## 分工：Claude 规划与审查，Gemini 执行

Claude 的调用成本高，Gemini 便宜。因此：

- **Claude 负责**：拆分任务、编写任务说明、**构建与运行测试**、审查 Gemini 的产出、决定返工或通过、提交代码、维护 docs。
- **Gemini（通过 `agy` CLI）负责**：按任务说明写代码与测试。**agy 禁止构建、运行测试或任何程序**（曾多次因等待构建/测试/基准卡死），只能读代码、改代码、运行 clang-format。编译错误与测试失败由 Claude 在审查时发现：一两行的直接改，其余写进返工意见。
- Claude **不要自己写实现代码**；只有在审查时发现的极小问题（改一两行），直接改比再委托一次更省时，才可以自己改。
- Claude 节省 token：审查时优先看 `git diff` 和测试结果，按需读相关文件片段，不要整读大文件；命令输出只看尾部或 grep 关键信息。

### 每个任务的流程（对应 ROADMAP 中的一个编号任务）

1. **写任务说明**：在 `.ai/tasks/<任务编号>.md`（该目录已被 git 忽略）写清：
   - 目标与范围（做什么、明确不做什么）
   - 涉及的文件/模块，需要新增或修改的接口（给出类名、关键方法签名）
   - 关键设计要求与约束（引用 REQUIREMENTS / DEVELOPMENT 的相关条目）
   - 必须编写的测试用例清单
   - 验收标准：需要满足的行为（构建与测试由 Claude 运行 `scripts/verify.sh` 验证）
   - 固定要求：先阅读 `.ai/COMMON.md`；**不要执行 git commit / 切换分支**；**不要构建、不要运行测试**；只改与任务相关的文件；在最终回复中汇报修改的文件列表和遗留问题
   - 测试要求按 DEVELOPMENT 4.6 从简：只列真正需要的几个测试点，不要穷举组合
2. **委托执行**（执行时间长，放到后台运行，Bash 的 `run_in_background: true`）：
   ```bash
   cd /home/liugantang/Code/AiMusic && agy -p "项目根目录是 /home/liugantang/Code/AiMusic，所有命令都在此目录下执行。请阅读 /home/liugantang/Code/AiMusic/.ai/tasks/<任务编号>.md 并完成其中的任务。" \
       --dangerously-skip-permissions --model gemini-3.7-flash-high \
       --print-timeout 20m > .ai/logs/<任务编号>.log 2>&1
   ```
3. **审查**：看 `git status`、`git diff`；自己运行 `scripts/verify.sh`（agy 不编译，第一次常有编译错误：小错直接改，多了就返工）（并行跑 debug + ci 构建与 ctest、格式、全量 `clang-tidy --strict`，约半分钟；涉及 C 库封装、内存/线程的任务加 `--asan`）。不要只信汇报，但也不要拆成多条命令重复跑。对照任务说明与 DEVELOPMENT.md 检查范围、设计、测试是否充分、有无越界修改。
4. **返工**：问题写进 `.ai/tasks/<任务编号>.review.md`，然后再次委托：
   ```bash
   cd /home/liugantang/Code/AiMusic && agy -p "项目根目录是 /home/liugantang/Code/AiMusic，所有命令都在此目录下执行。请阅读 /home/liugantang/Code/AiMusic/.ai/tasks/<任务编号>.review.md，按审查意见修改。" \
       --dangerously-skip-permissions --model gemini-3.7-flash-high \
       --print-timeout 20m > .ai/logs/<任务编号>-r<N>.log 2>&1
   ```
   同一任务连续两轮返工仍不通过：换 `--model gemini-3.1-pro-high` 再试，或把任务拆小；仍不行再向用户说明情况。
5. **提交**：审查通过后由 Claude 按 DEVELOPMENT.md 1.5 的格式提交，并在 ROADMAP 中标记任务完成。
6. **阶段收尾**：按 DEVELOPMENT.md 1.4 核对阶段验收标准，合并到 `main` 并打标签。

### 其他

- **prompt 中必须写明项目绝对路径和任务文件的绝对路径**：agy 的 shell 可能从家目录启动，曾因 `find . -name 0.1.md` 扫描整个家目录而卡住十几分钟。若 agy 长时间无文件改动，用 `pstree -ap <pid>` 查看是否有卡住的子进程。

- 较大的技术选型或验证（spike）也可以交给 Gemini 调研，但结论由 Claude 审查后写入 `docs/decisions/`。
- 可用模型用 `agy models` 查看。
- 重复运行测试（查偶发失败、稳定性验证）最多 10 次。
- **界面审查自己截图**：`scripts/ui-shot.sh` 在 Xvfb 虚拟显示中启动程序、按步骤点击/按键并截图（不影响用户的 Wayland 桌面），用 Read 查看 PNG。步骤坐标是物理像素（`--scale 2` 时为逻辑坐标 ×2）。用真实数据时 `--home` 指向 scratchpad 中用 `linernotes-scan --db <home>/data/library.db --cache <home>/cache/covers <曲库目录>` 建好的目录。涉及 UI 的任务，提交前至少截图检查一次，不要让用户代为截图。
- 不维护性能基准代码，也不写临时压测脚本；性能只在真实使用中感到卡顿时再排查。

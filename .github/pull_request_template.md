## 关联任务

- 关联 ROADMAP 任务编号：Refs: ROADMAP 

## 改动说明

<!-- 简要说明本次 PR 的修改内容、设计考量与重要取舍 -->

## 测试说明

<!-- 简要说明新增的测试用例或手动验证结果 -->
- [ ] 自动化测试（`ctest --preset debug` 全部通过）
- [ ] 手动验证（如有 UI 或用户交互变动，请简述验证过程与效果）

## 提交前检查清单 (Checklist)

> 请确保在提交 PR 前已核对以下项目（参见 [DEVELOPMENT.md](docs/DEVELOPMENT.md) 1.3 节）：

- [ ] 能在 Debug 与 Release 下编译，无新增警告
- [ ] `ctest` 全部通过
- [ ] 已运行 `clang-format`（`./scripts/format.sh --check` 通过）；`clang-tidy` 无新增问题（`./scripts/tidy.sh` 通过）
- [ ] 新增/修改的逻辑有对应测试
- [ ] 涉及数据库结构的改动有迁移脚本
- [ ] 涉及用户可见行为的改动，已手动运行程序验证过
- [ ] 没有提交密钥、个人路径、调试残留（`qDebug` 打桩、注释掉的大段代码）

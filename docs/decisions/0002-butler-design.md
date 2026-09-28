# 0002 · 管家（butler）的模块与数据流

> 2026-09-28 · 阶段 6

## 背景

阶段 6 起步“AI 管家”：乱码修复、多艺人拆分、艺人归一，以及所有 AI 修改共用的“提议 → 审核 → 生效 → 撤销”工作流。阶段 7（补全、版本、重复、写回）沿用同一套机制。

## 决定

1. **新模块 `butler`**（`src/butler`，目标 `linernotes_butler`，命名空间 `linernotes::butler`）：依赖 `ai`、`library`、`core`；`ui` 依赖 `butler`。乱码规则、拆分规则、艺人名规范化是纯逻辑类；每类整理任务是一个 `ai::JobHandler`，由 `JobQueue` 调度（暂停/续跑/预估沿用阶段 5）。
2. **修改只以“修正”形式产出**：butler 不直接改元数据，只往 `correction_batches` / `corrections` 写 `pending` 行。读写这两张表、接受/拒绝/编辑/整批撤销由 `library::CorrectionStore` 负责（它和 `OverrideStore` 一样，在同一事务里重新关联实体、清理孤儿、刷新搜索索引）。effective 层由现有触发器按 `accepted` 的修正计算。
   - 一次任务运行 = 一个批次，`correction_batches.kind` 标明种类（`mojibake`、`artist_split`、`artist_merge`…）。
   - 撤销批次：该批次所有 `accepted` 的修正改为 `reverted`，`pending` 的改为 `rejected`；写 `reverted_at`。
   - 高置信度自动接受（F-BUT-10）：写入时按阈值直接置为 `accepted`，仍在批次里可见、可撤销。
3. **规则优先、LLM 只处理疑难项**：每个 handler 先跑规则，只有规则判不了的才调用 LLM（经 `LlmService`，结构化输出 + JSON Schema）。handler 的 LLM 部分拆成“构造请求”与“把结构化结果转成修正”两个纯函数，后者用 JSON 字面量做单元测试。
4. **乱码修复**：优先用 `raw_tags.raw_bytes`（阶段 2 保留的 Latin-1 原始字节），没有时把字符串按 Latin-1 / Windows-1252 回编码。候选解码用 ICU 转换器（GB18030、Big5、Shift_JIS、EUC-KR、UTF-8）。内嵌少量高频字表（简体/繁体各 1500 字，由 rime-data 的 essay.txt 词频按字累加得到；韩文约 400 音节），用于区分编码区重叠的情况（EUC-KR 韩文区与 GB2312 一级字区重叠）；结合编码自身分区（GB2312 一级字、Big5 常用字、JIS 第一水准、KS X 1001 常用音节区）、uchardet 的判断与非法/罕见字符惩罚。同专辑（同目录）曲目共享编码判断。
5. **艺人实体与别名**：艺人归一不改曲目的 `artist` 字段，而是“别名”修正（`entity_type='artist'`，`field='alias'`，`entity_id` 为规范艺人）。接受后写 `artist_aliases`，`EntityLinker` 解析艺人名时先查别名，于是 “Jay Chou” 的曲目挂到“周杰伦”实体上，原实体成为孤儿被清理；撤销则删除别名并重新关联。各语言名作为带 `locale` 的别名保存，界面按“显示名偏好”选取。
6. **多艺人拆分**是曲目 `artist` 字段的修正，值用现有的多值约定 `" / "` 连接（`EntityLinker` 已按它拆分）。组合名白名单保证不误拆。
7. **MusicBrainz 客户端放在 butler**（阶段 7 的补全也在 butler），遵守 1 req/s 与 User-Agent 要求，响应缓存在 `library.db`。测试只测响应解析，素材是真实录制的 MB 响应。
8. **不可逆损坏**（已变成 `???`）：从文件名/目录名解析出候选作为低置信度修正，同时在 `track_issues` 中标记 `needs_online`，留给阶段 7 的指纹补全。

## 影响

- 新迁移 `0011`：`correction_batches.kind`、`track_issues`、`mb_cache`。
- 乱码与艺人归一的准确率用测试语料度量：乱码规则层是确定性的，作为单元测试断言准确率；艺人归一涉及 LLM，误合并率用 `tools/eval` 下的评测工具对真实服务度量（不进 CI）。

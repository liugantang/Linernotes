# 0001 · ai 模块的分层与测试方式

> 2026-09-27 · 阶段 5

## 背景

阶段 5 新建 `ai` 模块（LLM 客户端、缓存、用量、批任务）。其中缓存（`llm_cache`，0003 已预留）、用量、批任务进度都需要持久化；原先的模块图把 `ai` 与 `library` 画在同一层，禁止横向依赖。

## 决定

1. **分层改为 `features → ai → library → core`**：`ai` 可以依赖 `library` 的数据库设施（`Database`、`Transaction`、迁移），把表放在 `library.db` 中，从而复用已有的迁移、WAL、每日备份。`library` 永远不依赖 `ai`。
2. **不引入 SDK**：自研 OpenAI Chat Completions 客户端（`QNetworkAccessManager` + 自写 SSE 解析）。
3. **JSON Schema 校验用 nlohmann/json + json-schema-validator**（系统包，MIT），只在 `ai` 的一个 .cpp 中使用，对外接口仍是 `QJsonValue`。
4. **测试不访问真实网络**：测试辅助库提供一个本地 HTTP 假服务（`QTcpServer`），按脚本返回 JSON 或 SSE 分块；录制回放（5.14）的 fixture 也由它回放。这样测试走的是真实的网络代码路径，产品代码中不需要可替换的传输层接口。

## 影响

- DEVELOPMENT 2.4 的模块图同步修改。
- `features/*` 只通过 `ai` 的统一入口调用 LLM。

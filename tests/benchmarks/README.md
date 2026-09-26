# 性能基准测试 (Benchmarks)

本目录用于存放性能基准测试（如扫描性能、音频指纹计算、乱码修复算法打分速度、曲库数据库批量读写与 FTS 搜索性能等）。

## 约定与规范

1. **执行方式**：基准测试不加入 `ctest` 默认运行流程，避免影响 CI 和日常单元测试速度。
2. **构建控制**：由 CMake 选项 `LINERNOTES_BUILD_BENCHMARKS`（默认为 `OFF`）进行控制。开启后才会编译本目录下的基准测试目标。
3. **测试编写**：
   - 基准测试程序为独立可执行文件（如 `linernotes-bench-library`）。
   - 基准测试应保持确定性（固定随机数种子），在相同硬件环境下多次运行具有可比性。

## 构建与运行

推荐使用 `release` 预设并开启基准测试选项：

```bash
# 配置与构建
cmake --preset release -DLINERNOTES_BUILD_BENCHMARKS=ON
cmake --build --preset release --target linernotes-bench-library -j 96

# 运行曲库性能基准（默认生成 100,000 首曲目并测算 FTS、浏览查询与单 track 更新延迟）
./build/release/tests/benchmarks/library/linernotes-bench-library --check

# 可选参数：
#   -n, --count <N>    指定生成的 track 数量（默认 100000）
#   --check            若任一 FTS 或浏览查询 p95 >= 100 ms 则以非零退出码退出
#   --keep             保留临时数据库目录以供 sqlite3 分析
#   -h, --help         显示帮助信息
```

# 第三方依赖清单

> 本项目所使用的全部第三方依赖库及其许可证、用途与最低版本说明。
> 遵循 GPL-3.0 兼容性要求（见 [DEVELOPMENT.md](DEVELOPMENT.md) 5.2）。

## 运行时与构建依赖

| 依赖名称 | 用途 | 许可证 | 最低版本 | 引入阶段 |
|---|---|---|---|---|
| **Qt 6** | 应用框架（Core、Gui、Quick、QuickControls2、Widgets、Sql、Network、DBus、Concurrent、Test、LinguistTools） | LGPL-3.0 / GPL-3.0 | 6.8 | 阶段 0 |
| **QtKeychain (Qt6)** | 系统安全钥匙串访问（安全存储 LLM API Key） | BSD-2-Clause / BSD-3-Clause | - | 阶段 0 |
| **mpv (libmpv)** | 音频解码与播放内核引擎 | LGPL-2.1-or-later / GPL-2.0-or-later | 2.0 | 阶段 1 |
| **TagLib** | 音频文件元数据（ID3v1/ID3v2、Vorbis Comment、APE 等）读取与写入 | LGPL-2.1-or-later / MPL-1.1 | 2.0 | 阶段 2 |
| **uchardet** | 文本编码检测（GBK/Big5/Shift-JIS 等乱码识别与修复） | MPL-1.1 / GPL-2.0-or-later / LGPL-2.1-or-later | - | 阶段 2 |
| **ICU** | Unicode 字符处理、国际化与文本规范化 | Unicode-3.0 / ICU License (MIT-style) | 70 | 阶段 2 |
| **libchromaprint** | 音频声学指纹提取 (AcoustID) | LGPL-2.1-or-later | 1.5 | 阶段 2 |
| **FFmpeg** | 音频格式解析与重采样（libavformat、libavcodec、libavutil、libswresample） | LGPL-2.1-or-later / GPL-2.0-or-later | - | 阶段 2 |
| **libebur128** | EBU R128 响度扫描与 ReplayGain 计算 | MIT | 1.2 | 阶段 2 |
| **SQLite3** | 嵌入式关系型数据库引擎 | Public Domain / Blessing | 3.40 | 阶段 2 |
| **nlohmann_json** | C++ 现代 JSON 解析与序列化库 | MIT | 3.11 | 阶段 5 |
| **json-schema-validator** | 基于 nlohmann/json 的 JSON Schema (draft-07) 校验器 | MIT | - | 阶段 5 |
| **ONNX Runtime** | 本地音频模型推理 | MIT | 1.29 | 阶段 9 |

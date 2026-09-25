# Linernotes

> **Linernotes** (named after the liner notes inside record sleeves that tell the stories behind the music) is an open-source, local-first desktop music player enhanced by AI. It does not replace your local music library, but empowers it to be clean, conversational, radio-hosted, insightful, and memorable. Built with C++20, Qt 6 (Qt Quick / QML), and libmpv.

---

**Linernotes** 是一款面向本地音乐收藏者的开源桌面音乐播放器。AI 不替代你的曲库，而是让曲库「**变干净、能对话、会主持、可解读、有记忆**」。

## 五大 AI 角色

- **管家 (Butler)**：整理曲库，提供乱码修复、艺人归一、多艺人拆分、声学指纹与元数据补全、版本与重复识别，所有修改经 diff 审核。
- **界面 (Interface / NLQ)**：自然语言交互查询曲库，支持按元数据、播放行为和声音特征检索，并可规划情绪与能量曲线歌单。
- **主持人 (Host / DJ)**：私人电台 DJ，结合上下文、天气与听歌习惯生成串场词，通过本地/云端 TTS 朗读并在切歌时混音串场。
- **向导 (Guide)**：深度探索与音乐解读，提供歌词逐句翻译、典故隐喻解释、艺人百科卡片与音乐史关联漫游。
- **档案员 (Archive)**：听觉日记与记忆沉淀，记录完整播放日志，提供听歌统计与叙事生成，支持歌曲「瞬间」生活记忆绑定。

## 产品原则

- **本地优先与隐私保护**：音频文件永不上传；仅在必要时发送文本元数据与聚合统计，且用户可完全关闭网络调用。
- **用户自带服务 (BYOK)**：软件不内置 API Key、不提供中转服务；用户自带任意兼容 OpenAI 接口的服务（支持云端 API 或 Ollama / llama.cpp 等本地模型）。
- **非破坏性与可解释**：写回文件为显式操作，所有 AI 决策均提供依据与可审核 diff，可随时撤销。
- **离线可降级**：断网或未配置 AI 服务时，Linernotes 仍是一款功能完整、轻快好用的本地音乐播放器。

## 当前状态

项目目前处于**开发早期（阶段 0：工程基建）**。

开发进度与近期规划请参见：[开发计划与路线图 (ROADMAP.md)](docs/ROADMAP.md)。

## 构建与运行

### 依赖环境

- **编译器**：支持 C++20 的编译器（GCC 13+ 或 Clang 16+）
- **构建工具**：CMake (>= 3.25)、Ninja
- **核心框架与库**：
  - Qt 6 (>= 6.8)：`Core`, `Gui`, `Quick`, `QuickControls2`, `Sql`, `Network`, `DBus`, `Concurrent`, `Test`
  - QtKeyChain (Qt6)
  - libmpv (>= 2.0)
  - TagLib (>= 2.0)
  - uchardet
  - ICU (icu-uc, icu-i18n >= 70)
  - Chromaprint (>= 1.5)
  - FFmpeg (libavformat, libavcodec, libavutil, libswresample)
  - libebur128 (>= 1.2)

#### Arch Linux 安装依赖

```bash
sudo pacman -S --needed \
    base-devel cmake ninja clang \
    qt6-base qt6-declarative qt6-tools \
    mpv taglib uchardet icu chromaprint ffmpeg libebur128 \
    qtkeychain-qt6 git
```

### 构建步骤

项目使用 CMake Presets 进行配置与构建：

```bash
# 1. 配置 Debug 构建
cmake --preset debug

# 2. 编译项目
cmake --build --preset debug

# 3. 运行测试
ctest --preset debug
```

也可以使用 Release 预设进行构建：

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

## 文档索引

- [需求规格说明 (REQUIREMENTS.md)](docs/REQUIREMENTS.md)：完整产品功能需求与设计规范
- [开发计划与路线图 (ROADMAP.md)](docs/ROADMAP.md)：分阶段实现路线与任务拆解
- [开发规范与指南 (DEVELOPMENT.md)](docs/DEVELOPMENT.md)：代码风格、架构边界、测试与提交规范
- [贡献指南 (CONTRIBUTING.md)](CONTRIBUTING.md)：参与项目开发、提交 Issue 与 PR 的流程

## 开源许可证

本项目基于 [GPL-3.0-or-later](LICENSE) 协议开源。

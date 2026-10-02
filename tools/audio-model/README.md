# MS-CLAP 音频编码器 ONNX 导出工具

本目录包含将 MS-CLAP 2023 音频编码器（AudioEncoder）导出为 ONNX 格式的 Python 脚本。

## 用途

MS-CLAP 2023 包含音频编码器（约 3300 万参数）和文本编码器。Linernotes 在阶段 9 中使用音频编码器为音频文件提取 1024 维音频特征向量（一首歌一个向量），供人声/能量/乐器分析、相似度计算与智能推荐使用。

导出的 ONNX 模型直接接受原始 PCM 音频波形输入（44.1 kHz 单声道，7 秒窗口，308,700 个采样点），输出未归一化的 1024 维特征向量。模型内部包含梅尔频谱等特征提取层（通过 torchlibrosa 卷积实现），因此 C++ 端无需自行实现梅尔频谱转换。

## 依赖

脚本需要项目根目录虚拟环境（`.venv`）中的 Python 依赖包以及系统 `ffmpeg`（用于 `--check` 和 `--ref` 音频解码）：

- `msclap`
- `torch`
- `onnx`
- `onnxruntime` / `onnxruntime-gpu`
- `numpy`
- `ffmpeg`（系统可执行文件）

## 命令示例

```bash
# 1. 导出 ONNX 模型并进行随机输入自检
.venv/bin/python tools/audio-model/export_msclap.py --out models/msclap_audio.onnx

# 2. 导出并在指定音频样本上校验 PyTorch 与 ONNX Runtime 输出余弦相似度
.venv/bin/python tools/audio-model/export_msclap.py \
    --out models/msclap_audio.onnx \
    --check tests/fixtures/audio/sample1.mp3 tests/fixtures/audio/sample2.flac

# 3. 导出同时生成 C++ 验证对比用的 JSON 参考向量（L2 归一化）
.venv/bin/python tools/audio-model/export_msclap.py \
    --out models/msclap_audio.onnx \
    --check tests/fixtures/audio/sample1.mp3 \
    --ref /tmp/msclap_ref.json
```

## 在 Linernotes 中使用

导出 ONNX 模型后，可以在配置 CMake 时传入模型路径：

```bash
cmake -B build -DLINERNOTES_AUDIO_MODEL=/path/to/models/msclap_audio.onnx
```

配置后，构建时会将模型文件复制到程序输出目录的 `models/msclap-2023-audio.onnx` 下，安装时也会一同安装至 `${CMAKE_INSTALL_BINDIR}/models/msclap-2023-audio.onnx`。

## 模型许可证

- MS-CLAP 模型遵循 **MIT License**。
- 本目录不加入 CMake 构建系统。

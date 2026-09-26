# 测试素材规范 (Test Fixtures)

本目录用于存放自动化测试所需的测试素材（音频样本、标签样本、LLM 录制响应等）。

## 规范要求

根据 `docs/DEVELOPMENT.md` 第 4.5 节：

1. **音频素材尽量小**：音频片段尽量控制在几秒钟以内，仅用于验证解码、格式识别与元数据解析。
2. **版权与合规**：使用自行合成/生成的音频，或具有明确自由分发许可（如 CC0 / CC-BY / 公有领域）的素材，严禁提交受版权保护的完整音频文件。
3. **异常与边界样本**：真实曲库中的“怪样本”（如奇怪字符编码、损坏文件、非标元数据），只提取标签或最小复现片段，避免引入冗余数据。
4. **AI 录制数据**：LLM 录制回放文件存放在 `llm/` 子目录，文件中严禁包含真实 API Key 或用户敏感隐私数据。

## 素材来源登记表

| 文件名 / 路径 | 格式 / 类型 | 大小 / 时长 | 来源 / 授权 | 说明 |
| :--- | :--- | :--- | :--- | :--- |

| `audio/tone_440_1s.flac` | FLAC / 单声道 22050 Hz | ~16 KB / 1.0 s | `generate.sh` 自行合成，CC0 | 440 Hz 正弦测试音频 |
| `audio/tone_660_1s.flac` | FLAC / 单声道 22050 Hz | ~17 KB / 1.0 s | `generate.sh` 自行合成，CC0 | 660 Hz 正弦测试音频，用于 gapless 测试 |
| `audio/tone_880_1s.ogg` | OGG (Vorbis) / 单声道 22050 Hz | ~5 KB / 1.0 s | `generate.sh` 自行合成，CC0 | 880 Hz 正弦测试音频 |
| `audio/silence_5s.flac` | FLAC / 单声道 22050 Hz | ~9 KB / 5.0 s | `generate.sh` 自行合成，CC0 | 静音 5 秒测试音频，用于长时播放与导航测试 |
| `audio/corrupt.flac` | 损坏文件 / 文本 | ~360 B / 0 s | `generate.sh` 自行合成，CC0 | 非法音频数据样本，用于错误处理测试 |
| `library/mp3_id3v24_utf8.mp3` | MP3 / ID3v2.4 UTF-8 | ~10 KB / 1.0 s | `generate.py` 自行合成，CC0 | ID3v2.4 完整标签测试：中文标题、多值 ARTIST、ALBUMARTIST、ALBUM、DATE、TRACKNUMBER、DISCNUMBER、GENRE、COMPOSER、MusicBrainz IDs、ReplayGain、USLT 歌词、APIC 封面 |
| `library/mp3_id3v23_gbk.mp3` | MP3 / ID3v2.3 (GBK as Latin-1) | ~10 KB / 1.0 s | `generate.py` 自行合成，CC0 | 乱码样本（GBK）：TIT2/TPE1/TALB 填入 Latin-1 映射的 GBK 字节。<br/>- TITLE: `晚风里的歌`<br/>- ARTIST: `林晓风`<br/>- ALBUM: `山谷的回响` |
| `library/mp3_id3v23_shiftjis.mp3` | MP3 / ID3v2.3 (Shift-JIS as Latin-1) | ~10 KB / 1.0 s | `generate.py` 自行合成，CC0 | 乱码样本（Shift-JIS）：TIT2/TPE1/TALB 填入 Latin-1 映射的 Shift-JIS 字节。<br/>- TITLE: `雨の日の散歩`<br/>- ARTIST: `佐藤風花`<br/>- ALBUM: `静かな夜` |
| `library/mp3_id3v23_euckr.mp3` | MP3 / ID3v2.3 (EUC-KR as Latin-1) | ~10 KB / 1.0 s | `generate.py` 自行合成，CC0 | 乱码样本（EUC-KR）：TIT2/TPE1/TALB 填入 Latin-1 映射的 EUC-KR 字节。<br/>- TITLE: `새벽의 노래`<br/>- ARTIST: `김바람`<br/>- ALBUM: `도시의 꿈` |
| `library/mp3_id3v1_gbk.mp3` | MP3 / ID3v1 (GBK) | ~9 KB / 1.0 s | `generate.py` 自行合成，CC0 | 仅 ID3v1 乱码样本（GBK）：128 字节 TAG 块。<br/>- TITLE: `晚风里的歌`<br/>- ARTIST: `林晓风`<br/>- ALBUM: `山谷的回响`<br/>- DATE: `2023`<br/>- COMMENT: `虚构测试` |
| `library/mp3_id3v1_big5.mp3` | MP3 / ID3v1 (Big5) | ~9 KB / 1.0 s | `generate.py` 自行合成，CC0 | 仅 ID3v1 乱码样本（Big5）：128 字节 TAG 块。<br/>- TITLE: `晚風裡的歌`<br/>- ARTIST: `林曉風`<br/>- ALBUM: `山谷的迴響`<br/>- DATE: `2023`<br/>- COMMENT: `虛構測試` |
| `library/mp3_v1_and_v2.mp3` | MP3 / ID3v2.4 + ID3v1 | ~10 KB / 1.0 s | `generate.py` 自行合成，CC0 | 同时包含 ID3v2.4（TITLE: `晴空之下`, ARTIST: `云端漫步`, ALBUM: `远方的地平线`）与 ID3v1（TITLE: `Old Title V1`, ARTIST: `Old Artist V1`, ALBUM: `Old Album V1`, DATE: `2001`, COMMENT: `Old Comment`），用于验证多容器分别读取与优先级 |
| `library/flac_vorbis.flac` | FLAC / Vorbis comment | ~16 KB / 1.0 s | `generate.py` 自行合成，CC0 | FLAC Vorbis comment 完整标签：多值 ARTIST（`林晓风`、`夜行者`）、DATE、TRACKNUMBER=3、TRACKTOTAL=12、DISCNUMBER=1、GENRE、LYRICS、MUSICBRAINZ_*、REPLAYGAIN_*、FLAC PICTURE 封面 |
| `library/flac_no_tags.flac` | FLAC / 无标签 | ~8 KB / 1.0 s | `generate.py` 自行合成，CC0 | 无任何标签和封面的 FLAC 文件 |
| `library/ogg_vorbis.ogg` | OGG (Vorbis) | ~7 KB / 1.0 s | `generate.py` 自行合成，CC0 | OGG Vorbis comment 基本字段（TITLE: `森林的呼吸`, ARTIST: `风之子`, ALBUM: `绿色自然`, DATE: `2022-03-15`, TRACKNUMBER: 1） |
| `library/opus.opus` | Opus (Ogg) | ~11 KB / 1.0 s | `generate.py` 自行合成，CC0 | Opus + Vorbis comment 基本字段（TITLE: `数码回声`, ARTIST: `电波游侠`, ALBUM: `合成波浪`, DATE: `2024-01-01`, TRACKNUMBER: 2） |
| `library/m4a_aac.m4a` | MP4 / AAC | ~10 KB / 1.0 s | `generate.py` 自行合成，CC0 | AAC 编码 MP4 标签：©nam (`月光奏鸣`), ©ART (`夜色乐团`), aART (`夜色乐团`), ©alb (`静谧之夜`), ©day (`2023-11-11`), trkn (3,12), disk (1,2), ©gen (`Classical`), ©lyr (`月色如水流淌`), covr 封面 |
| `library/m4a_alac.m4a` | MP4 / ALAC | ~13 KB / 1.0 s | `generate.py` 自行合成，CC0 | ALAC (Apple Lossless 16-bit) 编码 MP4 标签，用于验证 codec (alac) 与 bitDepth (16) |
| `library/wav_id3.wav` | WAV / PCM 16-bit + ID3v2 | ~45 KB / 1.0 s | `generate.py` 自行合成，CC0 | WAV PCM 16-bit 22050 Hz，包含 ID3v2 块（TITLE: `声波漫游`, ARTIST: `脉冲乐队`, ALBUM: `模拟时代`, DATE: `2020-01-01`） |
| `library/wavpack_ape.wv` | WavPack / APEv2 | ~13 KB / 1.0 s | `generate.py` 自行合成，CC0 | WavPack 16-bit 22050 Hz + APEv2 标签（Title: `无损压缩之梦`, Artist: `音频极客`, Album: `极致保真`, Year: `2019`, Track: `5`） |
| `library/中文 文件名.flac` | FLAC / Vorbis comment | ~16 KB / 1.0 s | `generate.py` 自行合成，CC0 | 非 ASCII 且带空格的文件名，标题为日文（TITLE: `さくら咲く頃`, ARTIST: `花吹雪`, ALBUM: `春の歌`, DATE: `2022-04-01`） |
| `library/corrupt_truncated.mp3` | MP3 / 截断损坏 | 300 B / 0 s | `generate.py` 自行合成，CC0 | 截断仅保留前 300 字节的 MP3 损坏样本 |
| `library/corrupt_garbage.flac` | FLAC / 垃圾数据损坏 | 504 B / 0 s | `generate.py` 自行合成，CC0 | `fLaC` 开头后接伪随机垃圾字节的损坏样本 |
| `library/empty.mp3` | MP3 / 空文件 | 0 B / 0 s | `generate.py` 自行合成，CC0 | 0 字节空文件 |
| `library/not_audio.ogg` | 非音频 / 文本伪装 | 520 B / 0 s | `generate.py` 自行合成，CC0 | 纯文本重命名为 .ogg 的伪装文件 |

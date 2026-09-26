#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

"""
Dependencies:
    - Python 3.10+
    - mutagen >= 1.48
    - ffmpeg (compiled with libmp3lame, flac, libvorbis, libopus, aac, alac, wavpack)

Usage:
    python3 tests/fixtures/library/generate.py

This script generates synthetic audio test fixtures for library metadata and mojibake tests.
All files are generated directly into the directory containing this script.
All audio contents and metadata are fictional (CC0).
"""

import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path

import mutagen
from mutagen.apev2 import APEv2
from mutagen.flac import FLAC, Picture
from mutagen.id3 import (
    APIC,
    ID3,
    TALB,
    TCOM,
    TCON,
    TDRC,
    TIT2,
    TPOS,
    TPE1,
    TPE2,
    TRCK,
    TXXX,
    USLT,
)
from mutagen.mp4 import MP4, MP4Cover
from mutagen.oggopus import OggOpus
from mutagen.oggvorbis import OggVorbis
from mutagen.wave import WAVE
from mutagen.wavpack import WavPack


# Minimal 8x8 solid PNG image (79 bytes)
TINY_PNG_BYTES = (
    b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x08\x00\x00\x00\x08"
    b"\x08\x02\x00\x00\x00\x4b\x6d\x29\xdc\x00\x00\x00\x1bIDATx\x9cc\xfc"
    b"\xff\xff?\x03\x18\x18\x18\x18\x18\x18\x18\x18\x18\x18\x18\x18\x00\x00"
    b"\x96\x0c\x02\x01\x18\xdb\x9e\x7f\x00\x00\x00\x00IEND\xaeB`\x82"
)


def run_ffmpeg(args, output_path):
    cmd = [
        "ffmpeg",
        "-nostdin",
        "-y",
        "-f",
        "lavfi",
        "-i",
        "sine=frequency=440:sample_rate=22050",
        "-t",
        "1.0",
        "-fflags",
        "+bitexact",
        "-flags:a",
        "+bitexact",
        "-map_metadata",
        "-1",
    ] + args + [str(output_path)]
    subprocess.run(
        ["timeout", "10"] + cmd,
        check=True,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def make_id3v1_block(
    title_bytes: bytes,
    artist_bytes: bytes,
    album_bytes: bytes,
    year_bytes: bytes = b"2023",
    comment_bytes: bytes = b"",
) -> bytes:
    block = bytearray(128)
    block[0:3] = b"TAG"
    block[3 : 3 + min(30, len(title_bytes))] = title_bytes[:30]
    block[33 : 33 + min(30, len(artist_bytes))] = artist_bytes[:30]
    block[63 : 63 + min(30, len(album_bytes))] = album_bytes[:30]
    block[93 : 93 + min(4, len(year_bytes))] = year_bytes[:4]
    block[97 : 97 + min(30, len(comment_bytes))] = comment_bytes[:30]
    block[127] = 255  # Genre 255 = None
    return bytes(block)


def main():
    out_dir = Path(__file__).resolve().parent
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"Generating fixtures in {out_dir}...")

    # 1. mp3_id3v24_utf8.mp3
    p1 = out_dir / "mp3_id3v24_utf8.mp3"
    run_ffmpeg(["-c:a", "libmp3lame", "-b:a", "64k"], p1)
    id3 = ID3()
    id3.add(TIT2(encoding=3, text=["晨曦微光"]))
    id3.add(TPE1(encoding=3, text=["林晓风", "夜行者"]))
    id3.add(TPE2(encoding=3, text=["林晓风"]))
    id3.add(TALB(encoding=3, text=["山谷的回响"]))
    id3.add(TDRC(encoding=3, text=["2003-07-31"]))
    id3.add(TRCK(encoding=3, text=["3/12"]))
    id3.add(TPOS(encoding=3, text=["1/2"]))
    id3.add(TCON(encoding=3, text=["Folk"]))
    id3.add(TCOM(encoding=3, text=["林晓风"]))
    id3.add(TXXX(encoding=3, desc="MUSICBRAINZ_TRACKID", text=["c3898183-b787-43c2-bf72-888e0013b860"]))
    id3.add(TXXX(encoding=3, desc="MUSICBRAINZ_ALBUMID", text=["a1234567-b787-43c2-bf72-888e0013b861"]))
    id3.add(TXXX(encoding=3, desc="MUSICBRAINZ_ARTISTID", text=["b1234567-b787-43c2-bf72-888e0013b862"]))
    id3.add(TXXX(encoding=3, desc="REPLAYGAIN_TRACK_GAIN", text=["-6.50 dB"]))
    id3.add(TXXX(encoding=3, desc="REPLAYGAIN_TRACK_PEAK", text=["0.950000"]))
    id3.add(TXXX(encoding=3, desc="REPLAYGAIN_ALBUM_GAIN", text=["-5.20 dB"]))
    id3.add(TXXX(encoding=3, desc="REPLAYGAIN_ALBUM_PEAK", text=["0.980000"]))
    id3.add(USLT(encoding=3, lang="eng", desc="", text="风穿过山谷\n带走昨天的梦"))
    id3.add(APIC(encoding=3, mime="image/png", type=3, desc="Front Cover", data=TINY_PNG_BYTES))
    id3.save(str(p1), v2_version=4)

    # 2. mp3_id3v23_gbk.mp3
    p2 = out_dir / "mp3_id3v23_gbk.mp3"
    run_ffmpeg(["-c:a", "libmp3lame", "-b:a", "64k"], p2)
    id3 = ID3()
    id3.add(TIT2(encoding=0, text=["晚风里的歌".encode("gbk").decode("latin1")]))
    id3.add(TPE1(encoding=0, text=["林晓风".encode("gbk").decode("latin1")]))
    id3.add(TALB(encoding=0, text=["山谷的回响".encode("gbk").decode("latin1")]))
    id3.save(str(p2), v2_version=3)

    # 3. mp3_id3v23_shiftjis.mp3
    p3 = out_dir / "mp3_id3v23_shiftjis.mp3"
    run_ffmpeg(["-c:a", "libmp3lame", "-b:a", "64k"], p3)
    id3 = ID3()
    id3.add(TIT2(encoding=0, text=["雨の日の散歩".encode("shift_jis").decode("latin1")]))
    id3.add(TPE1(encoding=0, text=["佐藤風花".encode("shift_jis").decode("latin1")]))
    id3.add(TALB(encoding=0, text=["静かな夜".encode("shift_jis").decode("latin1")]))
    id3.save(str(p3), v2_version=3)

    # 4. mp3_id3v23_euckr.mp3
    p4 = out_dir / "mp3_id3v23_euckr.mp3"
    run_ffmpeg(["-c:a", "libmp3lame", "-b:a", "64k"], p4)
    id3 = ID3()
    id3.add(TIT2(encoding=0, text=["새벽의 노래".encode("euc-kr").decode("latin1")]))
    id3.add(TPE1(encoding=0, text=["김바람".encode("euc-kr").decode("latin1")]))
    id3.add(TALB(encoding=0, text=["도시의 꿈".encode("euc-kr").decode("latin1")]))
    id3.save(str(p4), v2_version=3)

    # 5. mp3_id3v1_gbk.mp3
    p5 = out_dir / "mp3_id3v1_gbk.mp3"
    run_ffmpeg(["-c:a", "libmp3lame", "-b:a", "64k"], p5)
    v1_gbk = make_id3v1_block(
        "晚风里的歌".encode("gbk"),
        "林晓风".encode("gbk"),
        "山谷的回响".encode("gbk"),
        b"2023",
        "虚构测试".encode("gbk"),
    )
    with open(p5, "ab") as f:
        f.write(v1_gbk)

    # 6. mp3_id3v1_big5.mp3
    p6 = out_dir / "mp3_id3v1_big5.mp3"
    run_ffmpeg(["-c:a", "libmp3lame", "-b:a", "64k"], p6)
    v1_big5 = make_id3v1_block(
        "晚風裡的歌".encode("big5"),
        "林曉風".encode("big5"),
        "山谷的迴響".encode("big5"),
        b"2023",
        "虛構測試".encode("big5"),
    )
    with open(p6, "ab") as f:
        f.write(v1_big5)

    # 7. mp3_v1_and_v2.mp3
    p7 = out_dir / "mp3_v1_and_v2.mp3"
    run_ffmpeg(["-c:a", "libmp3lame", "-b:a", "64k"], p7)
    id3 = ID3()
    id3.add(TIT2(encoding=3, text=["晴空之下"]))
    id3.add(TPE1(encoding=3, text=["云端漫步"]))
    id3.add(TALB(encoding=3, text=["远方的地平线"]))
    id3.save(str(p7), v2_version=4)
    v1_old = make_id3v1_block(
        b"Old Title V1",
        b"Old Artist V1",
        b"Old Album V1",
        b"2001",
        b"Old Comment",
    )
    with open(p7, "ab") as f:
        f.write(v1_old)

    # 8. flac_vorbis.flac
    p8 = out_dir / "flac_vorbis.flac"
    run_ffmpeg(["-c:a", "flac"], p8)
    flac = FLAC(str(p8))
    flac["TITLE"] = ["远方的星群"]
    flac["ARTIST"] = ["林晓风", "夜行者"]
    flac["ALBUMARTIST"] = ["林晓风"]
    flac["ALBUM"] = ["深空旅者"]
    flac["DATE"] = ["2021-05-20"]
    flac["TRACKNUMBER"] = ["3"]
    flac["TRACKTOTAL"] = ["12"]
    flac["DISCNUMBER"] = ["1"]
    flac["GENRE"] = ["Acoustic"]
    flac["LYRICS"] = ["星光落在海面上\n微风吹向远方"]
    flac["MUSICBRAINZ_TRACKID"] = ["c3898183-b787-43c2-bf72-888e0013b860"]
    flac["MUSICBRAINZ_ALBUMID"] = ["a1234567-b787-43c2-bf72-888e0013b861"]
    flac["MUSICBRAINZ_ARTISTID"] = ["b1234567-b787-43c2-bf72-888e0013b862"]
    flac["REPLAYGAIN_TRACK_GAIN"] = ["-6.50 dB"]
    flac["REPLAYGAIN_TRACK_PEAK"] = ["0.950000"]
    flac["REPLAYGAIN_ALBUM_GAIN"] = ["-5.20 dB"]
    flac["REPLAYGAIN_ALBUM_PEAK"] = ["0.980000"]
    pic = Picture()
    pic.type = 3
    pic.mime = "image/png"
    pic.desc = "Front Cover"
    pic.data = TINY_PNG_BYTES
    flac.add_picture(pic)
    flac.save()

    # 9. flac_no_tags.flac
    p9 = out_dir / "flac_no_tags.flac"
    run_ffmpeg(["-c:a", "flac"], p9)
    flac_empty = FLAC(str(p9))
    flac_empty.clear_pictures()
    flac_empty.delete()

    # 10. ogg_vorbis.ogg
    p10 = out_dir / "ogg_vorbis.ogg"
    run_ffmpeg(["-c:a", "libvorbis", "-b:a", "64k"], p10)
    ogg = OggVorbis(str(p10))
    ogg["TITLE"] = ["森林的呼吸"]
    ogg["ARTIST"] = ["风之子"]
    ogg["ALBUM"] = ["绿色自然"]
    ogg["DATE"] = ["2022-03-15"]
    ogg["TRACKNUMBER"] = ["1"]
    ogg.save()

    # 11. opus.opus
    p11 = out_dir / "opus.opus"
    cmd = [
        "ffmpeg",
        "-nostdin",
        "-y",
        "-f",
        "lavfi",
        "-i",
        "sine=frequency=440:sample_rate=48000",
        "-t",
        "1.0",
        "-fflags",
        "+bitexact",
        "-flags:a",
        "+bitexact",
        "-map_metadata",
        "-1",
        "-c:a",
        "libopus",
        "-b:a",
        "64k",
        str(p11),
    ]
    subprocess.run(
        ["timeout", "10"] + cmd,
        check=True,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    opus = OggOpus(str(p11))
    opus["TITLE"] = ["数码回声"]
    opus["ARTIST"] = ["电波游侠"]
    opus["ALBUM"] = ["合成波浪"]
    opus["DATE"] = ["2024-01-01"]
    opus["TRACKNUMBER"] = ["2"]
    opus.save()

    # 12. m4a_aac.m4a
    p12 = out_dir / "m4a_aac.m4a"
    run_ffmpeg(["-c:a", "aac", "-b:a", "64k"], p12)
    mp4_aac = MP4(str(p12))
    mp4_aac["\xa9nam"] = ["月光奏鸣"]
    mp4_aac["\xa9ART"] = ["夜色乐团"]
    mp4_aac["aART"] = ["夜色乐团"]
    mp4_aac["\xa9alb"] = ["静谧之夜"]
    mp4_aac["\xa9day"] = ["2023-11-11"]
    mp4_aac["trkn"] = [(3, 12)]
    mp4_aac["disk"] = [(1, 2)]
    mp4_aac["\xa9gen"] = ["Classical"]
    mp4_aac["\xa9lyr"] = ["月色如水流淌"]
    mp4_aac["covr"] = [MP4Cover(TINY_PNG_BYTES, imageformat=MP4Cover.FORMAT_PNG)]
    mp4_aac.save()

    # 13. m4a_alac.m4a
    p13 = out_dir / "m4a_alac.m4a"
    run_ffmpeg(["-c:a", "alac"], p13)
    mp4_alac = MP4(str(p13))
    mp4_alac["\xa9nam"] = ["无损的回忆"]
    mp4_alac["\xa9ART"] = ["声学研究"]
    mp4_alac["\xa9alb"] = ["纯净之声"]
    mp4_alac["\xa9day"] = ["2020-08-18"]
    mp4_alac.save()

    # 14. wav_id3.wav
    p14 = out_dir / "wav_id3.wav"
    run_ffmpeg(["-c:a", "pcm_s16le"], p14)
    wav = WAVE(str(p14))
    wav.add_tags()
    wav.tags.add(TIT2(encoding=3, text=["声波漫游"]))
    wav.tags.add(TPE1(encoding=3, text=["脉冲乐队"]))
    wav.tags.add(TALB(encoding=3, text=["模拟时代"]))
    wav.tags.add(TDRC(encoding=3, text=["2020-01-01"]))
    wav.save()

    # 15. wavpack_ape.wv
    p15 = out_dir / "wavpack_ape.wv"
    run_ffmpeg(["-c:a", "wavpack"], p15)
    wv = WavPack(str(p15))
    wv.add_tags()
    wv.tags["Title"] = "无损压缩之梦"
    wv.tags["Artist"] = "音频极客"
    wv.tags["Album"] = "极致保真"
    wv.tags["Year"] = "2019"
    wv.tags["Track"] = "5"
    wv.save()

    # 16. 中文 文件名.flac
    p16 = out_dir / "中文 文件名.flac"
    run_ffmpeg(["-c:a", "flac"], p16)
    flac_zh = FLAC(str(p16))
    flac_zh["TITLE"] = ["さくら咲く頃"]
    flac_zh["ARTIST"] = ["花吹雪"]
    flac_zh["ALBUM"] = ["春の歌"]
    flac_zh["DATE"] = ["2022-04-01"]
    flac_zh.save()

    # 17. corrupt_truncated.mp3
    p17 = out_dir / "corrupt_truncated.mp3"
    with open(p1, "rb") as f:
        head_data = f.read(300)
    with open(p17, "wb") as f:
        f.write(head_data)

    # 18. corrupt_garbage.flac
    p18 = out_dir / "corrupt_garbage.flac"
    rnd = random.Random(42)
    garbage_bytes = b"fLaC" + rnd.randbytes(500)
    with open(p18, "wb") as f:
        f.write(garbage_bytes)

    # 19. empty.mp3
    p19 = out_dir / "empty.mp3"
    with open(p19, "wb") as f:
        pass

    # 20. not_audio.ogg
    p20 = out_dir / "not_audio.ogg"
    with open(p20, "wb") as f:
        f.write(b"This is a text file and not a valid ogg audio file.\n" * 10)

    print("All fixtures generated successfully!")


if __name__ == "__main__":
    main()

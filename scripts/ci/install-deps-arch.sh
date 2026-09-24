#!/usr/bin/env bash
set -euo pipefail

pacman -Syu --noconfirm --needed \
    base-devel \
    cmake \
    ninja \
    clang \
    qt6-base \
    qt6-declarative \
    qt6-tools \
    mpv \
    taglib \
    uchardet \
    icu \
    chromaprint \
    ffmpeg \
    libebur128 \
    qtkeychain-qt6 \
    git

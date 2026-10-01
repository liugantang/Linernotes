# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

"""
Export MS-CLAP 2023 audio encoder to ONNX format.

Loads the MS-CLAP 2023 model, extracts its audio encoder, wraps it to accept
raw PCM audio [B, 308700] and return embeddings [B, 1024], exports to ONNX opset 17,
and validates ONNX Runtime outputs against PyTorch using cosine similarity.
Optionally generates reference embeddings for audio files in JSON format.
"""

import argparse
import json
import os
import subprocess
import sys
from typing import Dict, List

import numpy as np
import onnxruntime as ort
import torch
import msclap


class ClapAudioEncoderWrapper(torch.nn.Module):
    """Wraps MS-CLAP AudioEncoder to accept raw float PCM and return unnormalized embeddings."""

    def __init__(self, audio_encoder: torch.nn.Module) -> None:
        super().__init__()
        self.audio_encoder = audio_encoder

    def forward(self, pcm: torch.Tensor) -> torch.Tensor:
        """
        Forward pass.

        Args:
            pcm: Audio tensor of shape [B, 308700] (44.1 kHz, 7 seconds, mono).

        Returns:
            Embedding tensor of shape [B, 1024] (unnormalized).
        """
        out = self.audio_encoder(pcm)
        return out[0]


def load_audio_7s(file_path: str) -> np.ndarray:
    """
    Decodes audio to 44.1 kHz mono float32 and extracts a 7-second chunk centered at 50%.

    If the audio is shorter than 7 seconds (308,700 samples), it is padded with zeros at the end.
    """
    target_samples = 7 * 44100  # 308700
    cmd = [
        "ffmpeg",
        "-v",
        "error",
        "-i",
        file_path,
        "-f",
        "f32le",
        "-ac",
        "1",
        "-ar",
        "44100",
        "pipe:1",
    ]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    data = np.frombuffer(res.stdout, dtype=np.float32)
    total_samples = len(data)

    if total_samples < target_samples:
        padded = np.zeros(target_samples, dtype=np.float32)
        if total_samples > 0:
            padded[:total_samples] = data
        return padded

    start = (total_samples - target_samples) // 2
    return data[start : start + target_samples]


def cosine_similarity(a: np.ndarray, b: np.ndarray) -> float:
    """Computes cosine similarity between two 1D vectors."""
    norm_a = float(np.linalg.norm(a))
    norm_b = float(np.linalg.norm(b))
    if norm_a == 0.0 or norm_b == 0.0:
        return 0.0
    return float(np.dot(a, b) / (norm_a * norm_b))


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Export MS-CLAP 2023 audio encoder to ONNX and verify outputs."
    )
    parser.add_argument(
        "--out",
        type=str,
        required=True,
        help="Path to save the exported ONNX model.",
    )
    parser.add_argument(
        "--check",
        type=str,
        nargs="*",
        default=[],
        help="Audio files to run verification against.",
    )
    parser.add_argument(
        "--ref",
        type=str,
        default=None,
        help="Path to save reference JSON embeddings for checked audio files.",
    )

    args = parser.parse_args()

    out_path = os.path.abspath(args.out)
    out_dir = os.path.dirname(out_path)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    print("Loading MS-CLAP 2023 model (CPU)...")
    clap = msclap.CLAP(version="2023", use_cuda=False)
    audio_encoder = clap.clap.audio_encoder
    audio_encoder.eval()

    model = ClapAudioEncoderWrapper(audio_encoder)
    model.eval()

    print(f"Exporting ONNX model to {out_path}...")
    dummy_input = torch.zeros(1, 308700, dtype=torch.float32)
    torch.onnx.export(
        model,
        dummy_input,
        out_path,
        export_params=True,
        opset_version=17,
        do_constant_folding=True,
        input_names=["pcm"],
        output_names=["embedding"],
        # Fixed batch of 1: the traced HTSAT graph bakes in the batch size (batch 2 gave cosine 0.69).
        dynamo=False,  # TorchScript exporter: no onnxscript dependency, handles torchlibrosa ops
    )
    print("ONNX export completed.")

    print("Running self-check with ONNX Runtime...")
    session = ort.InferenceSession(out_path, providers=["CPUExecutionProvider"])

    all_passed = True

    # 1. Random input check
    rand_pcm = np.random.randn(1, 308700).astype(np.float32)
    with torch.no_grad():
        torch_rand_out = model(torch.from_numpy(rand_pcm)).numpy()
    ort_rand_out = session.run(["embedding"], {"pcm": rand_pcm})[0]

    for i in range(rand_pcm.shape[0]):
        sim = cosine_similarity(torch_rand_out[i], ort_rand_out[i])
        print(f"[Random input #{i + 1}] Cosine similarity: {sim:.6f}")
        if sim < 0.9999:
            print(f"  FAILED: Random input #{i + 1} cosine similarity {sim:.6f} < 0.9999", file=sys.stderr)
            all_passed = False

    # 2. Audio files check & reference generation
    ref_dict: Dict[str, List[float]] = {}
    for file_path in args.check:
        abs_path = os.path.abspath(file_path)
        print(f"Processing audio check: {file_path}...")
        try:
            pcm = load_audio_7s(file_path)
        except Exception as e:
            print(f"  FAILED to decode {file_path}: {e}", file=sys.stderr)
            all_passed = False
            continue

        pcm_batch = pcm[np.newaxis, :]
        with torch.no_grad():
            torch_out = model(torch.from_numpy(pcm_batch)).numpy()[0]
        ort_out = session.run(["embedding"], {"pcm": pcm_batch})[0][0]

        sim = cosine_similarity(torch_out, ort_out)
        print(f"[{file_path}] Cosine similarity: {sim:.6f}")
        if sim < 0.9999:
            print(f"  FAILED: {file_path} cosine similarity {sim:.6f} < 0.9999", file=sys.stderr)
            all_passed = False

        norm = float(np.linalg.norm(torch_out))
        normed = (torch_out / norm) if norm > 0.0 else torch_out
        ref_dict[abs_path] = [float(x) for x in normed]

    if args.ref and ref_dict:
        ref_path = os.path.abspath(args.ref)
        ref_dir = os.path.dirname(ref_path)
        if ref_dir:
            os.makedirs(ref_dir, exist_ok=True)
        with open(ref_path, "w", encoding="utf-8") as f:
            json.dump(ref_dict, f, indent=2)
        print(f"Saved reference embeddings ({len(ref_dict)} tracks) to {ref_path}")

    if not all_passed:
        print("Verification failed: cosine similarity below threshold 0.9999", file=sys.stderr)
        sys.exit(1)

    print("All checks passed successfully.")


if __name__ == "__main__":
    main()

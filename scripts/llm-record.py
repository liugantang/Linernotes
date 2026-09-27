#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

"""Record raw HTTP responses from real LLM services into fixture files for offline replay tests.

Usage:
    scripts/llm-record.py --base-url http://127.0.0.1:8080/v1 --request req.json \
        --out tests/fixtures/llm/xxx.json --description "..." [--service "llama.cpp b1234"]

API Key is read solely from the LINERNOTES_LLM_KEY environment variable.
"""

import argparse
import datetime
import json
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(
        description="Record real LLM HTTP responses as test fixtures."
    )
    parser.add_argument(
        "--base-url",
        required=True,
        help="Base URL of the LLM service (e.g. http://127.0.0.1:8080/v1)",
    )
    parser.add_argument(
        "--request",
        required=True,
        help="Path to request JSON file containing the ChatRequest body",
    )
    parser.add_argument(
        "--out",
        required=True,
        help="Output path for the fixture JSON file",
    )
    parser.add_argument(
        "--description",
        required=True,
        help="Description of the recorded fixture",
    )
    parser.add_argument(
        "--service",
        default="",
        help="Optional service identifier (e.g. 'llama.cpp b1234')",
    )
    return parser.parse_args()


def main():
    args = parse_args()

    req_path = Path(args.request)
    if not req_path.is_file():
        sys.stderr.write(f"Error: Request file '{req_path}' not found.\n")
        sys.exit(1)

    try:
        with open(req_path, "r", encoding="utf-8") as f:
            request_data = json.load(f)
    except json.JSONDecodeError as e:
        sys.stderr.write(f"Error: Failed to parse request JSON: {e}\n")
        sys.exit(1)

    api_key = os.environ.get("LINERNOTES_LLM_KEY", "").strip()

    model = request_data.get("model", "")
    stream = bool(request_data.get("stream", False))

    base_url = args.base_url.rstrip("/")
    url = f"{base_url}/chat/completions"

    headers = {
        "Content-Type": "application/json",
    }
    if stream:
        headers["Accept"] = "text/event-stream"
    if api_key:
        headers["Authorization"] = f"Bearer {api_key}"

    req_body = json.dumps(request_data).encode("utf-8")
    http_req = urllib.request.Request(url, data=req_body, headers=headers, method="POST")

    status = 200
    resp_headers = None
    body_bytes = b""

    try:
        with urllib.request.urlopen(http_req) as resp:
            status = resp.status
            resp_headers = resp.headers
            body_bytes = resp.read()
    except urllib.error.HTTPError as e:
        status = e.code
        resp_headers = e.headers
        body_bytes = e.read()
    except urllib.error.URLError as e:
        sys.stderr.write(f"Network error connecting to {url}: {e.reason}\n")
        sys.exit(1)

    content_type = "application/json"
    if resp_headers:
        content_type = resp_headers.get("Content-Type", "application/json")

    filtered_headers = []
    if resp_headers:
        retry_after = resp_headers.get("Retry-After")
        if retry_after is not None:
            filtered_headers.append(["Retry-After", str(retry_after)])

    body_str = body_bytes.decode("utf-8", errors="replace")
    chunks = []
    if stream or "text/event-stream" in content_type.lower():
        raw_events = re.split(r"(?:\r?\n){2,}", body_str.strip())
        for ev in raw_events:
            ev = ev.strip()
            if ev:
                chunks.append(ev + "\n\n")
    else:
        chunks = [body_str]

    fixture = {
        "format": 1,
        "source": "recorded",
        "description": args.description,
        "recordedAt": datetime.date.today().isoformat(),
        "service": args.service,
        "model": model,
        "request": request_data,
        "response": {
            "status": status,
            "contentType": content_type,
            "headers": filtered_headers,
            "chunks": chunks,
        },
    }

    serialized = json.dumps(fixture, indent=2, ensure_ascii=False) + "\n"

    # Security checks before writing out
    if api_key and api_key in serialized:
        sys.stderr.write("Security error: Fixture contains LINERNOTES_LLM_KEY value!\n")
        sys.exit(1)
    if re.search(r"sk-[A-Za-z0-9_-]{16,}", serialized):
        sys.stderr.write("Security error: Fixture contains an API key pattern (sk-...)!\n")
        sys.exit(1)
    if re.search(r"bearer\s+\S", serialized, re.IGNORECASE):
        sys.stderr.write("Security error: Fixture contains 'Bearer <token>' pattern!\n")
        sys.exit(1)
    if re.search(r'"authorization"', serialized, re.IGNORECASE):
        sys.stderr.write("Security error: Fixture contains 'authorization' header/field!\n")
        sys.exit(1)

    out_file = Path(args.out)
    out_file.parent.mkdir(parents=True, exist_ok=True)
    out_file.write_text(serialized, encoding="utf-8")
    print(f"Recorded fixture saved to {out_file}")


if __name__ == "__main__":
    main()

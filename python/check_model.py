"""Check the exact streaming model path used by Flare's village chief.

Run from the game checkout with:
    flare-engine/.venv/bin/python flare-engine/python/check_model.py
"""

import argparse
import os
import selectors
import subprocess
import sys
import time
from pathlib import Path


def run(question: str, timeout: float) -> int:
    bridge = Path(__file__).with_name("ai_village_chief.py")
    process = subprocess.Popen(
        [sys.executable, "-u", str(bridge)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        bufsize=0,
    )
    received_answer = False
    received_error = False
    finished = False
    thinking = False
    buffer = b""
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    try:
        process.stdin.write(b"Q\t" + question.encode("utf-8").hex().encode("ascii") + b"\n")
        process.stdin.flush()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and not finished:
            for _, _ in selector.select(max(0.0, min(0.5, deadline - time.monotonic()))):
                chunk = os.read(process.stdout.fileno(), 4096)
                if not chunk:
                    if buffer:
                        print("模型流中断：收到不完整的数据。", file=sys.stderr)
                    finished = True
                    break
                buffer += chunk
                while b"\n" in buffer:
                    raw, buffer = buffer.split(b"\n", 1)
                    if raw == b"T":
                        if not thinking:
                            print("[思考中]", flush=True)
                            thinking = True
                    elif raw.startswith(b"D\t"):
                        try:
                            part = bytes.fromhex(raw[2:].decode("ascii")).decode("utf-8")
                        except (UnicodeError, ValueError):
                            print("模型流包含无效文本。", file=sys.stderr)
                            received_error = True
                            continue
                        print(part, end="", flush=True)
                        received_answer = True
                    elif raw.startswith(b"E\t"):
                        error = bytes.fromhex(raw[2:].decode("ascii")).decode("utf-8", errors="replace")
                        print(f"\n模型调用失败：{error}", file=sys.stderr)
                        received_error = True
                    elif raw == b"F":
                        finished = True
                        break
        if not finished:
            print(f"\n模型请求超过 {timeout:g} 秒。", file=sys.stderr)
            return 1
        if received_error or not received_answer:
            if not received_error:
                print("模型没有返回回答。", file=sys.stderr)
            return 1
        print("\n\n模型流式对话可用。")
        return 0
    finally:
        selector.close()
        process.terminate()
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def main() -> int:
    parser = argparse.ArgumentParser(description="测试村长 AI 模型的流式对话")
    parser.add_argument("question", nargs="?", default="你好，村长。请用一句话介绍堕落港。")
    parser.add_argument("--timeout", type=float, default=30, help="最长等待秒数，默认 30")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout 必须大于 0")
    return run(args.question, args.timeout)


if __name__ == "__main__":
    raise SystemExit(main())

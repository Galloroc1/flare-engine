"""Optional live-model test with simulated engine replies, without changing a save."""
import json
import argparse
import os
from pathlib import Path
import selectors
import subprocess
import sys
import time

engine = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description="测试真实模型的村长任务工具和流式回复，不修改存档")
parser.add_argument("--clue", action="store_true", help="验证往事线索工具及隐藏任务接取")
arguments = parser.parse_args()
environment = dict(os.environ, FLARE_AI_SOURCE="game", FLARE_AI_LOG="/tmp/flare-chief-model-test.log")
process = subprocess.Popen([sys.executable, "-u", str(engine / "python/ai_village_chief.py")],
                           stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                           stderr=subprocess.DEVNULL, env=environment)
selector = selectors.DefaultSelector()
selector.register(process.stdout, selectors.EVENT_READ)
question = "请查阅任务列表，然后帮我接取清理村外威胁。"
if arguments.clue:
    question = "村长，聊聊你过去的失踪冒险者的往事吧。请先告诉我真实线索，再帮我接取对应的委托。"
process.stdin.write(b"Q\t" + question.encode().hex().encode() + b"\n")
process.stdin.flush()
operations, errors = [], []
chunks = 0
finished = False
buffer = b""
deadline = time.monotonic() + 55
discovered = False
try:
    while time.monotonic() < deadline and not finished:
        for _, _ in selector.select(0.5):
            data = os.read(process.stdout.fileno(), 4096)
            if not data:
                finished = True
                break
            buffer += data
            while b"\n" in buffer:
                raw, buffer = buffer.split(b"\n", 1)
                kind = raw[:1]
                if kind == b"U":
                    operation, quest_id = bytes.fromhex(raw[2:].decode()).decode().split("\t", 1)
                    operations.append(operation)
                    expected_id = "old_story" if arguments.clue else "goblins"
                    valid = operation == "list" or (operation == "accept" and quest_id == expected_id)
                    if arguments.clue and operation == "accept" and not discovered:
                        valid = False
                    if arguments.clue and operation == "discover" and quest_id == "past":
                        discovered = True
                        valid = True
                    reply = {"ok": valid, "message": "已接取" if operation == "accept" and valid else "任务列表",
                             "quests": [{"id": expected_id, "title": "村长的旧事" if arguments.clue else "清理村外威胁", "goal": "去堕落港洞穴入口附近的旧箱子找回信物" if arguments.clue else "击杀5只哥布林",
                                         "state": "进行中" if operation == "accept" and valid else "可接取",
                                         "current": 0, "target": 5, "reward_gold": 100, "reward_xp": 150}]}
                    if arguments.clue and not discovered:
                        reply["quests"] = []
                    if operation == "discover" and valid:
                        reply["message"] = "冒险者在堕落港洞穴失踪了，入口附近的旧箱子里可能有他的信物。"
                    process.stdin.write(b"R\t" + json.dumps(reply, ensure_ascii=False).encode().hex().encode() + b"\n")
                    process.stdin.flush()
                elif kind == b"D":
                    chunks += 1
                elif kind == b"E":
                    errors.append(bytes.fromhex(raw[2:].decode()).decode())
                elif kind == b"F":
                    finished = True
    print(json.dumps({"finished": finished, "tools": operations, "answer_chunks": chunks, "errors": errors}, ensure_ascii=False))
    required_query = "discover" if arguments.clue else "list"
    if not (finished and not errors and chunks and required_query in operations and "accept" in operations):
        raise SystemExit(1)
finally:
    selector.close()
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()

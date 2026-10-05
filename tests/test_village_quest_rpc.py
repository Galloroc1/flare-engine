"""Exercise the actual agent tool classes and game protocol without a model."""
import asyncio
import io
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from village_quest_tools import QuestRPC, make_quest_tools


class QuestProtocolTests(unittest.IsolatedAsyncioTestCase):
    async def test_tools_and_serialized_requests(self):
        events = []
        rpc = QuestRPC(lambda kind, value: events.append((kind, value)))
        tools = make_quest_tools(rpc)
        for tool_class, arguments, expected in zip(
            tools, [(), ("goblins",), ("goblins",), ("past",)],
            ["list\t", "accept\tgoblins", "submit\tgoblins", "discover\tpast"]
        ):
            await rpc.results.put('{"ok":true,"message":"成功"}')
            result = await tool_class.function(*arguments)
            self.assertTrue(json.loads(result)["ok"])
            self.assertEqual(events[-1], ("U", expected))
        before = len(events)
        self.assertFalse(json.loads(await rpc.call("reward", "goblins"))["ok"])
        self.assertFalse(json.loads(await rpc.call("accept", "x\nsubmit"))["ok"])
        rpc.failed = True
        self.assertFalse(json.loads(await rpc.call("submit", "goblins"))["ok"])
        self.assertEqual(len(events), before)

    async def test_input_routes_questions_and_results(self):
        rpc = QuestRPC(lambda *_: None)
        question = "村长，接取任务"
        response = '{"ok":true,"message":"已接取"}'
        lines = "malformed\nQ\tbadhex\nQ\t" + question.encode().hex()
        lines += "\nR\t" + response.encode().hex() + "\n"
        await rpc.read_input(io.StringIO(lines))
        self.assertEqual(await rpc.questions.get(), question)
        self.assertIsNone(await rpc.questions.get())
        self.assertEqual(await rpc.results.get(), response)
        self.assertFalse(json.loads(await rpc.results.get())["ok"])


if __name__ == "__main__":
    unittest.main()

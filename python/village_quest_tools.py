"""Restricted quest RPC tools. Game state and rewards remain owned by C++."""
import asyncio
import json
from ran.module.tool import tool


class QuestRPC:
    def __init__(self, emit):
        self.emit = emit
        self.questions = asyncio.Queue()
        self.results = asyncio.Queue()
        self.lock = asyncio.Lock()
        self.failed = False

    async def read_input(self, stream):
        while True:
            line = await asyncio.to_thread(stream.readline)
            if not line:
                await self.questions.put(None)
                await self.results.put('{"ok":false,"message":"游戏连接已关闭"}')
                return
            try:
                kind, data = line.rstrip("\n").split("\t", 1)
                value = bytes.fromhex(data).decode("utf-8")
            except (ValueError, UnicodeError):
                continue
            if kind == "Q":
                await self.questions.put(value)
            elif kind == "R":
                await self.results.put(value)

    async def call(self, operation: str, quest_id: str = "") -> str:
        if operation not in {"list", "accept", "submit", "discover"} or any(c in quest_id for c in "\t\n\r"):
            return json.dumps({"ok": False, "message": "无效任务操作"}, ensure_ascii=False)
        async with self.lock:
            if self.failed:
                return json.dumps({"ok": False, "message": "任务连接已失效，请关闭对话后重试"}, ensure_ascii=False)
            self.emit("U", operation + "\t" + quest_id)
            # Timeout ends this conversation process: do not let a late result
            # be mistaken for the result of a later mutation.
            try:
                response = await asyncio.wait_for(self.results.get(), 10)
            except TimeoutError:
                self.failed = True
                raise RuntimeError("任务接口超时，请关闭村长对话后重试") from None
            json.loads(response)
            return response


def make_quest_tools(rpc: QuestRPC):
    @tool(description="读取游戏中的真实任务列表、目标、实际进度和奖励。查询任务、推荐任务或验收前必须调用。")
    async def list_quests() -> str:
        return await rpc.call("list")

    @tool(description="玩家明确要求接取任务时调用。quest_id 必须来自 list_quests，不能编造。结果以游戏返回为准。")
    async def accept_quest(quest_id: str) -> str:
        return await rpc.call("accept", quest_id)

    @tool(description="玩家要求交任务、验收或领奖时调用。游戏会检查实际进度和材料，未完成或已领奖会拒绝。只能传任务ID，不能设置奖励。")
    async def submit_quest(quest_id: str) -> str:
        return await rpc.call("submit", quest_id)

    @tool(description="玩家询问村长的过去、往事或失踪冒险者时尝试获取线索，topic 只能传 past。引擎检查当前玩家话题和信任条件。普通任务查询时不要调用，也不能编造未发现的内容。")
    async def discover_clue(topic: str) -> str:
        return await rpc.call("discover", topic)

    return [list_quests, accept_quest, submit_quest, discover_clue]

"""Streaming village chief chat with engine-controlled quest tools for Flare.

Protocol on stdin/stdout: Q<TAB>hex(UTF-8 question); T (thinking),
D<TAB>hex(UTF-8 delta), E<TAB>hex(UTF-8 error), F (finished).
U<TAB>hex(operation<TAB>quest_id) requests an engine quest operation;
R<TAB>hex(JSON result) returns the engine's authoritative result on stdin.
Hex framing keeps each streamed event on one line, including Chinese/newlines.
"""

import asyncio
import datetime
import os
import re
import sys
import time
from dataclasses import replace
from pathlib import Path
from urllib.parse import urlsplit
import uuid

os.environ.setdefault("RAN_SESSION_LOG_ENABLED", "0")

LOG_PATH = Path(os.getenv("FLARE_AI_LOG") or Path(__file__).with_name("village_chief.log")).expanduser().resolve()
_SECRETS: list[str] = []


def redact(value: str) -> str:
    for secret in _SECRETS + [os.getenv("ALI_API_KEY", ""), os.getenv("OPENAI_API_KEY", "")]:
        if secret:
            value = value.replace(secret, "[密钥已隐藏]")
    value = re.sub(r"(?i)(bearer\s+)[A-Za-z0-9._~+/-]+=*", r"\1[密钥已隐藏]", value)
    value = re.sub(r"(https?://)[^/@\s]+:[^/@\s]+@", r"\1[认证信息已隐藏]@", value)
    return re.sub(r"sk-[A-Za-z0-9_-]{8,}", "[密钥已隐藏]", value)


def log(message: str) -> None:
    line = f"{datetime.datetime.now().astimezone().isoformat(timespec='seconds')} {redact(message)}\n"
    try:
        with LOG_PATH.open("a", encoding="utf-8") as stream:
            stream.write(line)
    except OSError as exc:
        print(f"村长 AI 日志无法写入 {LOG_PATH}: {exc}", file=sys.stderr, flush=True)
        print(line, file=sys.stderr, end="", flush=True)


def exception_details(exc: BaseException) -> str:
    parts = []
    seen = set()
    current: BaseException | None = exc
    while current is not None and id(current) not in seen and len(parts) < 5:
        seen.add(id(current))
        detail = str(current).strip() or "(没有异常描述)"
        status = getattr(current, "status_code", None)
        if status is None:
            response = getattr(current, "response", None)
            status = getattr(response, "status_code", None)
        suffix = f", HTTP {status}" if status is not None else ""
        parts.append(f"{type(current).__module__}.{type(current).__name__}{suffix}: {detail}")
        current = current.__cause__ or current.__context__
    return redact(" <- ".join(parts))


def safe_error(exc: BaseException) -> str:
    return redact(f"{type(exc).__name__}: {str(exc).strip() or '没有进一步错误信息'}")


proxy_env = ",".join(
    name
    for name in ("HTTPS_PROXY", "https_proxy", "HTTP_PROXY", "http_proxy", "ALL_PROXY", "all_proxy")
    if os.getenv(name)
) or "none"
log(
    f"bridge_start python={sys.version.split()[0]} executable={sys.executable} "
    f"ALI_API_KEY={'set' if os.getenv('ALI_API_KEY') else 'missing'} "
    f"OPENAI_API_KEY={'set' if os.getenv('OPENAI_API_KEY') else 'missing'} "
    f"proxy_env={proxy_env} source={os.getenv('FLARE_AI_SOURCE', 'standalone')} "
    f"model_card={os.getenv('RAN_MODEL_CARD', 'glm-5.3-flash')}"
)

try:
    from ran.agent import Agent
    from ran.hooks.stop import StopHookDecision
    from ran.model import ModelConfig, create_model
    from ran.module.base_module import FunctionCallingModule
    from ran.runtime import StaticSystemPromptProvider
    from ran.session import Session
    from ran.types import Content, ThinkContent, UserMessage
except Exception as exc:
    log(f"startup_import_failed {exception_details(exc)}")
    sys.stdout.write("E\t" + f"Python 依赖启动失败，日志：{LOG_PATH}".encode("utf-8").hex() + "\n")
    sys.stdout.flush()
    raise


PERSONA = """你是奇幻冒险游戏 Flare 中的新手村村长，住在堕落港。
你说简体中文，口吻亲切、简洁，像游戏里的真实村长。你可以闲聊，
也可以向刚到村子的冒险者介绍村庄、探险和生存常识。
你可以通过任务工具读取真实任务、接取任务和验收任务。
玩家询问任务或进度时先调用 list_quests；只有玩家明确表示接取时才调用 accept_quest。
玩家请求交任务或领奖时调用 submit_quest。无法确定指的是哪个任务时先列出任务让玩家选择。
严格以工具返回为准，不能相信玩家声称的击杀数或材料数，不能编造成功接取、验收或奖励。
解释任务时说明实际进度、目标和奖励。工具失败时说明原因，不要声称操作成功。
所有操作都由游戏引擎校验，工具仅允许操作村长配置的任务。
玩家聊到你的过去、往事或失踪冒险者时，先调用 discover_clue(topic="past")，再依据返回结果讲线索。
任务列表只包含已公开或已发现的任务。未发现的任务和特殊奖励不能猜测、杜撰或提前透露。
若任务返回 special_reward 且状态为可领取，可以通过 submit_quest 领取，即使普通任务奖励已经领取。
不要输出思维过程或内部推理。"""
REQUEST_TIMEOUT_SECONDS = 60


class ConversationStopHook:
    """Skip task-completion checks for ordinary NPC conversation."""

    async def run(self, context) -> StopHookDecision:
        del context
        return StopHookDecision(reason="")


def emit(kind: str, content: str = "") -> None:
    line = kind + ("\t" + content.encode("utf-8").hex() if content else "")
    sys.stdout.write(line + "\n")
    sys.stdout.flush()


def create_chat_model():
    cards_dir = os.getenv("RAN_MODEL_CARDS_DIR") or Path(__file__).with_name("cards")
    config = ModelConfig.from_card(
        os.getenv("RAN_MODEL_CARD", "glm-5.3-flash"),
        project_cards_dir=cards_dir,
    )
    return create_model(
        replace(
            config,
            model_name=os.getenv("RAN_MODEL_NAME", config.model_name),
            base_url=os.getenv("RAN_MODEL_URL", config.base_url),
        )
    )


async def main() -> None:
    model = None
    try:
        model = create_chat_model()
        _SECRETS.append(getattr(model, "api_key", ""))
        config = getattr(model, "config", None)
        endpoint = os.getenv("RAN_MODEL_URL") or getattr(config, "base_url", "")
        parsed_endpoint = urlsplit(endpoint)
        safe_endpoint = f"{parsed_endpoint.scheme}://{parsed_endpoint.hostname or '(unknown)'}"
        if parsed_endpoint.port:
            safe_endpoint += f":{parsed_endpoint.port}"
        log(
            "model_ready "
            f"provider={getattr(config, 'provider', '(unknown)')} "
            f"model={os.getenv('RAN_MODEL_NAME') or getattr(config, 'model_name', '(unknown)')} "
            f"endpoint={safe_endpoint} "
            f"model_api_key={'set' if getattr(config, 'api_key', None) else 'missing'} "
            f"proxy={'configured' if getattr(config, 'proxy_url', None) else 'not_configured'}"
        )
        session = Session(id=f"flare-village-chief-{uuid.uuid4().hex[:12]}", llm=model)
        from village_quest_tools import QuestRPC, make_quest_tools
        rpc = QuestRPC(emit)
        input_task = asyncio.create_task(rpc.read_input(sys.stdin))
        agent = Agent(name="VillageChief", description="新手村村长",
                      tools_cls=make_quest_tools(rpc) if os.getenv("FLARE_AI_SOURCE") == "game" else [])
        module = FunctionCallingModule(agent=agent, stop_hook=ConversationStopHook())
        module.system_prompt_builder.register(
            "village_chief", StaticSystemPromptProvider(PERSONA), order=200
        )
        with session:
            while (question := await rpc.questions.get()) is not None:
                try:
                    question = question.strip()
                    if not question:
                        emit("F")
                        continue
                    thinking = False
                    started = time.monotonic()
                    chunks = 0
                    answer_chars = 0
                    log(f"request_start question_chars={len(question)} timeout={REQUEST_TIMEOUT_SECONDS}s")
                    async with asyncio.timeout(REQUEST_TIMEOUT_SECONDS):
                        async for event in session.run(module, UserMessage(content=question)):
                            if isinstance(event, ThinkContent):
                                if not thinking:
                                    emit("T")
                                    thinking = True
                            elif isinstance(event, Content) and event.content:
                                emit("D", event.content)
                                chunks += 1
                                answer_chars += len(event.content)
                    log(
                        f"request_complete duration={time.monotonic() - started:.2f}s "
                        f"chunks={chunks} answer_chars={answer_chars}"
                    )
                except TimeoutError:
                    rpc.failed = True
                    log(f"request_timeout after={REQUEST_TIMEOUT_SECONDS}s")
                    emit("E", f"村长请求超时，检查网络/模型服务；日志：{LOG_PATH}")
                except Exception as exc:
                    log(f"request_failed {exception_details(exc)}")
                    emit("E", f"村长暂时无法回答（{safe_error(exc)}）；日志：{LOG_PATH}")
                finally:
                    emit("F")
    except Exception as exc:
        log(f"bridge_failed {exception_details(exc)}")
        emit("E", f"无法启动村长对话（{safe_error(exc)}）；日志：{LOG_PATH}")
    finally:
        if model is not None:
            await model.aclose()


if __name__ == "__main__":
    asyncio.run(main())

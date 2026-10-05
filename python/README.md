# Village chief AI

The game uses the `agent` project from
`git@github.com:Galloroc1/agent.git` through the dependency in
`flare-engine/pyproject.toml`.

Install its Python environment from `flare-engine`:

```sh
uv sync
```

Set `ALI_API_KEY` in the environment used to launch Flare. When launching from
Finder, put it in `flare-engine/.env` (this file is git-ignored):

```sh
ALI_API_KEY=your_key_here
```

The desktop launcher loads this file and reports a clear error if no key is
available. The default model card is `python/cards/glm-5.3-flash.toml`. `RAN_MODEL_CARD`,
`RAN_MODEL_CARDS_DIR`, `RAN_MODEL_NAME`, and `RAN_MODEL_URL` can override the
model selection and endpoint.

Test the same streaming path used by the NPC:

```sh
uv run python python/check_model.py
```

The checker prints answer chunks as they arrive and only displays a thinking
indicator. It exits with a nonzero status when the model request fails or
times out. Session logs are disabled for NPC chat unless
`RAN_SESSION_LOG_ENABLED=1` is explicitly set.

Bridge diagnostics are written to `python/village_chief.log` (or the path in
`FLARE_AI_LOG`). The log records key presence, endpoint host, proxy-variable
presence, request duration, and exception causes; it never records API key
values or conversation text.

## Village-chief quests

The chief in Perdition Harbor supports listing, accepting and turning in quests.
Ask “有什么任务”, “接取清理村外威胁”, or “提交村庄补给”. The three buttons
at the bottom also accept, show progress or turn in each quest, including when
the model is unavailable. Accepted quests appear in the regular quest journal.

| Quest | Objective | Reward |
| --- | --- | --- |
| 清理村外威胁 | Kill 5 goblins after accepting | 100 gold, 150 XP |
| 村庄补给 | Turn in 3 Aloe Vera (item 751) | 80 gold, 100 XP |
| 初试暗影 | Clear Shadow Trial floor 3 after accepting | 200 gold, 300 XP |

Definitions live in `flare-game/mods/empyrean_campaign/engine/village_quests.txt`.
C++ owns progress, checks materials, removes submitted items and awards rewards.
Each quest is rewarded once per character. State follows normal character saves;
AI conversation history is not used as proof of completion.

Python exposes only `list_quests`, `accept_quest(quest_id)` and
`submit_quest(quest_id)`, plus the restricted `discover_clue(topic)` lore tool.
Tool requests and engine results are recorded in the
bridge diagnostic log; the model cannot choose reward amounts.

Run the protocol and real-engine integration tests from `flare-engine`:

```sh
.venv/bin/python tests/test_village_quest_rpc.py
python3 tests/run_village_quests.py
```

The engine test uses temporary save/config directories. It writes diagnostics
and a rendered panel snapshot to `build/village-quests-test.log` and
`build/village-chief-quests.bmp`.

With `ALI_API_KEY` configured, verify live model tool selection and streaming:

```sh
.venv/bin/python tests/check_village_quest_model.py
```

This optional check supplies simulated engine replies and does not alter saves.

## Hidden content

Undiscovered quests are omitted from engine JSON, dialog summaries and quest
buttons. A hidden quest's name, objectives and rewards only become visible
after discovery; it still needs an explicit acceptance.

“村长的旧事” requires turning in “清理村外威胁”, then asking about the chief's
past, old stories or missing adventurers. The engine also checks the current
player message when the model calls `discover_clue("past")`. The “聊聊往事”
button provides the same interaction if the model is unavailable. Once accepted,
a chest near the Perdition Harbor Cave entrance grants a keepsake (1150).
Turning it in awards 150 gold, 200 XP and 守望之戒 (1700, +40 HP, +5 accuracy).

After accepting 初试暗影, start a new three-floor run and finish it without
losing HP. Shields that absorb all damage do not invalidate the run. Damage,
damage over time, negative HP regeneration and death do; healing cannot undo
it. Leaving the trial ends that attempt. Damage and floor sequence state follow
normal character saves. Starting a new run resets attempt state.

Only after qualifying does the engine reveal 无瑕暗影坠饰 (1701, +60 HP,
+5 critical chance, +8 accuracy). Submit 初试暗影 to collect it once per
character. Characters that already claimed the normal trial reward can earn
and claim this extra reward on a later qualifying run, without receiving normal
gold/XP again. Until earned, the extra reward never appears in model quest JSON.

Verify the model's discovery flow with simulated engine replies:

```sh
.venv/bin/python tests/check_village_quest_model.py --clue
```

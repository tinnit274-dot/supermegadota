import json, shutil, os
from collections import Counter

actions_file = r"C:\dotabot\build\demos\demo_20260726_021957\actions.jsonl"
backup = actions_file + ".backup"

if not os.path.exists(backup):
    shutil.copy(actions_file, backup)

with open(actions_file, "r", encoding="utf-8") as f:
    actions = [json.loads(l) for l in f if l.strip()]

fixed = []
prev_mx, prev_my = 0, 0

for i, act in enumerate(actions):
    keys = act.get("keys", [])
    mx = act.get("mx", 0)
    my = act.get("my", 0)
    action = "none"
    ability = -1

    if 65 in keys: action = "attack"
    elif 77 in keys or 81 in keys: action = "move"
    elif any(k in keys for k in [90, 88, 67, 69, 70, 82]):
        action = "ability"
        for idx, k in enumerate([90, 88, 67, 69, 70, 82]):
            if k in keys: ability = idx; break
    elif any(k in keys for k in [68, 87, 86, 20, 66]):
        action = "item"
        for idx, k in enumerate([68, 87, 86, 20, 66]):
            if k in keys: ability = idx; break

    if action == "none" and i > 0:
        if abs(mx - prev_mx) > 15 or abs(my - prev_my) > 15:
            action = "move"

    prev_mx, prev_my = mx, my
    act["action"] = action
    act["ability"] = ability
    act["tx"] = act.get("tx", mx)
    act["ty"] = act.get("ty", my)
    act["camx"] = act.get("camx", 0.5)
    act["camy"] = act.get("camy", 0.5)
    fixed.append(act)

with open(actions_file, "w", encoding="utf-8") as f:
    for act in fixed:
        f.write(json.dumps(act, ensure_ascii=False) + "\n")

print("Fixed! Distribution:", Counter(a["action"] for a in fixed))

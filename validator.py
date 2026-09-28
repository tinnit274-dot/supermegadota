"""Local health check for the DotaBot installation."""
import json
import os
import glob
from collections import Counter


ROOT = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(ROOT, "build")
MODELS = os.path.join(ROOT, "models")
DEMOS = os.path.join(ROOT, "demos")


def check(label, condition, warn=False):
    state = "OK" if condition else ("WARN" if warn else "FAIL")
    print(f"[{state}] {label}")
    return condition


print("=" * 60)
print("DOTABOT INSTALLATION CHECK — 2026-09-27")
print("=" * 60)

exe = os.path.join(BUILD, "DotaBot.exe")
check("DotaBot.exe exists", os.path.exists(exe))
check("ONNX Runtime DLL exists",
      os.path.exists(os.path.join(BUILD, "onnxruntime.dll")))
check("GSI config exists",
      os.path.exists(os.path.join(ROOT, "data", "gamestate_integration_bot.cfg")))
check("Templates directory exists",
      os.path.isdir(os.path.join(ROOT, "templates")))

demo_dirs = sorted(glob.glob(os.path.join(DEMOS, "demo_*")))
if not demo_dirs:
    check("At least one training demo", False, warn=True)
else:
    total = 0
    distribution = Counter()
    for demo in demo_dirs:
        actions_path = os.path.join(demo, "actions.jsonl")
        frames = glob.glob(os.path.join(demo, "frames", "*.jpg"))
        actions = []
        if os.path.exists(actions_path):
            with open(actions_path, encoding="utf-8") as handle:
                actions = [json.loads(line) for line in handle if line.strip()]
        total += len(actions)
        distribution.update(row.get("action", "none") for row in actions)
        check(f"{os.path.basename(demo)} has frames", bool(frames), warn=True)
        check(f"{os.path.basename(demo)} has actions", bool(actions), warn=True)
    check(f"Training samples: {total}", total > 0)
    print(f"Action distribution: {dict(distribution)}")

model = os.path.join(MODELS, "model.onnx")
check("models/model.onnx", os.path.exists(model), warn=True)
if os.path.exists(model):
    try:
        import onnx
        graph = onnx.load(model)
        output_shape = graph.graph.output[0].type.tensor_type.shape
        check("Model output format is 13 values",
              bool(output_shape.dim and output_shape.dim[-1].dim_value == 13))
    except Exception as exc:
        check(f"ONNX inspection: {exc}", False)

try:
    import torch
    print(f"PyTorch: {torch.__version__}; CUDA: {torch.cuda.is_available()}")
except Exception as exc:
    check(f"PyTorch import: {exc}", False)

try:
    import onnxruntime as ort
    print(f"ONNX Runtime device: {ort.get_device()}")
except Exception as exc:
    check(f"ONNX Runtime import: {exc}", False)

print("=" * 60)

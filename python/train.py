"""Train DotaBot from recorded demonstrations.

The exported model is intentionally small and deterministic:
  output[0:5]   action logits (none, move, attack, ability, item)
  output[5:7]   target x/y in [0, 1]
  output[7:13]  ability/item slot logits

This format is consumed by the native C++ runtime in BrainEngine.cpp.
"""
import argparse
import json
import os
import random
import shutil
from bisect import bisect_left
from collections import Counter

import cv2
import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, Dataset
from torchvision import models, transforms
from tqdm import tqdm

IMG_SIZE = 224
GSI_DIM = 69
ACTION_MAP = {"none": 0, "move": 1, "attack": 2, "ability": 3, "item": 4}


def clamp01(value):
    return float(np.clip(value, 0.0, 1.0))


def unwrap_gsi(payload):
    if isinstance(payload, dict) and isinstance(payload.get("data"), dict):
        return payload["data"]
    return payload if isinstance(payload, dict) else {}


def number(obj, key, default=0.0):
    value = obj.get(key, default) if isinstance(obj, dict) else default
    return float(value) if isinstance(value, (int, float)) else float(default)


def encode_gsi(raw):
    """Keep this feature layout in lockstep with BrainEngine::EncodeGSI."""
    raw = unwrap_gsi(raw)
    hero = raw.get("hero", {}) if isinstance(raw.get("hero"), dict) else {}
    player = raw.get("player", {}) if isinstance(raw.get("player"), dict) else {}
    game_map = raw.get("map", {}) if isinstance(raw.get("map"), dict) else {}
    vec = np.zeros(GSI_DIM, dtype=np.float32)
    max_health = max(number(hero, "max_health", 1), 1.0)
    max_mana = max(number(hero, "max_mana", 1), 1.0)
    vec[0] = clamp01(number(hero, "health") / max_health)
    vec[1] = clamp01(number(hero, "mana") / max_mana)
    vec[2] = clamp01(number(player, "gold") / 10000.0)
    vec[3] = clamp01(number(hero, "level") / 30.0)
    vec[4] = 1.0 if hero.get("alive", True) else 0.0
    vec[5] = clamp01(number(hero, "respawn_seconds") / 100.0)

    abilities = raw.get("abilities", {})
    if not isinstance(abilities, dict):
        abilities = {}
    for i, ability in enumerate(list(abilities.values())[:6]):
        if not isinstance(ability, dict):
            continue
        vec[6 + i * 2] = 1.0 if ability.get("can_cast", False) else 0.0
        vec[7 + i * 2] = clamp01(number(ability, "cooldown") / 100.0)

    items = raw.get("items", {})
    if not isinstance(items, dict):
        items = {}
    for i, item in enumerate(list(items.values())[:6]):
        vec[18 + i] = 1.0 if isinstance(item, dict) and item.get("name") else 0.0

    vec[24] = clamp01(number(game_map, "game_time") / 3600.0)
    vec[25] = clamp01(number(game_map, "clock_time") / 3600.0)
    vec[26] = clamp01(number(player, "kills") / 50.0)
    vec[27] = clamp01(number(player, "deaths") / 50.0)
    vec[28] = clamp01(number(player, "assists") / 50.0)
    vec[29] = clamp01(number(player, "last_hits") / 500.0)
    vec[30] = clamp01(number(player, "denies") / 100.0)
    vec[31] = float(np.clip(number(hero, "x") / 10000.0, -1.0, 1.0))
    vec[32] = float(np.clip(number(hero, "y") / 10000.0, -1.0, 1.0))
    vec[33] = 1.0
    return vec


def read_jsonl(path):
    if not os.path.exists(path):
        return []
    rows = []
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            try:
                value = json.loads(line)
                if isinstance(value, dict):
                    rows.append(value)
            except json.JSONDecodeError:
                continue
    return rows


def locate_demo_dirs(root):
    root = os.path.abspath(root)
    if os.path.isfile(os.path.join(root, "actions.jsonl")):
        return [root]
    if not os.path.isdir(root):
        return []
    return sorted(
        os.path.join(root, name)
        for name in os.listdir(root)
        if os.path.isfile(os.path.join(root, name, "actions.jsonl"))
    )


def gsi_for_action(action, gsi_rows, gsi_times):
    if not gsi_rows:
        return {}
    timestamp = action.get("ts")
    if not isinstance(timestamp, (int, float)) or not gsi_times:
        return unwrap_gsi(gsi_rows[min(len(gsi_rows) - 1, action.get("frame", 0))])
    pos = bisect_left(gsi_times, timestamp)
    candidates = []
    if pos < len(gsi_rows):
        candidates.append(pos)
    if pos > 0:
        candidates.append(pos - 1)
    best = min(candidates, key=lambda i: abs(gsi_times[i] - timestamp))
    return unwrap_gsi(gsi_rows[best])


class DemoDataset(Dataset):
    def __init__(self, demo_dirs, samples=None):
        self.items = []
        for demo_dir in demo_dirs:
            actions = read_jsonl(os.path.join(demo_dir, "actions.jsonl"))
            gsi_rows = read_jsonl(os.path.join(demo_dir, "gsi.jsonl"))
            gsi_times = [row.get("ts", 0) for row in gsi_rows]
            frames_dir = os.path.join(demo_dir, "frames")
            for action in actions:
                frame_id = action.get("frame")
                frame_path = os.path.join(frames_dir, f"frame_{frame_id}.jpg")
                if os.path.exists(frame_path):
                    gsi = gsi_for_action(action, gsi_rows, gsi_times)
                    self.items.append((frame_path, action, gsi))
        if samples is not None:
            self.items = [self.items[i] for i in samples]
        self.transform = transforms.Compose([
            transforms.ToTensor(),
            transforms.Resize((IMG_SIZE, IMG_SIZE), antialias=True),
            transforms.Normalize([0.485, 0.456, 0.406],
                                 [0.229, 0.224, 0.225]),
        ])

    def __len__(self):
        return len(self.items)

    def __getitem__(self, index):
        frame_path, action, gsi = self.items[index]
        image = cv2.imread(frame_path)
        if image is None:
            image = np.zeros((IMG_SIZE, IMG_SIZE, 3), dtype=np.uint8)
        image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
        image = self.transform(image)
        action_name = action.get("action", "none")
        action_id = ACTION_MAP.get(action_name, 0)
        screen_w = max(float(action.get("screen_w", 1920)), 1.0)
        screen_h = max(float(action.get("screen_h", 1080)), 1.0)
        tx = action.get("tx", action.get("mx", screen_w / 2))
        ty = action.get("ty", action.get("my", screen_h / 2))
        target = torch.tensor([
            action_id,
            clamp01(float(tx) / screen_w),
            clamp01(float(ty) / screen_h),
            int(action.get("ability", -1)),
        ], dtype=torch.float32)
        return image, torch.from_numpy(encode_gsi(gsi)), target


class BrainModel(nn.Module):
    def __init__(self):
        super().__init__()
        # weights=None avoids an implicit network download on a new machine.
        backbone = models.resnet18(weights=None)
        features = backbone.fc.in_features
        backbone.fc = nn.Identity()
        self.backbone = backbone
        self.head = nn.Sequential(
            nn.Linear(features + GSI_DIM, 384),
            nn.ReLU(inplace=True),
            nn.Dropout(0.2),
            nn.Linear(384, 128),
            nn.ReLU(inplace=True),
            nn.Linear(128, 13),
        )

    def forward(self, image, gsi):
        return self.head(torch.cat([self.backbone(image), gsi], dim=1))


def export_onnx(model, path, device):
    model.eval()
    image = torch.randn(1, 3, IMG_SIZE, IMG_SIZE, device=device)
    gsi = torch.randn(1, GSI_DIM, device=device)
    torch.onnx.export(
        model, (image, gsi), path,
        input_names=["image", "gsi"], output_names=["output"],
        dynamic_axes={"image": {0: "batch"}, "gsi": {0: "batch"},
                      "output": {0: "batch"}},
        opset_version=17,
    )


def split_samples(dataset, seed=42):
    indices = list(range(len(dataset)))
    random.Random(seed).shuffle(indices)
    if len(indices) < 2:
        return indices, indices
    val_count = max(1, int(len(indices) * 0.1))
    return indices[val_count:], indices[:val_count]


def train():
    parser = argparse.ArgumentParser()
    parser.add_argument("--demos", required=True)
    parser.add_argument("--output", default="models")
    parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--batch", type=int, default=16)
    parser.add_argument("--lr", type=float, default=2e-4)
    parser.add_argument("--workers", type=int, default=0)
    args = parser.parse_args()

    random.seed(42)
    np.random.seed(42)
    torch.manual_seed(42)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    demo_dirs = locate_demo_dirs(args.demos)
    if not demo_dirs:
        raise SystemExit("[Error] No demo folders with actions.jsonl were found.")

    all_dataset = DemoDataset(demo_dirs)
    if len(all_dataset) == 0:
        raise SystemExit("[Error] No frame/action pairs were found.")
    train_idx, val_idx = split_samples(all_dataset)
    train_ds = DemoDataset(demo_dirs, train_idx)
    val_ds = DemoDataset(demo_dirs, val_idx)
    print(f"[Dataset] demos={len(demo_dirs)} samples={len(all_dataset)} "
          f"train={len(train_ds)} val={len(val_ds)} device={device}")

    counts = Counter(int(item[1].get("action", "none") in ACTION_MAP and
                        ACTION_MAP.get(item[1].get("action", "none"), 0))
                    for item in train_ds.items)
    weights = torch.ones(5, dtype=torch.float32)
    for action_id in range(5):
        count = sum(1 for _, a, _ in train_ds.items
                    if ACTION_MAP.get(a.get("action", "none"), 0) == action_id)
        if count:
            weights[action_id] = 1.0 / np.sqrt(count)
    weights = weights / weights.mean()

    train_loader = DataLoader(train_ds, batch_size=max(1, args.batch),
                               shuffle=True, num_workers=args.workers)
    val_loader = DataLoader(val_ds, batch_size=max(1, args.batch),
                             shuffle=False, num_workers=args.workers)
    model = BrainModel().to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    action_loss = nn.CrossEntropyLoss(weight=weights.to(device))
    coordinate_loss = nn.SmoothL1Loss(reduction="none")
    ability_loss = nn.CrossEntropyLoss()
    best_loss = float("inf")
    os.makedirs(args.output, exist_ok=True)

    for epoch in range(1, args.epochs + 1):
        model.train()
        train_total = 0.0
        for images, gsi, target in tqdm(train_loader,
                                        desc=f"Epoch {epoch}/{args.epochs}"):
            images, gsi, target = images.to(device), gsi.to(device), target.to(device)
            output = model(images, gsi)
            action_ids = target[:, 0].long()
            coords = target[:, 1:3]
            slots = target[:, 3].long().clamp(0, 5)
            active = (action_ids != 0).float()
            special = ((action_ids == 3) | (action_ids == 4)).float()
            loss = action_loss(output[:, :5], action_ids)
            loss += (coordinate_loss(torch.sigmoid(output[:, 5:7]), coords).mean(dim=1)
                     * active).mean()
            if special.sum() > 0:
                loss += (ability_loss(output[:, 7:13], slots) * special).sum() / special.sum()
            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 2.0)
            optimizer.step()
            train_total += float(loss.item())

        model.eval()
        val_total = 0.0
        with torch.no_grad():
            for images, gsi, target in val_loader:
                images, gsi, target = images.to(device), gsi.to(device), target.to(device)
                output = model(images, gsi)
                action_ids = target[:, 0].long()
                active = (action_ids != 0).float()
                loss = action_loss(output[:, :5], action_ids)
                loss += (coordinate_loss(torch.sigmoid(output[:, 5:7]), target[:, 1:3])
                         .mean(dim=1) * active).mean()
                val_total += float(loss.item())
        train_avg = train_total / max(1, len(train_loader))
        val_avg = val_total / max(1, len(val_loader))
        print(f"[Epoch {epoch}] train={train_avg:.4f} val={val_avg:.4f}")
        if val_avg < best_loss:
            best_loss = val_avg
            checkpoint = os.path.join(args.output, "best.pt")
            torch.save({"state_dict": model.state_dict(),
                        "format": "dotabot_action_v2",
                        "gsi_dim": GSI_DIM}, checkpoint)
            temporary = os.path.join(args.output, "model.onnx.tmp")
            export_onnx(model, temporary, device)
            os.replace(temporary, os.path.join(args.output, "model.onnx"))
            shutil.copy2(os.path.join(args.output, "model.onnx"),
                         os.path.join(args.output, "brain.onnx"))

    with open(os.path.join(args.output, "model.json"), "w", encoding="utf-8") as handle:
        json.dump({"format": "dotabot_action_v2", "image_size": IMG_SIZE,
                   "gsi_dim": GSI_DIM, "actions": list(ACTION_MAP)},
                  handle, ensure_ascii=False, indent=2)
    print(f"[Done] model.onnx exported to {os.path.abspath(args.output)}")


if __name__ == "__main__":
    train()

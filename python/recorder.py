import os, time, json, ctypes
from datetime import datetime
import cv2
import numpy as np
import mss

# User keybinds (VK codes)
VK_A = 0x41
VK_M = 0x4D
VK_Q = 0x51
VK_Z = 0x5A
VK_X = 0x58
VK_C = 0x43
VK_E = 0x45
VK_F = 0x46
VK_R = 0x52
VK_D = 0x44
VK_W = 0x57
VK_V = 0x56
VK_CAPITAL = 0x14
VK_B = 0x42
VK_LBUTTON = 0x01
VK_RBUTTON = 0x02
VK_XBUTTON1 = 0x05
VK_XBUTTON2 = 0x06

ABILITY_KEYS = [VK_Z, VK_X, VK_C, VK_E, VK_F, VK_R]
ITEM_KEYS = [VK_D, VK_W, VK_V, VK_CAPITAL, VK_B]

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEMOS_DIR = os.path.join(ROOT_DIR, "demos")

def get_key_name(vk):
    names = {
        VK_A: 'A', VK_M: 'M', VK_Q: 'Q', VK_Z: 'Z', VK_X: 'X',
        VK_C: 'C', VK_E: 'E', VK_F: 'F', VK_R: 'R', VK_D: 'D',
        VK_W: 'W', VK_V: 'V', VK_CAPITAL: 'CAPS', VK_B: 'B',
        VK_LBUTTON: 'LMB', VK_RBUTTON: 'RMB'
    }
    return names.get(vk, str(vk))

def get_pressed_keys():
    """Return list of currently pressed VK codes"""
    pressed = []
    user_keys = [
        VK_A, VK_M, VK_Q, VK_Z, VK_X, VK_C, VK_E, VK_F, VK_R,
        VK_D, VK_W, VK_V, VK_CAPITAL, VK_B, VK_LBUTTON, VK_RBUTTON,
        VK_XBUTTON1, VK_XBUTTON2
    ]
    for vk in user_keys:
        if ctypes.windll.user32.GetAsyncKeyState(vk) & 0x8000:
            pressed.append(vk)
    return pressed

def detect_action(keys):
    """Determine action and ability index from pressed keys"""
    if VK_RBUTTON in keys:
        return "move", -1
    if VK_LBUTTON in keys:
        return "attack", -1
    if VK_A in keys:
        return "attack", -1
    if VK_M in keys or VK_Q in keys:
        return "move", -1

    for idx, k in enumerate(ABILITY_KEYS):
        if k in keys:
            return "ability", idx

    for idx, k in enumerate(ITEM_KEYS):
        if k in keys:
            return "item", idx

    return "none", -1

def main():
    print("=" * 50)
    print("Dota 2 Demo Recorder (Python)")
    print("=" * 50)
    print("Controls:")
    print("  A / LMB = attack")
    print("  M / Q / RMB = move")
    print("  Z X C E F R = abilities")
    print("  D W V CAPS B = items")
    print("  Press ESC to stop recording")
    print("=" * 50)

    # Create demo folder
    now = datetime.now().strftime("%Y%m%d_%H%M%S")
    demo_dir = os.path.join(DEMOS_DIR, f"demo_{now}")
    frames_dir = os.path.join(demo_dir, "frames")
    os.makedirs(frames_dir, exist_ok=True)

    actions_file = os.path.join(demo_dir, "actions.jsonl")
    actions = []

    sct = mss.mss()
    monitor = sct.monitors[1]  # Primary monitor

    frame_idx = 0
    prev_mx, prev_my = 0, 0
    print(f"\nRecording to: {demo_dir}")
    print("Press ESC to stop...\n")

    try:
        while True:
            # Check ESC
            if ctypes.windll.user32.GetAsyncKeyState(0x1B) & 0x8000:
                print("\nESC pressed, stopping...")
                break

            # Screenshot
            img = np.array(sct.grab(monitor))
            img = cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)

            # Mouse position
            point = ctypes.wintypes.POINT()
            ctypes.windll.user32.GetCursorPos(ctypes.byref(point))
            mx, my = point.x, point.y

            # Keys
            keys = get_pressed_keys()
            action, ability = detect_action(keys)

            # If no keys but mouse moved significantly -> camera move
            if action == "none" and frame_idx > 0:
                if abs(mx - prev_mx) > 15 or abs(my - prev_my) > 15:
                    action = "move"

            prev_mx, prev_my = mx, my

            # Save frame
            frame_path = os.path.join(frames_dir, f"frame_{frame_idx}.jpg")
            cv2.imwrite(frame_path, img, [cv2.IMWRITE_JPEG_QUALITY, 90])

            # Log action
            log = {
                "frame": frame_idx,
                "ts": int(time.time() * 1000),
                "mx": mx,
                "my": my,
                "keys": keys,
                "action": action,
                "ability": ability,
                "tx": mx,
                "ty": my,
                "camx": 0.5,
                "camy": 0.5,
                "screen_w": monitor["width"],
                "screen_h": monitor["height"]
            }
            actions.append(log)

            if frame_idx % 100 == 0:
                print(f"  Frames recorded: {frame_idx} | Last action: {action}")

            frame_idx += 1
            time.sleep(0.1)  # 10 FPS

    except KeyboardInterrupt:
        print("\nInterrupted by user")

    # Save actions
    with open(actions_file, 'w', encoding='utf-8') as f:
        for act in actions:
            f.write(json.dumps(act, ensure_ascii=False) + '\n')

    print(f"\nSaved {frame_idx} frames to {demo_dir}")
    print(f"Actions: {len(actions)}")
    from collections import Counter
    print("Distribution:", Counter(a['action'] for a in actions))
    print("\nNow train with:")
    print(f'  python train.py --demos "{DEMOS_DIR}" --output "{os.path.join(ROOT_DIR, "models")}" --epochs 20')

if __name__ == "__main__":
    main()

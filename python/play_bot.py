import os, time, ctypes
import numpy as np
import cv2
import mss
import onnxruntime as ort
from torchvision import transforms

IMG_SIZE = 224
GSI_DIM = 69
ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Keybinds for output
KEYBINDS = {
    'move': [0x02],      # RMB
    'attack': [0x01],    # LMB
    'ability': [0x5A, 0x58, 0x43, 0x45, 0x46, 0x52],  # Z X C E F R
    'item': [0x44, 0x57, 0x56, 0x14, 0x42],           # D W V CAPS B
}

VK_LBUTTON = 0x01
VK_RBUTTON = 0x02

def press_key(vk):
    ctypes.windll.user32.keybd_event(vk, 0, 0, 0)

def release_key(vk):
    ctypes.windll.user32.keybd_event(vk, 0, 2, 0)

def mouse_click(x, y, button='left'):
    ctypes.windll.user32.SetCursorPos(int(x), int(y))
    if button == 'left':
        ctypes.windll.user32.mouse_event(0x0002, 0, 0, 0, 0)  # down
        time.sleep(0.05)
        ctypes.windll.user32.mouse_event(0x0004, 0, 0, 0, 0)  # up
    else:
        ctypes.windll.user32.mouse_event(0x0008, 0, 0, 0, 0)  # down
        time.sleep(0.05)
        ctypes.windll.user32.mouse_event(0x0010, 0, 0, 0, 0)  # up

def main():
    model_path = os.path.join(ROOT_DIR, "models", "model.onnx")
    if not os.path.exists(model_path):
        print(f"Model not found: {model_path}")
        print("Train first: python train.py ...")
        return

    session = ort.InferenceSession(model_path, providers=['CUDAExecutionProvider', 'CPUExecutionProvider'])
    print(f"Loaded model: {model_path}")
    print(f"Inputs: {[i.name for i in session.get_inputs()]}")
    print(f"Outputs: {[o.name for o in session.get_outputs()]}")

    transform = transforms.Compose([
        transforms.ToTensor(),
        transforms.Resize((IMG_SIZE, IMG_SIZE)),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
    ])

    sct = mss.mss()
    monitor = sct.monitors[1]
    screen_w = monitor["width"]
    screen_h = monitor["height"]

    print("\nBot started! Press ESC to stop.")
    print("=" * 50)

    try:
        while True:
            if ctypes.windll.user32.GetAsyncKeyState(0x1B) & 0x8000:
                print("ESC pressed, stopping...")
                break

            # Screenshot
            img = np.array(sct.grab(monitor))
            img = cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)
            img_rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
            img_t = transform(img_rgb).unsqueeze(0).numpy()

            # Native DotaBot supplies live GSI.  The standalone runner keeps
            # the interface valid and uses zeros when no bridge is attached.
            gsi = np.zeros((1, GSI_DIM), dtype=np.float32)

            # Inference
            outputs = session.run(None, {"image": img_t, "gsi": gsi})
            pred = outputs[0][0]
            action_names = ["none", "move", "attack", "ability", "item"]
            action_type = int(np.argmax(pred[:5]))
            confidence = float(np.exp(pred[action_type] - np.max(pred[:5])) /
                               np.exp(pred[:5] - np.max(pred[:5])).sum())
            tx = 1.0 / (1.0 + np.exp(-pred[5])) * screen_w
            ty = 1.0 / (1.0 + np.exp(-pred[6])) * screen_h
            ability = int(np.argmax(pred[7:13]))
            action_name = action_names[action_type] if action_type < 5 else "none"
            has_action = confidence >= 0.55

            if has_action and action_name != "none":
                # Move mouse to target
                ctypes.windll.user32.SetCursorPos(int(tx), int(ty))

                if action_name == "move":
                    mouse_click(tx, ty, 'right')
                elif action_name == "attack":
                    mouse_click(tx, ty, 'left')
                elif action_name == "ability":
                    if 0 <= ability < 6:
                        vk = KEYBINDS['ability'][ability]
                        press_key(vk)
                        time.sleep(0.05)
                        release_key(vk)
                elif action_name == "item":
                    if 0 <= ability < 5:
                        vk = KEYBINDS['item'][ability]
                        press_key(vk)
                        time.sleep(0.05)
                        release_key(vk)

                print(f"Action: {action_name:8} | Target: ({int(tx)}, {int(ty)}) | Ability: {ability}")

            time.sleep(0.1)

    except KeyboardInterrupt:
        print("\nStopped")

if __name__ == "__main__":
    main()

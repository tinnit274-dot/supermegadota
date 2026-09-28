import os, time, cv2, numpy as np
import pyautogui
import mss

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATES_DIR = os.path.join(ROOT_DIR, "templates")

def capture_screen():
    with mss.mss() as sct:
        monitor = sct.monitors[1]
        img = np.array(sct.grab(monitor))
        return cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)

def find_and_click(template_name, confidence=0.75):
    template_path = os.path.join(TEMPLATES_DIR, template_name + ".png")
    if not os.path.exists(template_path):
        return False
    screen = capture_screen()
    template = cv2.imread(template_path)
    if template is None:
        return False
    result = cv2.matchTemplate(screen, template, cv2.TM_CCOEFF_NORMED)
    _, max_val, _, max_loc = cv2.minMaxLoc(result)
    if max_val >= confidence:
        h, w = template.shape[:2]
        cx, cy = max_loc[0] + w // 2, max_loc[1] + h // 2
        pyautogui.click(cx, cy)
        print(f"[LobbyBot] Clicked {template_name} at ({cx},{cy}) conf={max_val:.2f}")
        return True
    return False

def pick_hero(hero_name):
    # TODO: add hero templates in templates/heroes/
    hero_path = os.path.join(TEMPLATES_DIR, "heroes", hero_name + ".png")
    if os.path.exists(hero_path):
        return find_and_click(os.path.join("heroes", hero_name), confidence=0.8)
    return False

def main():
    print("=" * 50)
    print("Dota 2 Lobby Bot")
    print("=" * 50)
    print("Templates are loaded from the project templates/ folder:")
    print("  - find_match.png  (button 'Найти игру')")
    print("  - accept.png     (button 'Принять')")
    print("  - pick_hero.png  (optional, any hero portrait)")
    print("  - heroes/<name>.png (for auto-pick)")
    print("Press Ctrl+C to stop")
    print("=" * 50)

    state = "find_match"
    last_click = 0

    while True:
        if state == "find_match":
            if find_and_click("find_match"):
                state = "wait_accept"
                time.sleep(3)

        elif state == "wait_accept":
            if find_and_click("accept"):
                state = "wait_pick"
                time.sleep(15)  # wait for pick screen

        elif state == "wait_pick":
            # Example: auto-pick Anti-Mage if template exists
            # Change "antimage" to your hero file name
            if pick_hero("antimage"):
                print("[LobbyBot] Hero picked!")
                state = "in_game"
            time.sleep(1)

        elif state == "in_game":
            print("[LobbyBot] Game started. Handing over to AI bot...")
            break

        time.sleep(1)

if __name__ == "__main__":
    main()

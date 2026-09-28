"""Install the generated Dota 2 GSI configuration into Steam libraries."""
import os
import shutil
import winreg


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "data", "gamestate_integration_bot.cfg")
FILE_NAME = "gamestate_integration_dotabot.cfg"


def registry_value(root, key, name):
    try:
        with winreg.OpenKey(root, key) as handle:
            return winreg.QueryValueEx(handle, name)[0]
    except OSError:
        return ""


def steam_roots():
    roots = []
    for hive, key in (
        (winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam"),
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam"),
        (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Valve\Steam"),
    ):
        path = registry_value(hive, key, "SteamPath")
        if path:
            roots.append(path)
    roots.extend([
        os.environ.get("ProgramFiles(x86)", "") + r"\Steam",
        os.environ.get("ProgramFiles", "") + r"\Steam",
    ])
    return list(dict.fromkeys(os.path.normpath(p) for p in roots if p))


def library_roots():
    result = []
    for root in steam_roots():
        result.append(root)
        library_file = os.path.join(root, "steamapps", "libraryfolders.vdf")
        if not os.path.exists(library_file):
            continue
        with open(library_file, "r", encoding="utf-8", errors="ignore") as handle:
            for line in handle:
                line = line.strip()
                if '"path"' not in line:
                    continue
                parts = line.replace('"', "").split()
                if len(parts) >= 2:
                    result.append(parts[-1].replace("\\\\", "\\"))
    return list(dict.fromkeys(os.path.normpath(p) for p in result))


def main():
    if not os.path.exists(SOURCE):
        raise SystemExit(f"Missing config: {SOURCE}")
    installed = []
    for library in library_roots():
        target_dir = os.path.join(library, "steamapps", "common",
                                  "dota 2 beta", "game", "dota", "cfg",
                                  "gamestate_integration")
        if not os.path.isdir(os.path.dirname(target_dir)):
            continue
        os.makedirs(target_dir, exist_ok=True)
        target = os.path.join(target_dir, FILE_NAME)
        shutil.copy2(SOURCE, target)
        installed.append(target)
    if installed:
        print("GSI installed:")
        for path in installed:
            print(f"  {path}")
    else:
        print("Steam/Dota 2 was not found automatically.")
        print("Run the bot once and copy data/gamestate_integration_bot.cfg")
        print("to Dota 2/game/dota/cfg/gamestate_integration manually.")


if __name__ == "__main__":
    main()

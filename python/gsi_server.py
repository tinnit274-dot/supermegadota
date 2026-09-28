"""Optional standalone GSI bridge.

The native app already has an embedded receiver.  This script is useful when
recording with python/recorder.py without starting DotaBot.exe.
"""
import glob
import http.server
import json
import os
import socketserver
import time

PORT = int(os.environ.get("DOTABOT_GSI_PORT", "3000"))
ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEMOS_DIR = os.path.join(ROOT_DIR, "demos")


def get_latest_demo():
    demos = sorted(glob.glob(os.path.join(DEMOS_DIR, "demo_*")))
    return demos[-1] if demos else None


class Handler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        try:
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length).decode("utf-8")
            demo = get_latest_demo()
            if demo:
                path = os.path.join(demo, "gsi.jsonl")
                payload = json.loads(body)
                with open(path, "a", encoding="utf-8") as handle:
                    handle.write(json.dumps(
                        {"ts": int(time.time() * 1000), "data": payload},
                        ensure_ascii=False) + "\n")
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"OK")
        except (OSError, ValueError, json.JSONDecodeError):
            self.send_response(400)
            self.end_headers()

    def log_message(self, *_):
        pass


if __name__ == "__main__":
    os.makedirs(DEMOS_DIR, exist_ok=True)
    print(f"DotaBot GSI bridge listening on :{PORT}")
    with socketserver.ThreadingTCPServer(("", PORT), Handler) as server:
        server.allow_reuse_address = True
        server.serve_forever()

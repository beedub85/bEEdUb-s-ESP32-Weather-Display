"""Read-only Home Assistant telemetry for the Weather Display."""
import argparse
import hmac
import ipaddress
import json
import math
import os
import re
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener

FIELDS = {
    "status", "job", "stage", "progress", "remaining_minutes", "finish_at",
    "layer", "layers", "nozzle", "bed", "chamber", "room_temp", "humidity", "pm25",
}
TEMPERATURES = {"nozzle", "bed", "chamber", "room_temp"}


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def normalized(field, entity):
    state = entity["state"]
    if state in ("unknown", "unavailable", ""):
        return None
    if field in {"status", "job", "stage"}:
        return state[:100]
    if field == "finish_at":
        timestamp = datetime.fromisoformat(state.replace("Z", "+00:00"))
        return int(timestamp.timestamp()) if timestamp.tzinfo else None

    value = float(state)
    unit = entity.get("attributes", {}).get("unit_of_measurement")
    if field in TEMPERATURES:
        if unit == "°F":
            value = (value - 32) * 5 / 9
        elif unit != "°C":
            return None
    elif field == "remaining_minutes":
        if unit == "h":
            value *= 60
        elif unit == "s":
            value /= 60
        elif unit != "min":
            return None
    elif field == "pm25" and unit not in {"µg/m³", "μg/m³", "ug/m3"}:
        return None
    return value if math.isfinite(value) else None


def collect_forever(config, token, publish):
    base = config["home_assistant"].rstrip("/")
    opener = build_opener(NoRedirect())

    def read(item):
        field, entity_id = item
        request = Request(f"{base}/api/states/{entity_id}",
                          headers={"Authorization": f"Bearer {token}"})
        try:
            with opener.open(request, timeout=3) as response:
                return field, normalized(field, json.loads(response.read(65536)))
        except (OSError, ValueError, TypeError, KeyError, OverflowError):
            return field, None

    with ThreadPoolExecutor(max_workers=len(FIELDS)) as pool:
        while True:
            start = time.monotonic()
            data = dict(pool.map(read, config["entities"].items()))
            publish({"schema": 1, "printer": config.get("printer", "Bambu Lab X2D")[:60],
                     "readings": data})
            time.sleep(max(0, 3 - (time.monotonic() - start)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path)
    parser.add_argument("--bind", required=True, help="LAN IP of this computer")
    parser.add_argument("--port", type=int, default=8787)
    args = parser.parse_args()
    try:
        address = ipaddress.IPv4Address(args.bind)
    except ValueError:
        parser.error("--bind must be this computer's private IPv4 LAN address.")
    if not any(address in ipaddress.ip_network(net)
               for net in ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "127.0.0.1/32")):
        parser.error("--bind must be this computer's private IPv4 LAN address.")
    config = json.loads(args.config.read_text(encoding="utf-8"))
    token = os.environ.get("HA_TOKEN", "")
    if not token:
        parser.error("Set HA_TOKEN in the bridge environment.")
    key = os.environ.get("PRINT_ROOM_KEY", "")
    if not re.fullmatch(r"[0-9a-fA-F]{32}", key):
        parser.error("Set PRINT_ROOM_KEY to a random 32-character hex key.")
    expected = ("Bearer " + key).encode("ascii")
    url = urlsplit(config["home_assistant"])
    if (url.scheme not in {"http", "https"} or not url.hostname or url.username or
            url.password or url.query or url.fragment):
        parser.error("Enter a Home Assistant base URL.")
    entities = config["entities"]
    if ("status" not in entities or entities.keys() - FIELDS or
            any(not re.fullmatch(r"sensor\.[a-z0-9_]+", entity) for entity in entities.values())):
        parser.error("Map status and any optional fields to Home Assistant sensor entity IDs.")

    payload = (None, 0)

    def publish(snapshot):
        nonlocal payload
        payload = (json.dumps(snapshot, allow_nan=False).encode("utf-8"), time.monotonic())

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path != "/status":
                self.send_error(404)
                return
            supplied = self.headers.get("Authorization", "").encode("utf-8")
            if not hmac.compare_digest(supplied, expected):
                self.send_error(401)
                return
            current, collected_at = payload
            if current is None or time.monotonic() - collected_at >= 12:
                self.send_error(503, "Waiting for Home Assistant")
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(current)))
            self.end_headers()
            self.wfile.write(current)

        def log_message(self, *_):
            pass

    with ThreadingHTTPServer((args.bind, args.port), Handler) as server:
        threading.Thread(target=collect_forever, args=(config, token, publish), daemon=True).start()
        print(f"Print Room bridge listening at http://{args.bind}:{args.port}/status", flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Local-only Tianji HTTP/security regression with a disposable server and data.

Usage: python3 tools/tianji-adversarial-check.py --binary build/ningzhou_web
Never connects to an existing server. All test accounts, edited import fixtures,
store failure injection and restart tests live inside a fresh temporary directory.
Edited imports are test fixtures, not evidence of competitive anti-cheat security.
"""
from __future__ import annotations

import argparse
import copy
import concurrent.futures
import http.cookiejar
import json
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request


class Client:
    def __init__(self, base: str, nickname: str = ""):
        self.base, self.nickname = base, nickname
        self.jar = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.jar))

    def call(self, path, body=None, method=None):
        data = None if body is None else (body if isinstance(body, bytes) else json.dumps(body).encode())
        request = urllib.request.Request(self.base + path, data=data,
                                         method=method or ("POST" if data is not None else "GET"),
                                         headers={"Content-Type": "application/json"})
        for attempt in range(3):
            try:
                with self.opener.open(request, timeout=30) as response:
                    raw, status = response.read(), response.status
            except urllib.error.HTTPError as error:
                raw, status = error.read(), error.code
            if status == 429 and attempt < 2:
                time.sleep(1)
                continue
            try:
                result = json.loads(raw)
            except (ValueError, UnicodeDecodeError):
                result = {"raw": repr(raw[:300])}
            return status, result
        raise AssertionError("unreachable")

    def ok(self, path, body=None, method=None):
        status, result = self.call(path, body, method)
        if status != 200 or result.get("ok") is False:
            raise AssertionError(f"Fixture request failed {path}: HTTP {status} {result}")
        return result

    def login(self):
        return self.ok("/api/login", {"nickname": self.nickname, "password": "localTestOnly"})

    def status(self):
        return self.ok("/api/tianji/status")

    def export(self):
        return json.loads(self.ok("/api/export")["game"])

    def load(self, game):
        return self.ok("/api/import", {"game": json.dumps(game, ensure_ascii=False)})


class Server:
    def __init__(self, binary: Path, web: Path, directory: Path):
        self.binary, self.web, self.directory = binary, web, directory
        self.data = directory / "data"
        self.feedback = directory / "feedback"
        self.process = None
        self.log = None
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            self.port = sock.getsockname()[1]
        self.base = f"http://127.0.0.1:{self.port}"

    def start(self):
        self.log = (self.directory / "server.log").open("ab")
        self.process = subprocess.Popen(
            [str(self.binary), "--host", "127.0.0.1", "--port", str(self.port),
             "--web", str(self.web), "--data", str(self.data),
             "--feedback", str(self.feedback), "--no-open"],
            cwd=self.directory, stdout=self.log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                raise AssertionError(f"Server exited {self.process.returncode}; see {self.directory / 'server.log'}")
            try:
                Client(self.base).call("/api/tianji/board")
                return
            except (OSError, urllib.error.URLError):
                time.sleep(.1)
        raise AssertionError("Server startup timed out")

    def stop(self, crash=False):
        if self.process is not None:
            if crash:
                self.process.kill()
            else:
                self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=10)
            self.process = None
        if self.log:
            self.log.close()
            self.log = None

    def restart(self, *clients, crash=False):
        self.stop(crash)
        self.start()
        for client in clients:
            client.login()


class Checks:
    def __init__(self):
        self.failed = []
        self.count = 0

    def check(self, condition, label, details=None):
        self.count += 1
        print(f"  [{'PASS' if condition else 'FAIL'}] {label}", flush=True)
        if not condition:
            self.failed.append(label)
            if details is not None:
                print("    " + json.dumps(details, ensure_ascii=False)[:1500], flush=True)
        return condition

    def rejected(self, result, label, codes=(400, 403, 404, 409, 413, 422)):
        status, body = result
        return self.check(status in codes and body.get("ok") is False,
                          label, {"status": status, "body": body})


def fixture(client, year=50, stage=0, seed=1234):
    """Use actual export/import validation, avoiding direct writes to player data."""
    client.login()
    client.ok("/api/new", {"name": client.nickname + "修士", "gender": "女", "seed": seed, "traits": []})
    game = client.export()
    game["year"] = year - 1
    game["clock"]["days"] = (year - 1) * 360
    game["player"]["age"] = game["clock"]["ageBase"] + year - 1
    game["player"]["lifespan"] = max(2000, game["player"]["lifespan"])
    game["player"]["stage"] = stage
    game["player"]["highestStage"] = max(stage, game["player"].get("highestStage", stage))
    game["player"]["location"] = "tianji"
    game["job"] = None
    game["pending"] = None
    game["player"]["arts"]["battle"] = None
    game["checkpoint"]["enabled"] = False
    game["checkpoint"]["snapshot"] = ""
    client.load(game)
    return client.export()


def at_year(client, source, year):
    game = copy.deepcopy(source)
    game["year"] = year - 1
    game["clock"]["days"] = (year - 1) * 360
    game["player"]["age"] = game["clock"]["ageBase"] + year - 1
    game["checkpoint"].update(enabled=False, snapshot="", days=0, lastAutoYear=0)
    client.load(game)


def entry_id(state):
    return (state.get("entry") or {}).get("id")


def encounter(state):
    return state.get("encounter") or {}


def action_input(state, request_id, action="end"):
    current = encounter(state)
    return {"encounterId": current.get("id"), "expectedRevision": current.get("revision"),
            "requestId": request_id, "id": action}


def board_rows(value):
    """Allow grouped and flat board representations; identify score rows only."""
    if isinstance(value, list):
        return [row for part in value for row in board_rows(part)]
    if isinstance(value, dict):
        if "score" in value and ("nickname" in value or "id" in value):
            return [value]
        return [row for part in value.values() for row in board_rows(part)]
    return []


def private_keys(value, path=""):
    found = []
    if isinstance(value, dict):
        forbidden = {"snapshot", "game", "rng", "seed", "password", "passwordHash", "session", "sid",
                     "enemyCards", "ownerId", "accountId", "lifeId", "ip", "npcBonds", "relations", "logs"}
        for key, part in value.items():
            if key in forbidden:
                found.append(path + "/" + key)
            found.extend(private_keys(part, path + "/" + key))
    elif isinstance(value, list):
        for index, part in enumerate(value):
            found.extend(private_keys(part, path + f"/{index}"))
    return found


def run(server, checks):
    anon = Client(server.base)
    checks.check(anon.call("/api/tianji/board")[0] == 200, "Public board is readable without a session")
    for endpoint in ("status", "join", "challenge", "action"):
        body = None if endpoint == "status" else {"requestId": "anonymous"}
        checks.rejected(anon.call("/api/tianji/" + endpoint, body),
                        f"Anonymous {endpoint} rejected", (401,))
    checks.rejected(anon.call("/api/tianji/board", {"score": 99999}),
                    "Public board cannot accept client-submitted scores", (405,))

    a, b, c, d = [Client(server.base, name) for name in ("TianjiQA", "TianjiQB", "TianjiQC", "TianjiQD")]
    a.login()
    empty = a.status()
    checks.check(empty.get("phase") == "unavailable" and empty.get("entry") is None and not empty.get("canJoin"),
                 "A logged-in account without a character cannot join")
    checks.rejected(a.call("/api/tianji/join", {"requestId": "no-character"}), "Join without a personal world rejected")
    ga = fixture(a, seed=101)
    gb = fixture(b, seed=202)
    fixture(c, stage=3, seed=303)
    fixture(d, year=100, seed=404)
    for endpoint in ("join", "challenge", "action"):
        checks.rejected(a.call("/api/tianji/" + endpoint), f"GET {endpoint} cannot mutate", (405,))
    checks.rejected(a.call("/api/tianji/status", {}), "POST status rejected", (405,))

    for year, opened in ((49, False), (50, True), (59, True), (60, False), (99, False), (100, True)):
        at_year(a, ga, year)
        status = a.status()
        checks.check(status.get("window", {}).get("open") is opened,
                     f"Displayed year {year}: {'open' if opened else 'closed'} window", status.get("window"))
    at_year(a, ga, 49)
    checks.rejected(a.call("/api/tianji/join", {"requestId": "closed-join"}), "Cannot join before window")
    at_year(a, ga, 50)
    for bad in ({}, {"requestId": ""}, {"requestId": "x" * 1000}, {"requestId": 1}):
        checks.rejected(a.call("/api/tianji/join", bad), "Join requires bounded string requestId")
    checks.rejected(a.call("/api/tianji/join", {"requestId": "spoof-owner", "owner": "other", "score": 9999}),
                    "Signup cannot inject another owner, a build or a score")
    joined = a.ok("/api/tianji/join", {"requestId": "join-a"})
    checks.check(bool(joined.get("rules", {}).get("integrityNotice")) and
                 "导入" in joined["rules"]["integrityNotice"], "Import and multi-account fairness limitations are disclosed")
    astate = a.status()
    aid = entry_id(astate)
    checks.check(bool(aid), "Join creates a server entry identifier", joined)
    a.ok("/api/tianji/join", {"requestId": "join-a"})
    checks.check(entry_id(a.status()) == aid, "Identical join retry preserves entry")
    b.ok("/api/tianji/join", {"requestId": "join-b"})
    bid = entry_id(b.status())
    c.ok("/api/tianji/join", {"requestId": "join-c"})
    cid = entry_id(c.status())
    d.ok("/api/tianji/join", {"requestId": "join-d"})
    did = entry_id(d.status())
    opponents = a.status().get("opponents", [])
    listed = {item.get("id") for item in opponents}
    checks.check(bid in listed and aid not in listed and cid not in listed and did not in listed,
                 "Opponents exclude self, different major realm and different event", opponents)
    for target, label in ((aid, "self"), (cid, "different major realm"), (did, "different event"),
                          ("../../players/elsewhere", "path traversal"), ("x" * 10000, "oversized target")):
        checks.rejected(a.call("/api/tianji/challenge", {"targetId": target, "requestId": "invalid-" + label.replace(" ", "-")}),
                        f"Challenge rejects {label}")

    # Imported/new lives cannot make a second frozen entry in the same account/event.
    for index, change in enumerate(("edited-life", "new-life")):
        changed = copy.deepcopy(ga)
        changed["lifeId"] = change
        changed["checkpoint"].update(enabled=False, snapshot="", days=0, lastAutoYear=0)
        changed["player"]["body"] += 200
        if change == "new-life":
            a.ok("/api/new", {"name": "新的一世", "seed": 707, "traits": []})
        a.load(changed)
        a.call("/api/tianji/join", {"requestId": "join-repeat-" + str(index)})
        checks.check(entry_id(a.status()) == aid, f"{change}: same account/event retains original entry")
    a.load(ga)

    # Replace and log out the opponent: the challenge must use their enrolled shadow.
    old_b_entry = copy.deepcopy(b.status().get("entry"))
    changed_b = copy.deepcopy(gb)
    changed_b["player"]["stage"] = changed_b["player"]["highestStage"] = 12
    changed_b["player"]["body"] += 999
    changed_b["player"]["name"] = "ChangedAfterEnrollment"
    b.load(changed_b)
    opponent_world = b.export()
    checks.check(b.status().get("entry") == old_b_entry, "Post-join build changes cannot alter registered shadow")
    b.ok("/api/logout", {})
    server.restart(a, c, d)
    before_world = a.export()
    started = a.ok("/api/tianji/challenge", {"targetId": bid, "requestId": "challenge-b"})
    active = a.status()
    fight = encounter(active)
    checks.check(bool(fight.get("id")) and bool(fight.get("battle")), "Offline opponent shadow starts a manual card battle", started)
    checks.check(a.export() == before_world, "Starting tournament fight cannot mutate personal world")
    checks.check(not private_keys(active), "Status exposes no source Game, RNG, account or opponent hidden card state", private_keys(active))
    checks.check(not private_keys(anon.ok("/api/tianji/board")), "Public board exposes no private account/source state", private_keys(anon.ok("/api/tianji/board")))
    checks.rejected(c.call("/api/tianji/action", action_input(active, "steal-fight")), "Another account cannot control encounter")
    checks.check(a.status().get("encounter") == fight, "Cross-account action leaves encounter unchanged")
    for patch, label in (({"expectedRevision": -1}, "negative revision"), ({"expectedRevision": 0.5}, "fractional revision"),
                         ({"expectedRevision": 1e30}, "overflow revision"), ({"expectedRevision": fight.get("revision", 0) + 1}, "future revision"),
                         ({"id": "not-a-card"}, "invented action"), ({"requestId": ""}, "empty request ID"),
                         ({"expectedRevision": True}, "boolean revision"), ({"target": {}}, "object target"), ({"encounterId": "../store"}, "path-like encounter")):
        body = action_input(active, "bad-" + label.replace(" ", "-"))
        body.update(patch)
        checks.rejected(a.call("/api/tianji/action", body), f"Action rejects {label}")
        checks.check(a.status().get("encounter") == fight, f"Rejected {label} leaves state unchanged")

    for path, body in (("/api/new", {"name": "不能替换", "traits": []}),
                       ("/api/import", {"game": json.dumps(ga)}),
                       ("/api/battle/turn", {"id": "escape"}),
                       ("/api/advance", {"days": 1})):
        checks.rejected(a.call(path, body), f"Active duel blocks unrelated mutation {path}")
    checks.check(a.export() == before_world, "Rejected world mutations preserve original save")

    # Exact duplicates, stale requests and crash/restart consistency.
    request = action_input(active, "first-turn")
    a.ok("/api/tianji/action", request)
    after = a.status()
    a.ok("/api/tianji/action", request)
    checks.check(a.status().get("encounter") == encounter(after), "Exact action retry cannot advance revision/RNG twice")
    changed_request = dict(request, id="defend")
    checks.rejected(a.call("/api/tianji/action", changed_request), "Reusing requestId for a different payload rejected")
    checks.rejected(a.call("/api/tianji/action", dict(request, requestId="stale-new-id")), "Stale revision with new requestId rejected")
    server.restart(a, c, d, crash=True)
    restored = a.status()
    checks.check(encounter(restored) == encounter(after), "Crash/restart preserves battle state and revision exactly")
    a.ok("/api/tianji/action", request)
    checks.check(encounter(a.status()) == encounter(after), "Action receipt remains idempotent after crash/restart")

    # Branch the disposable durable store to prove future RNG, not merely the visible
    # current hand, is identical after serialization. No production files are used.
    store = server.data / "tianji" / "store.json"
    prior_store = store.read_bytes()
    replay_request = action_input(a.status(), "rng-replay")
    a.ok("/api/tianji/action", replay_request)
    replay_state = encounter(a.status())
    server.stop(crash=True)
    store.write_bytes(prior_store)
    server.start()
    a.login()
    a.ok("/api/tianji/action", replay_request)
    checks.check(encounter(a.status()) == replay_state,
                 "Same action from the same saved encounter produces identical post-restart RNG/result")

    parallel_state = a.status()
    parallel_request = action_input(parallel_state, "parallel-duplicate")
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
        futures = [executor.submit(a.call, "/api/tianji/action", parallel_request) for _ in range(2)]
        parallel_results = [future.result() for future in futures]
    checks.check(all(code == 200 for code, _ in parallel_results) and
                 encounter(a.status()).get("revision") == encounter(parallel_state).get("revision", 0) + 1,
                 "Concurrent duplicate actions commit exactly once", parallel_results)

    # Full manual fight uses advertised cards, never submits an outcome or score.
    settled = a.status()
    for index in range(160):
        current = encounter(settled)
        if current.get("state") == "settled" or not current.get("battle"):
            break
        battle = current["battle"]
        cards = [card for card in battle.get("hand", []) if card.get("playable") and not card.get("requiresTarget")]
        card = max(cards, key=lambda row: (row.get("damage", 0), row.get("shield", 0)), default=None)
        command = card["id"] if card else "end"
        settled = a.ok("/api/tianji/action", action_input(settled, f"play-{index}", command))
        if "encounter" not in settled:
            settled = a.status()
    checks.check(encounter(settled).get("state") == "settled", "Manual card battle reaches a durable terminal result", encounter(settled))
    checks.check(a.export() == before_world, "Tournament battle and result leave world time, health, resources unchanged")
    b.login()
    checks.check(b.export() == opponent_world, "Challenging an offline shadow never mutates its owner’s live world")
    board = anon.ok("/api/tianji/board")
    expected_score = (settled.get("entry") or a.status().get("entry") or {}).get("score")
    deadline = time.monotonic() + board.get("refreshSeconds", 30) + 3
    while time.monotonic() < deadline:
        rows = board_rows(board)
        row = next((row for row in rows if row.get("id") == aid), None)
        if row and row.get("pending") == 0 and row.get("score") == expected_score:
            break
        time.sleep(.5)
        board = anon.ok("/api/tianji/board")
    checks.check(row is not None, "Tournament result appears on public server board", board)
    if row:
        checks.check(row.get("score") == row.get("wins", 0) * 3 + row.get("draws", 0),
                     "Board score is server-derived: win 3, draw 1, loss 0", row)
    checks.rejected(a.call("/api/tianji/challenge", {"targetId": bid, "requestId": "repeat-target"}), "A scored target cannot be farmed again")
    saved_board = anon.ok("/api/tianji/board")
    a.load(ga)
    a.call("/api/tianji/join", {"requestId": "post-result-import"})
    checks.check(entry_id(a.status()) == aid, "Save rollback cannot reset scored tournament entry")
    checks.rejected(a.call("/api/tianji/challenge", {"targetId": bid, "requestId": "rollback-target"}), "Save rollback cannot reset target attempt ledger")
    checks.check(board_rows(anon.ok("/api/tianji/board")) == board_rows(saved_board), "Save rollback cannot change tournament board")
    server.restart(a, crash=True)
    checks.check(board_rows(anon.ok("/api/tianji/board")) == board_rows(saved_board), "Completed result persists across abrupt restart")

    # New eligible target proves expiration, rather than the old attempt ledger,
    # prevents going back in time to reopen an already expired entry.
    e = Client(server.base, "TianjiQE")
    strong = fixture(e, stage=2, seed=505)
    strong["player"]["body"] = 100
    strong["player"]["arts"]["hpBonus"] = 100
    strong["player"]["arts"]["speed"] = 200
    e.load(strong)
    e.ok("/api/tianji/join", {"requestId": "join-e"})
    eid = entry_id(e.status())
    at_year(a, ga, 60)
    a.status()  # Observing the closed period must be durably monotone.
    a.load(ga)
    checks.rejected(a.call("/api/tianji/challenge", {"targetId": eid, "requestId": "expired-rollback"}),
                    "Import rollback cannot reopen an observed expired tournament window")
    server.restart(a, crash=True)
    checks.rejected(a.call("/api/tianji/challenge", {"targetId": eid, "requestId": "expired-restarted"}),
                    "Window-expiry lock survives restart")

    # A failed atomic replacement must roll back both the mutation and its receipt.
    f = Client(server.base, "TianjiQF")
    fixture(f, seed=606)
    backup = store.with_name("store.before-failure.json")
    store.rename(backup)
    store.mkdir()
    try:
        checks.rejected(f.call("/api/tianji/join", {"requestId": "failed-disk-join"}),
                        "Write failure is returned instead of claiming signup succeeded", (500, 503))
        checks.check(entry_id(f.status()) is None, "Failed persistence rolls back in-memory signup")
    finally:
        store.rmdir()
        backup.rename(store)
    f.ok("/api/tianji/join", {"requestId": "failed-disk-join"})
    fid = entry_id(f.status())
    checks.check(bool(fid), "Same request can succeed after restoring writable store")
    server.restart(f, crash=True)
    checks.check(entry_id(f.status()) == fid, "Recovered transaction is durable after restart")

    e.login()
    won = e.ok("/api/tianji/challenge", {"targetId": fid, "requestId": "score-positive"})
    for index in range(200):
        current = encounter(won)
        if current.get("state") == "settled":
            break
        cards = [card for card in current.get("battle", {}).get("hand", [])
                 if card.get("playable") and not card.get("requiresTarget")]
        card = max(cards, key=lambda row: (row.get("damage", 0), row.get("shield", 0)), default=None)
        won = e.ok("/api/tianji/action", action_input(won, f"positive-{index}", card["id"] if card else "end"))
    checks.check(encounter(won).get("result") == "win" and won.get("entry", {}).get("score") == 3 and
                 won.get("entry", {}).get("wins") == 1,
                 "Real manual victory awards exactly one win and three points", encounter(won).get("result"))
    server.restart(e, f, crash=True)
    winner = next((row for row in board_rows(anon.ok("/api/tianji/board")) if row.get("id") == eid), {})
    checks.check(winner.get("score") == 3 and winner.get("rank") == 1,
                 "Positive score survives restart and leads the public board", winner)
    resigned = f.ok("/api/tianji/challenge", {"targetId": eid, "requestId": "forfeit-match"})
    resignation = action_input(resigned, "forfeit-action", "forfeit")
    resigned = f.ok("/api/tianji/action", resignation)
    f.ok("/api/tianji/action", resignation)
    checks.check(encounter(resigned).get("result") == "forfeit" and
                 f.status().get("entry", {}).get("losses") == 1 and f.status().get("entry", {}).get("score") == 0,
                 "Forfeit and duplicate terminal receipt produce one loss and zero points")
    checks.rejected(f.call("/api/tianji/action", action_input(resigned, "terminal-new-action")),
                    "A new action cannot revive a settled encounter")

    corrupt_dir = server.directory / "corrupt-store-case"
    corrupt_dir.mkdir()
    corrupt = Server(server.binary, server.web, corrupt_dir)
    corrupt_path = corrupt.data / "tianji" / "store.json"
    corrupt_path.parent.mkdir(parents=True)
    corrupt_path.write_text("{this-is-not-valid-json", encoding="utf-8")
    try:
        corrupt.start()
        checks.rejected(Client(corrupt.base).call("/api/tianji/board"),
                        "Corrupt tournament store fails closed instead of resetting rankings", (500, 503))
        checks.check(corrupt_path.read_text() == "{this-is-not-valid-json",
                     "Corrupt store is preserved for operator recovery")
    finally:
        corrupt.stop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--keep", action="store_true", help="Keep disposable data and logs for diagnosis")
    args = parser.parse_args()
    binary = args.binary.resolve()
    if not binary.is_file():
        parser.error(f"Missing built server: {binary}")
    root = Path(__file__).resolve().parents[1]
    temp = Path(tempfile.mkdtemp(prefix="ningzhou-tianji-qa-"))
    server = Server(binary, root / "web", temp)
    checks = Checks()
    failed = False
    try:
        server.start()
        run(server, checks)
    except Exception as error:
        failed = True
        print(f"\n[ERROR] {type(error).__name__}: {error}", file=sys.stderr)
        import traceback
        traceback.print_exc()
    finally:
        server.stop()
        print(f"\nTianji adversarial checks: {checks.count - len(checks.failed)}/{checks.count} passed")
        if args.keep or failed or checks.failed:
            print(f"Disposable test data and log: {temp}")
        else:
            shutil.rmtree(temp)
    return 1 if failed or checks.failed else 0


if __name__ == "__main__":
    sys.exit(main())

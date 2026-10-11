#!/usr/bin/env python3
"""多人版接口自测：两个道号互不干扰、需要登录、导入上限、路径穿越、静态图片。
用法：python3 multiplayer-check.py http://127.0.0.1:18106"""
import json, sys, urllib.request, http.cookiejar, socket, urllib.parse

BASE = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:18106"
fails = 0
def check(cond, msg):
    global fails
    print(("  [通过] " if cond else "  [失败] ") + msg)
    if not cond: fails += 1

def client():
    cj = http.cookiejar.CookieJar()
    return urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj))

def call(op, path, body=None, method=None):
    data = None if body is None else (body if isinstance(body, bytes) else json.dumps(body).encode())
    req = urllib.request.Request(BASE + path, data=data, method=method or ("POST" if data is not None else "GET"),
                                 headers={"Content-Type": "application/json"})
    try:
        with op.open(req, timeout=30) as r:
            raw = r.read(); return r.status, raw, dict(r.headers)
    except urllib.error.HTTPError as e:
        return e.code, e.read(), dict(e.headers)

def js(raw):
    try: return json.loads(raw)
    except Exception: return {}

anon = client()
s, b, _ = call(anon, "/api/state")
check(s == 401 and js(b).get("needLogin"), "未登录访问 /api/state 返回 401")
s, b, _ = call(anon, "/api/login", {"nickname": "../etc", "password": "pw1234"})
check(s == 400, "非法道号（含路径字符）被拒绝")
s, b, _ = call(anon, "/api/login", {"nickname": "一二三四五六七八九十一二三四五六七", "password": "pw1234"})
check(s == 400, "超过 16 字的道号被拒绝")

a, bb = client(), client()
s, b, h = call(a, "/api/login", {"nickname": "测试甲", "password": "pw1234"})
check(s == 200 and js(b).get("nickname") == "测试甲" and "nz_sid=" in h.get("Set-Cookie", ""), "道号「测试甲」登录并获得 cookie")
s, b, _ = call(bb, "/api/login", {"nickname": "Tester_B", "password": "pw1234"})
check(s == 200, "道号「Tester_B」登录")

s, b, _ = call(a, "/api/new", {"name": "甲的角色", "gender": "女", "seed": 11, "traits": []})
check(s == 200 and js(b).get("ok"), "甲开新局")
s, b, _ = call(bb, "/api/state")
check(s == 200 and js(b).get("hasGame") is False, "乙此时没有存档（互不影响）")
s, b, _ = call(bb, "/api/new", {"name": "乙的角色", "gender": "男", "seed": 22, "traits": []})
check(s == 200 and js(b).get("ok"), "乙开新局")
s, b, _ = call(a, "/api/state")
check(js(b).get("state", {}).get("player", {}).get("name") == "甲的角色", "甲读到的仍是自己的角色")
s, b, _ = call(a, "/api/save", {"slot": 1})
check(s == 200 and js(b).get("ok"), "甲存到 1 号位")
s, b, _ = call(bb, "/api/slots")
slots = js(b).get("slots") or js(b).get("list") or []
check(not any(x.get("exists") and x.get("slot") == 1 for x in slots), "乙看不到甲的 1 号存档位")

a2 = client()
s, b, _ = call(a2, "/api/login", {"nickname": "测试甲", "password": "pw1234"})
check(js(b).get("hasGame") is True, "另一设备用同一道号登录，读到同一份进度")
s, b, _ = call(a2, "/api/state")
check(js(b).get("state", {}).get("player", {}).get("name") == "甲的角色", "同一道号共享存档")

def too_big(path, size):
    try:
        s, b, _ = call(a, path, json.dumps({"game": "x" * size}).encode())
        return s == 413
    except Exception:
        return True   # 服务器直接拒收并断开也算拒绝
check(too_big("/api/import", 6 * 1024 * 1024 + 10), "超过 6MB 的存档导入被拒绝")
check(too_big("/api/save", 600 * 1024), "普通接口超过 512KB 的请求被拒绝")

s, b, _ = call(a, "/api/export", {})
exp = js(b)
check(s == 200 and exp.get("name") == "甲的角色", "导出的是自己的存档")
s, b, _ = call(bb, "/api/import", {"game": exp.get("game", ""), "legacy": exp.get("legacy", "")})
check(s == 200 and js(b).get("state", {}).get("player", {}).get("name") == "甲的角色", "导入只影响导入者自己（乙导入甲的导出档）")
s, b, _ = call(a, "/api/state")
check(js(b).get("state", {}).get("player", {}).get("name") == "甲的角色", "甲未受乙导入影响")

# 路径穿越（原始请求，避免客户端规整路径）
def raw_get(path):
    u = urllib.parse.urlparse(BASE)
    sk = socket.create_connection((u.hostname, u.port), timeout=10)
    sk.sendall(f"GET {path} HTTP/1.1\r\nHost: x\r\n\r\n".encode())
    data = b""
    while True:
        c = sk.recv(65536)
        if not c: break
        data += c
    sk.close()
    return int(data.split(b" ")[1]), data
for p in ["/../CMakeLists.txt", "/%2e%2e/%2e%2e/etc/passwd", "/assets/..%2f..%2fetc/passwd", "/..%5c..%5cetc/passwd"]:
    code, _ = raw_get(p)
    check(code in (400, 404), f"路径穿越 {p} 被拒绝 ({code})")
code, data = raw_get("/assets/map/ningzhou-atlas.png")
check(code == 200 and b"image/webp" in data[:400], "请求 .png 返回压缩后的 WebP")
code, data = raw_get("/")
check(code == 200 and b"app.mjs" in data, "首页可访问")

# 排行榜：无需登录，数据来自服务器存档，不接受客户端提交
s, b, _ = call(client(), "/api/leaderboard?limit=10")
lb = js(b)
names = [x.get("nickname") for x in lb.get("list", [])]
check(s == 200 and lb.get("ok") and "测试甲" in names and "Tester_B" in names, "排行榜（未登录可读）包含两位玩家")
check(all(x.get("realm") and "stage" in x and "xp" in x for x in lb.get("list", [])), "排行榜条目含境界/修为")
st = [(x["stage"], x["xp"]) for x in lb.get("list", [])]
check(st == sorted(st, reverse=True), "排行榜按境界、修为降序")
s, b, _ = call(a, "/api/leaderboard", {"nickname": "测试甲", "stage": 12, "xp": 999999})
check(s == 405, "排行榜拒绝 POST 提交分数")
s, b, _ = call(anon, "/api/leaderboard?limit=1")
check(len(js(b).get("list", [])) <= 1, "limit 参数生效")

# ---- 密码 ----
import os
c = client()
s, b, _ = call(c, "/api/login", {"nickname": "测试甲"})
check(s == 400, "缺少密码被拒绝")
s, b, _ = call(c, "/api/login", {"nickname": "测试丙", "password": "123"})
check(s == 400, "少于 4 位的密码被拒绝")
s, b, _ = call(c, "/api/nickinfo?nickname=" + urllib.parse.quote("测试甲"))
check(js(b).get("hasPassword") is True, "nickinfo：测试甲已设密码")
s, b, _ = call(c, "/api/nickinfo?nickname=" + urllib.parse.quote("从未出现"))
check(js(b).get("exists") is False and js(b).get("hasPassword") is False, "nickinfo：新道号未注册")
DATA = os.environ.get("NZ_DATA")
if DATA:
    rec = open(os.path.join(DATA, "players", "测试甲".encode().hex(), "password.txt")).read()
    check(rec.startswith("pbkdf2-sha256$") and "pw1234" not in rec, "密码以加盐 PBKDF2 存储，无明文")
    old = os.path.join(DATA, "players", "老号".encode().hex())
    os.makedirs(old, exist_ok=True); open(os.path.join(old, "nickname.txt"), "w").write("老号\n")
    s, b, _ = call(c, "/api/nickinfo?nickname=" + urllib.parse.quote("老号"))
    check(js(b).get("exists") is True and js(b).get("hasPassword") is False, "nickinfo：老号存在但未设密码")
    s, b, _ = call(c, "/api/login", {"nickname": "老号", "password": "oldpw"})
    check(s == 200 and js(b).get("passwordSet") is True, "老号首次登录设置密码")
    s, b, _ = call(client(), "/api/login", {"nickname": "老号", "password": "other"})
    check(s == 401, "老号设密后错误密码被拒")
s, b, _ = call(client(), "/api/login", {"nickname": "测试甲", "password": "wrong"})
check(s == 401, "错误密码被拒绝")
codes = [call(client(), "/api/login", {"nickname": "测试甲", "password": "wrong"})[0] for _ in range(6)]
check(codes[-1] == 429, f"连续失败后限速 {codes}")
s, b, _ = call(client(), "/api/login", {"nickname": "Tester_B", "password": "pw1234"})
check(s == 200, "限速只针对该 IP+道号，其他道号不受影响")
s, b, _ = call(bb, "/api/password", {"old": "bad", "new": "newpw"})
check(s == 403, "改密码：原密码错误被拒")
s, b, _ = call(bb, "/api/password", {"old": "pw1234", "new": "newpw"})
check(s == 200 and js(b).get("ok"), "改密码成功")
s, b, _ = call(bb, "/api/state")
check(s == 200, "改密码的会话仍有效")
s, b, _ = call(client(), "/api/login", {"nickname": "Tester_B", "password": "newpw"})
check(s == 200, "新密码可登录")

# ---- 反馈箱 ----
s, b, _ = call(client(), "/api/feedback", {"type": "bug", "text": "x"})
check(s == 401, "未登录不能提交反馈")
s, b, _ = call(a, "/api/feedback", {"type": "hack", "text": "x"})
check(s == 400, "非法反馈类型被拒绝")
s, b, _ = call(a, "/api/feedback", {"type": "bug", "text": "长" * 2001})
check(s == 400, "超过 2000 字被拒绝")
s, b, _ = call(a, "/api/feedback", {"type": "bug", "text": "   "})
check(s == 400, "空反馈被拒绝")
s, b, _ = call(a, "/api/feedback", {"type": "suggest", "text": "测试反馈\n第二行"})
check(s == 200 and js(b).get("ok"), "登录后提交反馈成功")
s, b, _ = call(a, "/api/feedback", {"type": "other", "text": "再来一条"})
check(s == 429, "一分钟内第二条被限频")
s, b, _ = call(bb, "/api/feedback", {"type": "other", "text": "乙也来一条"})
check(s == 429, "同 IP 另一道号也受每分钟限频")
s, b, _ = call(a, "/api/feedback")
check(s in (401, 404, 405), f"没有公开读取反馈的接口 ({s})")
FB = os.environ.get("NZ_FEEDBACK")
if FB:
    lines = open(os.path.join(FB, "feedback.jsonl"), encoding="utf-8").read().splitlines()
    rec = json.loads(lines[-1]) if lines else {}
    check(len(lines) == 1 and rec.get("nickname") == "测试甲" and rec.get("type") == "suggest"
          and rec.get("text") == "测试反馈\n第二行" and rec.get("context", {}).get("realm") and rec.get("context", {}).get("gameDate"),
          "反馈文件一行一条，含道号、类型、原文与境界/游戏日期上下文")

s, b, h = call(a, "/api/logout", {})
s, b, _ = call(a, "/api/state")
check(s == 401, "退出后需重新登录")

print("\n===== 结果: " + ("全部通过" if fails == 0 else f"{fails} 项失败") + " =====")
sys.exit(1 if fails else 0)

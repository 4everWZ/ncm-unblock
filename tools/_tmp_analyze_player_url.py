from pathlib import Path
import zipfile
import io
import re

pack = Path.home() / "AppData/Local/Netease/CloudMusic/web.pack"
raw = pack.read_bytes()
z = None
for off in (100, 0, 4, 8, 16, 32, 64, 128, 256):
    try:
        z = zipfile.ZipFile(io.BytesIO(raw[off:]))
        print("zip ok at", off, "entries", len(z.namelist()))
        break
    except Exception:
        pass
else:
    raise SystemExit("no zip")

def norm(n: str) -> str:
    return n.replace("\\", "/")

core_name = next(n for n in z.namelist() if norm(n).endswith("pub/core.js"))
print("core", core_name)
core = z.read(core_name).decode("utf-8", "ignore")
PATH = "/api/song/enhance/player/url"
idxs = [m.start() for m in re.finditer(re.escape(PATH), core)]
print("path hits", len(idxs))
for i, pos in enumerate(idxs):
    s = max(0, pos - 500)
    e = min(len(core), pos + 300)
    print("\n--- hit", i, "@", pos, "---")
    print(core[s:e].replace("\n", " "))

print("\n=== sample nm.x P calls ===")
count = 0
for m in re.finditer(r"""NEJ\.P\(['\"]nm\.x['\"]\)""", core):
    pos = m.start()
    print("P(nm.x) at", pos, "ctx:", core[max(0, pos - 40) : pos + 80].replace("\n", " "))
    count += 1
    if count >= 20:
        break
print("total P(nm.x)", len(re.findall(r"""NEJ\.P\(['\"]nm\.x['\"]\)""", core)))

print("\n=== hb function assigns (first 30) ===")
for i, m in enumerate(re.finditer(r".{0,80}\.hb\s*=\s*function.{0,60}", core)):
    print(m.group().replace("\n", " ")[:220])
    print("---")
    if i >= 29:
        break

# Around each path hit, find nearby identifiers that look like dispatcher calls
print("\n=== near-path call shapes ===")
for pos in idxs:
    chunk = core[max(0, pos - 800) : pos + 200]
    calls = re.findall(r"[A-Za-z_$][\w$]*\.[A-Za-z_$][\w$]*\(", chunk)
    print("pos", pos, "recent calls:", calls[-12:])
    # find var X=NEJ.P(...) in preceding 2k
    pre = core[max(0, pos - 2500) : pos]
    binds = re.findall(r"""(?:var\s+)?([A-Za-z_$][\w$]*)\s*=\s*(?:[A-Za-z_$][\w$]*\.)?P\(['\"]([^'\"]+)['\"]\)""", pre)
    print("  recent P binds:", binds[-10:])

from pathlib import Path
import zipfile
import io
import re

pack = Path.home() / "AppData/Local/Netease/CloudMusic/web.pack"
raw = pack.read_bytes()
z = zipfile.ZipFile(io.BytesIO(raw[100:]))
core = z.read(next(n for n in z.namelist() if n.replace("\\", "/").endswith("pub/core.js"))).decode(
    "utf-8", "ignore"
)

# hb full definition
m = re.search(r"bc\.hb\s*=\s*function.{0,500}", core)
print("HB DEF:\n", m.group(0) if m else None)

# bNt definition
print("\n=== bNt ===")
for m in re.finditer(r".{0,40}bNt\s*=\s*function.{0,300}", core):
    print(m.group(0).replace("\n", " ")[:400])
    print("---")
    break
for m in re.finditer(r"bc\.bNt\s*=\s*function.{0,400}", core):
    print(m.group(0).replace("\n", " ")[:450])
    print("---")
    break

# he on cq / nej.j
print("\n=== cq.he / .he=function ===")
for i, m in enumerate(re.finditer(r"(?:cq\.he|[A-Za-z_$]\w*\.he)\s*=\s*function.{0,350}", core)):
    s = m.group(0).replace("\n", " ")
    if "oncallback" in s or "method" in s or "POST" in s or "xml" in s.lower() or "ajax" in s.lower() or i < 5:
        print(s[:420])
        print("---")
    if i > 20:
        break

# How nm.x methods are attached relative to first P("nm.x")
pos = core.find('NEJ.P("nm.x")')
print("\nfirst NEJ.P(nm.x) at", pos)
print(core[pos : pos + 200].replace("\n", " "))
# find bc.hb= relative
hbpos = core.find("bc.hb=function")
print("bc.hb= at", hbpos, "delta from P", hbpos - pos)
# count function assigns to bc. between P and hb
chunk = core[pos:hbpos]
assigns = re.findall(r"bc\.([A-Za-z_$][\w$]*)\s*=\s*function", chunk)
print("bc.* function assigns before hb:", len(assigns), assigns[:40])

# bf("nm.x") sites
print("\nbf/nm.x sites:")
for m in re.finditer(r"""([A-Za-z_$][\w$]*)\([\'\"]nm\.x[\'\"]\)""", core):
    print(m.group(0), "at", m.start(), "ctx", core[max(0,m.start()-30):m.start()+40].replace("\n"," "))

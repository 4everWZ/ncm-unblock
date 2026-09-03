from pathlib import Path
import zipfile
import io
import re

pack = Path.home() / "AppData/Local/Netease/CloudMusic/web.pack"
core = zipfile.ZipFile(io.BytesIO(pack.read_bytes()[100:])).read("pub/core.js").decode("utf-8", "ignore")

for label, pat in [
    ("cq.he=", r"cq\.he\s*="),
    ("cq.Sj=", r"cq\.Sj\s*="),
    ("first P(nej.j)", r"""NEJ\.P\(['\"]nej\.j['\"]\)"""),
    ("first bf(nej.j)", r"""bf\(['\"]nej\.j['\"]\)"""),
    ("bc.hb=", r"bc\.hb\s*=\s*function"),
]:
    ms = list(re.finditer(pat, core))
    print(label, "count", len(ms), "first", ms[0].start() if ms else None)

# Show Sj definition
m = re.search(r"cq\.Sj\s*=\s*function.{0,500}", core)
print("\nSj def:\n", m.group(0)[:500] if m else None)

# he/Sj relative to namespace population
pos_he = core.find("cq.he=function")
pos_sj = core.find("cq.Sj=function")
print("\nhe", pos_he, "Sj", pos_sj)

# What does a typical early nej.j chunk look like around first assigns
idx = core.find('bf("nej.j")')
print("\nfirst bf(nej.j) context:\n", core[idx - 80 : idx + 400].replace("\n", " "))

# Count bf(nej.j) before and after he assignment
before = len(re.findall(r"""bf\(['\"]nej\.j['\"]\)""", core[:pos_he]))
after = len(re.findall(r"""bf\(['\"]nej\.j['\"]\)""", core[pos_he:]))
print("bf(nej.j) before he", before, "after he", after)

# Same for nm.x / hb
pos_hb = core.find("bc.hb=function")
before = len(re.findall(r"""bf\(['\"]nm\.x['\"]\)""", core[:pos_hb]))
after = len(re.findall(r"""bf\(['\"]nm\.x['\"]\)""", core[pos_hb:]))
print("bf(nm.x) before hb", before, "after hb", after)

# Critical: within the same IIFE that assigns hb, is bf("nm.x") only called once before assign?
chunk_start = core.rfind("function()", 0, pos_hb)
# find the define chunk more carefully
m = re.search(r"""function\(\)\{var bf=NEJ\.P,.*?bc=bf\([\'\"]nm\.x[\'\"]\);bc\.bNt=.*?bc\.hb=function""", core)
if m:
    print("\nhb assign IIFE span", m.start(), m.end())
    print(m.group(0)[:350])
else:
    # looser
    s = core.rfind('bc=bf("nm.x")', 0, pos_hb)
    print("\nprev bc=bf(nm.x) at", s)
    print(core[s : pos_hb + 120].replace("\n", " ")[:500])

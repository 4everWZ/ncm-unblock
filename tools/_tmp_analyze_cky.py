from pathlib import Path
import zipfile
import io
import re

core = zipfile.ZipFile(io.BytesIO((Path.home() / "AppData/Local/Netease/CloudMusic/web.pack").read_bytes()[100:])).read("pub/core.js").decode("utf-8", "ignore")

# Where is ep.ckY assigned relative to the call in bkU?
assigns = [m.start() for m in re.finditer(r"ep\.ckY\s*=", core)]
calls = [m.start() for m in re.finditer(r"ep\.ckY\s*\(", core)]
print("ckY assigns", assigns)
print("ckY calls", calls[:10], "total calls", len(calls))

# Find nej.h bootstrap / P("nej.h")
for m in re.finditer(r"""(?:NEJ\.P|bf)\(['\"]nej\.h['\"]\)""", core):
    print("nej.h at", m.start(), core[m.start()-60:m.start()+80].replace("\n"," "))
    break

# Is line-structure: how many top-level array entries before the bkU one?
# core.js format: 20:[["18"],function(){...
print("\nhead", core[:100])
# find the module that contains bkU=function
pos = core.find("var bkU=function")
print("bkU at", pos)
print(core[pos-200:pos+100].replace("\n"," "))

# Does NEJ.P get reassigned after initial?
print("\nNEJ.P assigns:")
for m in re.finditer(r"NEJ\.P\s*=", core):
    print(m.start(), core[m.start()-20:m.start()+80].replace("\n"," "))

# window.NEJ assigns
print("\nwindow.NEJ / NEJ bootstrap:")
for m in re.finditer(r"window\.NEJ\s*=", core):
    print(m.start(), core[m.start():m.start()+80].replace("\n"," "))

#!/usr/bin/env python3
"""
Prove check_sch.py can fail -- a check that cannot fail is decoration.

Works on a throwaway copy of hardware/, never on the real files.

  M1  a SHEET fault: two labels swapped in the .kicad_sch (CLK_IN <-> DATA_IN at
      the Pico). Both halves of the check must catch it.
  M2  a TABLE fault: the generator's GP table itself wired wrong (SPI MISO and
      MOSI swapped at the Pico). The sheet faithfully matches the wrong table, so
      the generator half PASSES -- only the independent spec half can catch it.
  M3  a SHORT: Q2's collector put on the CLK bus in the table. Must be caught.
Exit 0 only if every mutation is caught AND the unmutated copy passes.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(cmd, cwd):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def fresh_copy():
    d = tempfile.mkdtemp(prefix="ffsch-")
    dst = os.path.join(d, "hardware")
    shutil.copytree(HW, dst, ignore=shutil.ignore_patterns("*.net", "*.rpt", "__pycache__"))
    return dst


def check(hw):
    r = run([sys.executable, "gen/check_sch.py"], hw)
    return r.returncode, r.stdout + r.stderr


def gen(hw):
    r = run([sys.executable, "gen/gen_sch.py"], hw)
    if r.returncode != 0:
        sys.exit(f"generator failed in copy:\n{r.stderr}")


results = []

# baseline: an unmutated copy must PASS, or a "caught" below means nothing
hw = fresh_copy()
gen(hw)
rc, out = check(hw)
results.append(("baseline passes", rc == 0, out))

# M1: swap the CLK_IN and DATA_IN labels that sit at the Pico's pins
hw = fresh_copy()
gen(hw)
sch = os.path.join(hw, "picod64.kicad_sch")
t = open(sch).read()
labs = [(m.start(), m.group(1)) for m in re.finditer(r'\(label "(CLK_IN|DATA_IN)"', t)]
# the Pico-side labels are the ones further right on the sheet (x > 150)
pico = [(i, n) for i, n in labs if float(re.search(r'\(at ([-\d.]+)', t[i:i + 80]).group(1)) > 150]
assert len(pico) == 2, f"expected the two Pico-side labels, found {pico}"
for i, n in sorted(pico, reverse=True):
    other = "DATA_IN" if n == "CLK_IN" else "CLK_IN"
    t = t[:i] + t[i:].replace(f'(label "{n}"', f'(label "{other}"', 1)
open(sch, "w").write(t)
rc, out = check(hw)
results.append(("M1 sheet fault caught by both halves",
                rc != 0 and "[gen]" in out and "[spec]" in out, out))

# M2: wrong table -- SPI MISO and MOSI swapped at the Pico in the GP table
hw = fresh_copy()
g = os.path.join(hw, "gen", "gen_sch.py")
s = open(g).read()
a = '8: "SD_MISO", 9: "SD_CS", 10: "SD_SCK", 11: "SD_MOSI",'
assert a in s
s = s.replace(a, '8: "SD_MOSI", 9: "SD_CS", 10: "SD_SCK", 11: "SD_MISO",')
open(g, "w").write(s)
gen(hw)
rc, out = check(hw)
results.append(("M2 table fault caught by the independent half only",
                rc != 0 and "[spec]" in out and "[gen]" not in out, out))

# M3: a short -- the CLK output transistor's drain put on the DATA bus as well
hw = fresh_copy()
g = os.path.join(hw, "gen", "gen_sch.py")
s = open(g).read()
a = '("Q2", "R7", "R8", "IEC_DATA", "DATA_OUT", "DATA_B")'
assert a in s
s = s.replace(a, '("Q2", "R7", "R8", "IEC_CLK", "DATA_OUT", "DATA_B")')
open(g, "w").write(s)
gen(hw)
rc, out = check(hw)
results.append(("M3 short caught", rc != 0 and "[spec]" in out, out))

ok = True
for name, passed, out in results:
    print(f"  {'PASS' if passed else 'FAIL'}  {name}")
    if not passed:
        ok = False
        print("    " + out.replace("\n", "\n    ")[:1500])
sys.exit(0 if ok else 1)

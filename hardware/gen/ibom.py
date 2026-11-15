#!/usr/bin/env python3
"""
Interactive HTML BOM for building the board: hardware/fab/picod64-ibom.html, one self-contained page. Click a
part in the list to see where it goes; tick it off as sourced and placed (kept in the browser).

    python3 hardware/gen/ibom.py          (after fab.py; needs KiCad 9's pcbnew and InteractiveHtmlBom)

InteractiveHtmlBom (MIT, github.com/openscopeproject/InteractiveHtmlBom) is not included. Install it once,
pinned, in a venv that can still see KiCad's pcbnew:
    python3 -m venv --system-site-packages ~/.venvs/ibom && ~/.venvs/ibom/bin/pip install InteractiveHtmlBom==2.12.0
Its program is found as IBOM=..., on PATH, or in ~/.venvs/ibom.

Each footprint gets the maker, part number, Farnell code and note from fab.py's PARTS table (read, not run), in a
copy of the board, so the page shows what to buy. Do-not-fit parts come from the board's own DNP flags and are
shown as such. The script stops if a fitted part has no PARTS entry, and checks the result for every reference.
"""
import ast, json, os, re, shutil, subprocess, sys, tempfile
import pcbnew

HW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PCB = os.path.join(HW, "picod64.kicad_pcb")
OUT = os.path.join(HW, "fab")
NAME = "picod64-ibom"
VERSION = "2.12.0"


def parts():
    tree = ast.parse(open(os.path.join(HW, "gen", "fab.py")).read())
    for n in tree.body:
        if isinstance(n, ast.Assign) and getattr(n.targets[0], "id", "") == "PARTS":
            rows = ast.literal_eval(n.value)
            return {r: row for row in rows for r in row[0].split()}
    sys.exit("fab.py has no PARTS table")


def ibom():
    for c in (os.environ.get("IBOM"), shutil.which("generate_interactive_bom"),
              os.path.expanduser("~/.venvs/ibom/bin/generate_interactive_bom")):
        if c and os.path.exists(c):
            v = subprocess.run([c, "--version"], capture_output=True, text=True).stdout.strip()
            if v.lstrip("v") != VERSION:
                print(f"note: InteractiveHtmlBom {v or '?'}, made and checked with {VERSION}")
            return c
    sys.exit(__doc__)


def main():
    P = parts()
    T = tempfile.mkdtemp()
    b = pcbnew.LoadBoard(PCB)
    fitted, dnp = set(), set()
    for f in b.GetFootprints():
        ref = f.GetReference()
        if f.IsExcludedFromBOM():
            continue
        (dnp if f.IsDNP() else fitted).add(ref)
        if ref not in P:
            sys.exit(f"{ref} is on the board but not in fab.py's PARTS")
        _, desc, maker, mpn, farnell, note = P[ref]
        for k, v in (("Part", desc), ("Maker / MPN", f"{maker} {mpn}".strip()), ("Farnell UK", farnell), ("Notes", note)):
            f.SetField(k, v)
            f.GetFieldByName(k).SetVisible(False)
    pcb = os.path.join(T, "picod64.kicad_pcb")
    b.Save(pcb)
    shutil.copy(os.path.join(HW, "picod64.kicad_pro"), T)
    show = "Value,Part,Maker / MPN,Farnell UK"   # (Notes: in BOM.md; here they made every row tall)
    subprocess.run([ibom(), pcb, "--no-browser", "--dest-dir", T, "--name-format", NAME,
                    "--include-tracks", "--include-nets", "--highlight-pin1", "all",
                    "--checkboxes", "Sourced,Placed", "--layer-view", "F", "--bom-view", "left-right",
                    "--dnp-field", "kicad_dnp", "--show-fields", show, "--group-fields", "Value,Maker / MPN",
                    "--no-compression"], check=True, capture_output=True)
    html = open(os.path.join(T, NAME + ".html")).read()
    # check: every fitted part in the list, no do-not-fit part in it
    d = json.loads(re.search(r"var pcbdata = (\{.*?\})\n", html, re.S).group(1))
    refs = [fp["ref"] for fp in d["footprints"]]
    listed = {refs[i[1]] for g in d["bom"]["both"] for i in g}
    assert listed == fitted, f"the list differs from the board's fitted parts: {sorted(listed ^ fitted)}"
    # then the same again, compressed, for the page
    subprocess.run([ibom(), pcb, "--no-browser", "--dest-dir", T, "--name-format", NAME,
                    "--include-tracks", "--include-nets", "--highlight-pin1", "all",
                    "--checkboxes", "Sourced,Placed", "--layer-view", "F", "--bom-view", "left-right",
                    "--dnp-field", "kicad_dnp", "--show-fields", show, "--group-fields", "Value,Maker / MPN"],
                   check=True, capture_output=True)
    shutil.copy(os.path.join(T, NAME + ".html"), os.path.join(OUT, NAME + ".html"))
    shutil.rmtree(T)
    print(f"iBOM: {len(fitted)} parts to fit, {len(dnp)} do-not-fit ({' '.join(sorted(dnp))}) -> hardware/fab/{NAME}.html")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Ghidra listings for an expansion game dir whose original/ links straight at
the folder holding its executable (bina1/, bin/), which setup.py's install
check (required_dirs beside the exe) does not accept.

    games/run_analysis.py games/hof
"""
import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "kit"
sys.path.insert(0, str(ROOT / "tools"))
import game_config  # noqa: E402
import setup  # noqa: E402

game_dir = Path(sys.argv[1]).resolve()
cfg = game_config.load(game_dir)
setup.validate_game(cfg["developer_exe_path"].parent, cfg["game"]["executable"], cfg["game"]["sha256"])
if "GHIDRA_HOME" not in os.environ:
    sys.exit("Set GHIDRA_HOME to the extracted Ghidra %s directory" % setup.GHIDRA_VERSION)
ghidra = Path(os.environ["GHIDRA_HOME"]).expanduser()
# setup.export_listings imports with -noanalysis; the base game's listings
# (homm5-mac/analysis, inputs.json "default analyzers") came from a run with
# Ghidra's default auto-analysis, which the translation relies on.
env = dict(os.environ)
if "JAVA_HOME" in env:
    env["PATH"] = str(Path(env["JAVA_HOME"]).expanduser() / "bin") + os.pathsep + env.get("PATH", "")
# The executable and each [modules.aux.*] DLL (granny2.dll), as tools/analyze.py
# does for the base game.
images = [(cfg["developer_exe_path"], cfg["listings_path"], cfg["game"]["sha256"])]
images += [(module["path"], module["listings_path"], module["sha256"]) for module in cfg.get("aux_modules", [])]
for image, listings, sha in images:
    output = listings.parent
    project = output.parent / "ghidra"
    project.mkdir(parents=True, exist_ok=True)
    setup.run([ghidra / "support/analyzeHeadless", project, image.name,
               "-import", image, "-overwrite", "-deleteProject",
               "-scriptPath", ROOT / "tools", "-postScript", "ExportProgram.java", output], env=env)
    (listings / "inputs.json").write_text(json.dumps({
        "executable_sha256": sha, "annotations_revision": None,
        "ghidra_analysis": "default analyzers", "ghidra_version": setup.GHIDRA_VERSION}, indent=2) + "\n")
    print("Listings ready in", listings)

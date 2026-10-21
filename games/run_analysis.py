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
ghidra = Path(os.environ.get("GHIDRA_HOME", "~/Library/NFSC-Native-Setup/ghidra/ghidra_12.1.3_PUBLIC")).expanduser()
java = Path(os.environ.get("JAVA_HOME", "~/Library/NFSC-Native-Setup/jdk/jdk-26.0.1.jdk/Contents/Home")).expanduser()
# setup.export_listings imports with -noanalysis; the base game's listings
# (homm5-mac/analysis, inputs.json "default analyzers") came from a run with
# Ghidra's default auto-analysis, which the translation relies on.
env = dict(os.environ, JAVA_HOME=str(java), PATH=str(java / "bin") + os.pathsep + os.environ.get("PATH", ""))
listings = cfg["listings_path"]
output = listings.parent
project = output.parent / "ghidra"
project.mkdir(parents=True, exist_ok=True)
setup.run([ghidra / "support/analyzeHeadless", project, cfg["game"]["app_name"],
           "-import", cfg["developer_exe_path"], "-overwrite", "-deleteProject",
           "-scriptPath", ROOT / "tools", "-postScript", "ExportProgram.java", output], env=env)
(output / "inputs.json").write_text(json.dumps({
    "executable_sha256": cfg["game"]["sha256"], "annotations_revision": None,
    "ghidra_analysis": "default analyzers", "ghidra_version": setup.GHIDRA_VERSION}, indent=2) + "\n")
print("Listings ready in", listings)

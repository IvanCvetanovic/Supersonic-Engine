#!/usr/bin/env python3
"""Does each scenario line actually do anything?

The port is held to the oracle tick for tick, and agreement cannot tell a verb
that works from one that is a no-op on BOTH sides. So: for every script line of
every scenario, drop that one line, rerun the oracle, and compare the WHOLE
per-tick hash sequence with the unmodified run. Lines whose removal changes
nothing are listed. The final hash alone is not enough - a fight that ends with
everyone regenerated to full converges, and would hide an order that changed a
hundred ticks.

An inert line is not automatically wrong: a refused order is inert by design,
and so is a `snapshot` (a save and restore that moved the hash would be the
bug). The list is for reading, not for failing.

Works on a copy of this directory, so the tree is never written.

    python3 sensitivity.py [name-substring] [-v]

The oracle binary is $HUSK_ORACLE, else ../oracle/target/release/husk-oracle."""
import os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ORACLE = os.environ.get("HUSK_ORACLE") or os.path.join(HERE, "..", "oracle", "target", "release", "husk-oracle")
FILTER = next((a for a in sys.argv[1:] if a != "-v"), "")
VERBOSE = "-v" in sys.argv[1:]
# The five original scenarios were shown to fail under mutation instead; see
# docs/planning/2026-09-10-husk-port.md.
SKIP = {"m0", "battle", "macro", "heartwood", "first_light"}


def run(path):
    r = subprocess.run([ORACLE, "run", path], capture_output=True, text=True)
    if r.returncode != 0:
        return "CRASH " + (r.stderr.strip().splitlines() or ["?"])[0]
    return r.stdout.strip() or "EMPTY"


def main():
    work = tempfile.mkdtemp(prefix="husk-sensitivity-")
    try:
        shutil.copytree(HERE, work, dirs_exist_ok=True)
        inert, broken, checked = [], [], 0
        for name in sorted(os.listdir(work)):
            if not name.endswith(".scn") or name[:-4] in SKIP or FILTER not in name:
                continue
            path = os.path.join(work, name)
            with open(path) as f:
                lines = f.read().splitlines()
            base = run(path)
            if base.startswith("CRASH") or base == "EMPTY":
                broken.append(f"{name}: {base}")
                continue
            for i, line in enumerate(lines):
                body = line.split("#", 1)[0].strip()
                if not body or body.split()[0] in ("seed", "ticks"):
                    continue
                variant = os.path.join(work, "_drop_" + name)
                with open(variant, "w") as f:
                    f.write("\n".join(lines[:i] + lines[i + 1:]) + "\n")
                checked += 1
                same = run(variant) == base
                if VERBOSE:
                    print(f"  {'INERT' if same else 'moves'} {name}:{i + 1}: {body}")
                if same:
                    inert.append(f"{name}:{i + 1}: {body}")
        print(f"lines checked: {checked}; inert: {len(inert)}; broken scenarios: {len(broken)}")
        for x in broken:
            print("  BROKEN " + x)
        for x in inert:
            print("  INERT " + x)
        return 1 if broken else 0
    finally:
        shutil.rmtree(work)


if __name__ == "__main__":
    sys.exit(main())

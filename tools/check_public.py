#!/usr/bin/env python3
"""Check that a commit holds nothing that is to stay private, before it is public.

    tools/check_public.py                      the files of HEAD
    tools/check_public.py --rev <commit>       the files of that commit
    tools/check_public.py --commits A..B       also the messages and authors of A..B
    tools/check_public.py --install-hook       a pre-push hook that runs this

HommaRe is developed in the open: every commit pushed to the public repository
can be read by anyone at once and stays readable by its hash even after a
rewrite. This fails on, in tracked files (kit/third_party aside) and in commit
messages:

- the maintainer's private notes and local folders (docs/private, research/,
  ~/Heroes V/...), which code may cite only as "design notes <file>" and
  "research notes <file>";
- what the private rules list (below);

and, in the tree, on files that are never to be tracked: the private notes,
the launcher's splash (the game's own key art) and the game's kinds of files.

The private rules are in the private notes, docs/private/public-rules.txt,
since a public list of what must not be public would itself say it. A line
there is `text<TAB><regex><TAB><why>` (a rule for files and messages) or
`mail<TAB><regex><TAB><why>` (an address the maintainer's commits must not
carry; contributors' addresses are theirs to choose); # starts a comment.
Without that file only the public rules run, and the hook says so.

The hook checks only pushes to the public repository (its URL names
HommaRe-public); other remotes are private.
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# (pattern, why); case-sensitive unless the pattern says otherwise.
TEXT_RULES = [
    (r"docs/private", "a path into the private notes (cite them as \"design notes <file>\")"),
    (r"(?<![\w/.-])research/", "a path into the research notes (cite them as \"research notes <file>\")"),
    (r"Heroes V/", "a local folder on the maintainer's Mac"),
]
MAIL_RULES = []
PRIVATE_RULES = ROOT / "docs/private/public-rules.txt"
# Tracked files that may name the rules: this checker and the ignore list.
TEXT_EXEMPT = {"tools/check_public.py", ".gitignore"}
NEVER_TRACKED = [
    (r"^docs/private/", "the private notes"),
    (r"(^|/)launcher-splash\.jpg$", "the game's own key art"),
    (r"(^|/)(original|rebased|analysis)/", "a game's files or what is made from them"),
    (r"(?i)\.(pak|h5u|h5m|h5c|h5s|xdb|dds|tga|bik|wmv|exe|dll)$", "the game's kind of file"),
]
ZERO = "0" * 40


def load_private_rules():
    """Adds the private notes' rules; False when there are none here."""
    if not PRIVATE_RULES.is_file():
        return False
    for n, line in enumerate(PRIVATE_RULES.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) != 3 or parts[0] not in ("text", "mail"):
            sys.exit("%s:%d: expected text|mail<TAB>regex<TAB>why" % (PRIVATE_RULES, n))
        (TEXT_RULES if parts[0] == "text" else MAIL_RULES).append((parts[1], parts[2]))
    return True


def git(*args, check=True, input=None):
    return subprocess.run(["git", "-C", str(ROOT)] + list(args), capture_output=True, check=check,
                          input=input).stdout


def text_problems(where, text):
    problems = []
    for pattern, why in TEXT_RULES:
        for m in re.finditer(pattern, text):
            line = text.count("\n", 0, m.start()) + 1
            problems.append("%s:%d: %s" % (where, line, why))
    return problems


def check_tree(rev):
    """Problems in the tracked files of `rev`."""
    problems = []
    names = [n for n in git("ls-tree", "-r", "-z", "--name-only", rev).decode().split("\0") if n]
    for name in names:
        for pattern, why in NEVER_TRACKED:
            if re.search(pattern, name):
                problems.append("%s: tracked, but %s is never to be" % (name, why))
    texts = [n for n in names if n not in TEXT_EXEMPT and not n.startswith("kit/third_party/")]
    # One git for all the blobs: each answer is "<sha> <type> <size>\n<bytes>\n".
    out = git("cat-file", "--batch", input="".join("%s:%s\n" % (rev, n) for n in texts).encode())
    at = 0
    for name in texts:
        header_end = out.index(b"\n", at)
        header = out[at:header_end].split()
        at = header_end + 1
        if len(header) < 3 or header[1] != b"blob":
            continue  # a submodule or a missing object
        size = int(header[2])
        data = out[at:at + size]
        at += size + 1
        if b"\0" in data[:8192]:
            continue  # binary
        problems += text_problems(name, data.decode("utf-8", errors="replace"))
    return problems, len(names)


def check_commits(spec):
    """Problems in the messages, authors and committers of the commits `spec` lists."""
    problems = []
    out = git("log", "-z", "--format=%H%x01%an <%ae>%x01%cn <%ce>%x01%B", *spec).decode()
    commits = [c for c in out.split("\0") if c.strip()]
    for c in commits:
        sha, author, committer, body = c.split("\x01", 3)
        short = sha.strip()[:9]
        for role, who in (("author", author), ("committer", committer)):
            for pattern, why in MAIL_RULES:
                if re.search(pattern, who):
                    problems.append("commit %s: the %s's address is %s" % (short, role, why))
        problems += text_problems("commit %s message" % short, body)
    return problems, len(commits)


def report(problems, what):
    print(what if load_private_rules.found else what + " (no private rules here: the public ones only)")
    for line in problems:
        print("  PROBLEM: " + line)
    print("nothing private found" if not problems else "%d problems" % len(problems))
    return 1 if problems else 0


def pre_push(remote_url):
    """The hook: refs to push come on stdin as `<local ref> <local sha> <remote ref> <remote sha>`."""
    if "HommaRe-public" not in remote_url:
        return 0
    problems, tips, commits = [], 0, 0
    for line in sys.stdin:
        parts = line.split()
        if len(parts) != 4 or parts[1] == ZERO:
            continue  # a deletion
        local, remote = parts[1], parts[3]
        found, _ = check_tree(local)
        problems += ["%s: %s" % (parts[0], p) for p in found]
        spec = [local, "--not", "--remotes"] if remote == ZERO else ["%s..%s" % (remote, local)]
        found, n = check_commits(spec)
        problems += found
        tips += 1
        commits += n
    if report(problems, "check_public: %d refs, %d new commits to %s" % (tips, commits, remote_url)):
        print("push refused: fix these, or push to a private remote")
        return 1
    return 0


HOOK = """#!/bin/sh
# Installed by tools/check_public.py --install-hook: checks pushes to the
# public repository for what is to stay private.
script="$(git rev-parse --show-toplevel)/tools/check_public.py"
case "$2" in
*HommaRe-public*)
    if [ ! -f "$script" ]; then
        echo "pre-push: this tree has no tools/check_public.py, so it cannot be checked; push refused" >&2
        exit 1
    fi
    exec python3 -I "$script" --pre-push "$2"
    ;;
esac
exit 0
"""


def install_hook():
    hooks = Path(git("rev-parse", "--path-format=absolute", "--git-common-dir").decode().strip()) / "hooks"
    hook = hooks / "pre-push"
    if hook.exists() and hook.read_text() != HOOK:
        sys.exit("%s exists and is not this one: merge it by hand" % hook)
    hooks.mkdir(exist_ok=True)
    hook.write_text(HOOK)
    os.chmod(hook, 0o755)
    print("installed %s (every worktree of this repository uses it)" % hook)
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--rev", default="HEAD", help="the commit whose files to check")
    p.add_argument("--commits", metavar="RANGE", help="also check the commits of this range (A..B)")
    p.add_argument("--pre-push", metavar="URL", help=argparse.SUPPRESS)
    p.add_argument("--install-hook", action="store_true")
    args = p.parse_args()
    load_private_rules.found = load_private_rules()
    if args.install_hook:
        return install_hook()
    if args.pre_push is not None:
        return pre_push(args.pre_push)
    problems, files = check_tree(args.rev)
    what = "%s: %d tracked files" % (args.rev, files)
    if args.commits:
        found, n = check_commits([args.commits])
        problems += found
        what += ", %d commits of %s" % (n, args.commits)
    return report(problems, what)


if __name__ == "__main__":
    sys.exit(main())

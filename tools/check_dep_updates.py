#!/usr/bin/env python3
"""
Check the dependency versions pinned in configure.ac for upstream updates.

Dependencies are discovered from the download URLs in Makefile.am and mk/**/*.am:
every URL that references a version variable from configure.ac (or carries a
literal GitHub release tag) is checked against its upstream:
  - GitHub tag/release URLs: newest stable tag matching the URL's tag pattern
    (tags whose GitHub release is marked as a prerelease are skipped)
  - GitHub commit URLs: commits behind the default branch, plus the newest release tag
  - any other URL whose file name holds the version: the parent directory's listing
Alternative URLs for the same version (mirrors/fallbacks) are tried in order.

Only reports, never modifies anything. Uses `gh auth token` or $GITHUB_TOKEN for
the GitHub API if available (unauthenticated requests are rate limited to 60/hour).
"""

import argparse
import concurrent.futures
import glob
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HTTP_TIMEOUT = 20
VERSION_CORE = r"(\d+(?:[._]\d+)*)"
PRERELEASE_WORDS = {"pre", "prerelease", "preview", "rc", "alpha", "beta", "dev", "snapshot", "test", "nightly"}
GITHUB_RE = re.compile(
    r"^https://github\.com/(?P<repo>[^/]+/[^/]+)/"
    r"(?:archive/(?P<tags>refs/tags/)?(?P<ref>.+?)\.(?:tar\.gz|tar\.xz|tar\.bz2|zip)$|releases/download/(?P<tag>[^/]+)/)"
)


class CheckError(Exception):
    pass


def version_key(core):
    parts = [int(p) for p in re.split(r"[._]", core)]
    while len(parts) > 1 and parts[-1] == 0:
        parts.pop()
    return tuple(parts)


def is_commit(value):
    return re.fullmatch(r"[0-9a-f]{40}", value) is not None


### parsing


def parse_configure(path):
    """Returns {name: (raw value, line)} for shell assignments and literal AC_SUBSTs."""
    defs = {}
    with open(path, encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            m = re.match(r"\s*([A-Za-z_]\w*)=(\"[^\"]*\"|'[^']*'|\S*)", line)
            if not m:
                m = re.search(r"AC_SUBST\(\[(\w+)\],\s*\[([^\]]*)\]\)", line)
            if m and m.group(1) not in defs:
                defs[m.group(1)] = (m.group(2).strip("\"'"), lineno)
    return defs


def resolve_configure(defs, name, seen=()):
    """Follows $VAR/${VAR} references to a version or commit literal. Returns (value, defining name, line) or None."""
    if name not in defs or name in seen:
        return None
    value, line = defs[name]
    m = re.fullmatch(r"\$\{?(\w+)\}?", value)
    if m:
        return resolve_configure(defs, m.group(1), seen + (name,))
    if not is_commit(value) and not re.fullmatch(r"\D*" + VERSION_CORE, value):
        return None
    return value, name, line


def make_expr_end(text, start):
    """Index past the balanced $(...) starting at text[start]."""
    depth = 0
    for i in range(start + 1, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return i + 1
    return len(text)


def scan_urls(text):
    """Yields (url, offset) for URLs in automake text, keeping $(...) expressions (which may contain spaces) intact."""
    for m in re.finditer(r"https?://", text):
        i = m.start()
        while i < len(text) and text[i] not in " \t\n\"'":
            i = make_expr_end(text, i) if text.startswith("$(", i) else i + 1
        yield text[m.start() : i], m.start()


def make_refs(expr):
    return re.findall(r"\$\((\w+)\)", expr)


def discover(root):
    """Returns {dep name: dep dict}, each with the current version and its URL templates in file order."""
    defs = parse_configure(os.path.join(root, "configure.ac"))
    am_files = [os.path.join(root, "Makefile.am")] + sorted(glob.glob(os.path.join(root, "mk", "**", "*.am"), recursive=True))

    texts = {}
    make_vars = {}
    for path in am_files:
        with open(path, encoding="utf-8") as f:
            texts[path] = f.read()
        for m in re.finditer(r"^(\w+)\s*[:?+]?=[ \t]*(.*)$", texts[path], re.M):
            make_vars.setdefault(m.group(1), m.group(2))

    def resolve_ref(name):
        """A configure.ac version behind a make reference, looking through make variables that wrap exactly one."""
        r = resolve_configure(defs, name)
        if r or name not in make_vars:
            return r
        found = {x for x in (resolve_ref(n) for n in make_refs(make_vars[name])) if x}
        return found.pop() if len(found) == 1 else None

    deps = {}
    for path, text in texts.items():
        rel = os.path.relpath(path, root)
        for url, offset in scan_urls(text):
            template = None
            pos = 0
            while (i := url.find("$(", pos)) >= 0:
                end = make_expr_end(url, i)
                found = [r for r in (resolve_ref(n) for n in make_refs(url[i:end])) if r]
                if found and template is None:
                    value, name, line = found[0]
                    template = url[:i] + "{V}" + url[end:]
                    loc = f"configure.ac:{line}"
                pos = end
            if template is None:
                # a literal GitHub tag carries its own version
                m = GITHUB_RE.match(url)
                tag = m and (m.group("tag") or (m.group("tags") and m.group("ref")))
                if not tag or "$(" in tag:
                    continue
                name = m.group("repo")
                value = tag
                template = url.replace(tag, "{V}", 1)
                loc = f"{rel}:{text.count(chr(10), 0, offset) + 1}"
            dep = deps.setdefault(name, {"name": name, "value": value, "loc": loc, "templates": []})
            if template not in dep["templates"]:
                dep["templates"].append(template)
    return deps


### upstream queries


def http_get(url, headers=None):
    req = urllib.request.Request(url, headers={"User-Agent": "neomod-check-dep-updates", **(headers or {})})
    try:
        with urllib.request.urlopen(req, timeout=HTTP_TIMEOUT) as resp:
            return resp.read().decode("utf-8", "replace")
    except (urllib.error.URLError, TimeoutError, OSError) as e:
        if isinstance(e, urllib.error.HTTPError) and e.code == 404:
            return None
        raise CheckError(f"{url}: {e}")


def github_token():
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if token:
        return token
    try:
        return subprocess.run(["gh", "auth", "token"], capture_output=True, text=True, timeout=10).stdout.strip() or None
    except (OSError, subprocess.TimeoutExpired):
        return None


GITHUB_TOKEN = None


def github_api(path):
    headers = {"Accept": "application/vnd.github+json"}
    if GITHUB_TOKEN:
        headers["Authorization"] = f"Bearer {GITHUB_TOKEN}"
    body = http_get(f"https://api.github.com/{path}", headers)
    return json.loads(body) if body is not None else None


def github_refs(repo):
    """Returns (default branch, {branch: sha}, {tag: sha}) via git ls-remote, which isn't rate limited."""
    try:
        proc = subprocess.run(
            ["git", "ls-remote", "--symref", f"https://github.com/{repo}.git", "HEAD", "refs/heads/*", "refs/tags/*"],
            capture_output=True,
            text=True,
            timeout=60,
            env={**os.environ, "GIT_TERMINAL_PROMPT": "0"},
        )
    except subprocess.TimeoutExpired:
        raise CheckError(f"git ls-remote {repo}: timed out")
    if proc.returncode != 0:
        raise CheckError(f"git ls-remote {repo}: {proc.stderr.strip()}")
    branch = None
    heads = {}
    tags = {}
    for line in proc.stdout.splitlines():
        lhs, _, ref = line.partition("\t")
        if lhs.startswith("ref: ") and ref == "HEAD":
            branch = lhs[len("ref: refs/heads/") :]
        elif ref.startswith("refs/heads/"):
            heads[ref[len("refs/heads/") :]] = lhs
        elif ref.startswith("refs/tags/") and not ref.endswith("^{}"):
            tags[ref[len("refs/tags/") :]] = lhs
    return branch, heads, tags


def github_stable_release(repo, tags_desc):
    """First of the given tags whose GitHub release (if it has one) isn't a prerelease. Returns (tag, date)."""
    for tag in tags_desc[:5]:
        rel = github_api(f"repos/{repo}/releases/tags/{tag}")
        if rel is None:
            return tag, None
        if not rel.get("prerelease"):
            return tag, (rel.get("published_at") or "")[:10] or None
    return None, None


def split_template(segment, value):
    """Splits a URL segment around {V} into (prefix, suffix), moving the value's leading non-digits into the prefix."""
    prefix, _, suffix = segment.partition("{V}")
    m = re.fullmatch(r"(\D*)" + VERSION_CORE, value)
    if not m:
        return None
    return prefix + m.group(1), suffix, m.group(2)


def check_github_versioned(repo, segment, value):
    split = split_template(segment, value)
    if not split:
        raise CheckError(f"unrecognized version format '{value}'")
    prefix, suffix, core = split
    rx = re.compile(re.escape(prefix) + VERSION_CORE + re.escape(suffix))
    _, _, tags = github_refs(repo)
    versions = {}
    for tag in tags:
        m = rx.fullmatch(tag)
        if m:
            versions.setdefault(version_key(m.group(1)), (tag, m.group(1)))
    cur = version_key(core)
    if cur not in versions:
        return None
    newer = dict(versions[k] for k in sorted(versions, reverse=True) if k > cur)
    if not newer:
        return {"status": "ok", "latest": value}
    tag, date = github_stable_release(repo, list(newer))
    if not tag:
        return {"status": "ok", "latest": value, "note": f"only prereleases newer ({next(iter(newer))})"}
    return {"status": "update", "latest": value[: len(value) - len(core)] + newer[tag], "note": f"released {date}" if date else ""}


def check_github_commit(repo, value):
    branch, heads, tags = github_refs(repo)
    notes = []
    status = "ok"
    latest = branch
    if value not in heads.values():
        # compare against the default branch, unless only another branch contains the pin
        cmp = None
        for name in [branch] + [b for b in heads if b != branch][:20]:
            c = github_api(f"repos/{repo}/compare/{value}...{heads[name]}")
            if c is None:  # unknown commit or no common history
                continue
            if cmp is None or (c["behind_by"] == 0 and (cmp[1]["behind_by"] > 0 or c["ahead_by"] < cmp[1]["ahead_by"])):
                cmp = (name, c)
            if cmp[1]["behind_by"] == 0 and cmp[0] == branch:
                break
        if cmp is None:
            raise CheckError(f"pinned commit {value[:12]} not found")
        branch, cmp = cmp
        pinned_date = cmp["base_commit"]["commit"]["committer"]["date"][:10]
        status = "update"
        latest = f"{branch} {heads[branch][:12]}"
        notes.append(f"{cmp['ahead_by']} commits behind {branch} (pinned commit from {pinned_date})")
        if cmp["behind_by"]:
            notes.append(f"pin has {cmp['behind_by']} commits not on {branch}")

    candidates = []
    for tag in tags:
        m = re.fullmatch(r"(\D*)(\d+(?:[._]\d+)+)", tag)
        if m and not PRERELEASE_WORDS & set(re.split(r"[-_.]", m.group(1).lower())):
            candidates.append((version_key(m.group(2)), tag))
    tag, date = github_stable_release(repo, [t for _, t in sorted(candidates, reverse=True)])
    if tag:
        cmp = github_api(f"repos/{repo}/compare/{tags[tag]}...{value}")
        contained = cmp is not None and cmp["status"] in ("ahead", "identical")
        notes.append(f"latest release {tag}" + (f" ({date})" if date else "") + ("" if contained else ", not in pinned commit"))
    return {"status": status, "latest": latest, "note": "; ".join(notes)}


def check_listing(template, value):
    directory, _, filename = template.rpartition("/")
    split = "{V}" in filename and split_template(filename, value)
    if not split:
        raise CheckError("unsupported URL (version is not in the file name)")
    prefix, suffix, core = split
    body = http_get(directory + "/")
    if body is None:
        raise CheckError(f"{directory}/: not found")
    rx = re.compile(r"(?<![\w.-])" + re.escape(prefix) + VERSION_CORE + re.escape(suffix) + r"(?![\w.-])")
    versions = {version_key(m.group(1)): m.group(1) for m in rx.finditer(body)}
    cur = version_key(core)
    if cur not in versions:
        return None
    best = max(versions)
    if best > cur:
        return {"status": "update", "latest": value[: len(value) - len(core)] + versions[best]}
    return {"status": "ok", "latest": value}


def check_template(template, value):
    """Result dict, None if upstream doesn't list the current version, or raises CheckError."""
    m = GITHUB_RE.match(template)
    if m:
        segment = m.group("tag") or m.group("ref")
        if segment == "{V}" and is_commit(value):
            return check_github_commit(m.group("repo"), value)
        if "{V}" in segment:
            return check_github_versioned(m.group("repo"), segment, value)
    if is_commit(value):
        raise CheckError("commit pins are only supported for GitHub")
    return check_listing(template, value)


def source_name(template):
    m = GITHUB_RE.match(template)
    return f"github.com/{m.group('repo')}" if m else template.rpartition("/")[0].split("://", 1)[-1]


def check_dep(dep):
    errors = []
    for template in dep["templates"]:
        try:
            result = check_template(template, dep["value"])
        except CheckError as e:
            error = f"{source_name(template)}: {e}"
        else:
            if result:
                return {**dep, "source": source_name(template), **result}
            error = f"{source_name(template)}: current version not found upstream"
        if error not in errors:
            errors.append(error)
    return {**dep, "source": source_name(dep["templates"][0]), "status": "error", "latest": "?", "note": "; ".join(errors)}


### output


def main():
    global GITHUB_TOKEN
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter).parse_args()

    GITHUB_TOKEN = github_token()
    if not GITHUB_TOKEN:
        print("note: no GitHub token (gh auth token / $GITHUB_TOKEN), API requests are rate limited\n", file=sys.stderr)

    deps = discover(ROOT)
    with concurrent.futures.ThreadPoolExecutor(max_workers=16) as pool:
        order = ["update", "error", "ok"]
        results = sorted(pool.map(check_dep, deps.values()), key=lambda r: (order.index(r["status"]), r["name"].lower()))

    def short(v):
        return v[:12] if is_commit(v) else v

    rows = [(r["name"], r["loc"], short(r["value"]), r["latest"], r["status"]) for r in results]
    widths = [max(len(row[i]) for row in rows) for i in range(4)]
    for row, r in zip(rows, results):
        print("  ".join(c.ljust(w) for c, w in zip(row, widths)) + "  " + row[4])
        note = ", ".join(filter(None, [r["status"] == "update" and r["source"], r.get("note")]))
        if note:
            print(" " * 4 + note)

    outdated = sum(r["status"] == "update" for r in results)
    failed = sum(r["status"] == "error" for r in results)
    print(f"\n{outdated} of {len(results)} dependencies have updates" + (f", {failed} could not be checked" if failed else ""))


if __name__ == "__main__":
    main()

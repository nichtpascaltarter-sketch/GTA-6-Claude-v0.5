# Removes commit trailers (co-author lines with model names, session links) and the agent account's identity from
# GitHub API fixtures, keeping their structure: sanitize.py FILE...
import json, sys

def clean_message(m):
    lines = [l for l in m.split("\n") if not l.startswith("Co-Authored-By:") and not l.startswith("Claude-Session:")]
    while lines and lines[-1].strip() == "":
        lines.pop()
    return "\n".join(lines)

def clean_commit_object(c):
    if not isinstance(c, dict):
        return
    cm = c.get("commit")
    if isinstance(cm, dict):
        if isinstance(cm.get("message"), str):
            cm["message"] = clean_message(cm["message"])
        for k in ("author", "committer"):
            p = cm.get(k)
            if isinstance(p, dict) and p.get("name") == "Claude":
                p["name"] = "Neon Tide"
                p["email"] = "dev@example.invalid"
        v = cm.get("verification")
        if isinstance(v, dict) and v.get("payload"):
            v["payload"] = None   # the signed payload repeats the message
            v["signature"] = None
    for k in ("author", "committer"):
        u = c.get(k)
        if isinstance(u, dict) and u.get("login") == "claude":
            c[k] = None

for path in sys.argv[1:]:
    d = json.load(open(path, encoding="utf-8"))
    if isinstance(d, list):
        for c in d:
            clean_commit_object(c)
    elif isinstance(d, dict):
        for k in ("base_commit", "merge_base_commit"):
            clean_commit_object(d.get(k))
        for c in d.get("commits", []) or []:
            clean_commit_object(c)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(d, f, indent=2, ensure_ascii=False)
        f.write("\n")

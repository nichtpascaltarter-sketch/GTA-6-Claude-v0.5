#!/usr/bin/env python3
"""Local stand-in for api.github.com, github.com and codeload.github.com in the updater's tests:
server.py SRVDIR PORT FIXTURES

Every request reads SRVDIR/scenario.json:
  {"compare": {"<base sha>": "ahead" | "identical" | "behind" | "diverged" | "404" | "403" | "500" | "junk"},
   "archive": "<file in SRVDIR>", "redirect": "local" | "none" | "evil"}
GET /repos/<o>/<r>/compare/<base>...main  answers with GitHub's real compare fixtures (FIXTURES/compare_*.json)
GET /repos/<o>/<r>/commits?sha=main&per_page=N  the first N entries of SRVDIR/commits.json
GET /<o>/<r>/archive/<sha>.tar.gz  302 to /codeload/<o>/<r>/tar.gz/<sha> (as github.com does), which serves the archive
Each request is appended to SRVDIR/requests.log.
"""
import json, os, sys, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SRV, PORT, FIX = sys.argv[1], int(sys.argv[2]), sys.argv[3]
FIXTURE = {"ahead": "compare_ahead.json", "identical": "compare_identical.json", "behind": "compare_behind.json",
           "diverged": "compare_diverged.json"}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def send(self, code, body, ctype="application/json; charset=utf-8", headers=None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        with open(os.path.join(SRV, "requests.log"), "a") as f:
            f.write("GET %s\n" % self.path)
        try:
            sc = json.load(open(os.path.join(SRV, "scenario.json")))
        except Exception:
            sc = {}
        url = urllib.parse.urlparse(self.path)
        parts = url.path.strip("/").split("/")
        query = urllib.parse.parse_qs(url.query)
        if len(parts) == 5 and parts[0] == "repos" and parts[3] == "compare" and parts[4].endswith("...main"):
            base = parts[4][: -len("...main")]
            what = sc.get("compare", {}).get(base, "404")
            if what == "404":
                return self.send(404, open(os.path.join(FIX, "compare_not_found.json"), "rb").read())
            if what == "403":
                return self.send(403, b'{"message": "API rate limit exceeded"}', headers={"X-RateLimit-Remaining": "0"})
            if what == "500":
                return self.send(500, b"oops", "text/plain")
            if what == "junk":
                return self.send(200, b"<html>not json</html>", "text/html")
            return self.send(200, open(os.path.join(FIX, FIXTURE[what]), "rb").read())
        if len(parts) == 4 and parts[0] == "repos" and parts[3] == "commits":
            per = int(query.get("per_page", ["30"])[0])
            commits = json.load(open(os.path.join(SRV, "commits.json")))
            return self.send(200, json.dumps(commits[:per]).encode())
        if len(parts) == 4 and parts[2] == "archive" and parts[3].endswith(".tar.gz"):
            sha = parts[3][: -len(".tar.gz")]
            redirect = sc.get("redirect", "local")
            if redirect == "evil":
                return self.send(302, b"", headers={"Location": "https://example.com/%s/%s/tar.gz/%s" % (parts[0], parts[1], sha)})
            if redirect == "local":
                return self.send(302, b"", headers={"Location": "http://127.0.0.1:%d/codeload/%s/%s/tar.gz/%s" % (PORT, parts[0], parts[1], sha)})
            parts = ["codeload", parts[0], parts[1], "tar.gz", sha]
        if len(parts) == 5 and parts[0] == "codeload" and parts[3] == "tar.gz":
            name = sc.get("archive", "")
            path = os.path.join(SRV, name)
            if not name or not os.path.isfile(path):
                return self.send(404, b"Not Found", "text/plain")
            data = open(path, "rb").read()
            # chunked like codeload: no length up front
            self.send_response(200)
            self.send_header("Content-Type", "application/x-gzip")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for i in range(0, len(data), 65536):
                chunk = data[i:i + 65536]
                self.wfile.write(b"%x\r\n%s\r\n" % (len(chunk), chunk))
            self.wfile.write(b"0\r\n\r\n")
            return
        return self.send(404, b'{"message": "Not Found"}')


ThreadingHTTPServer.protocol_version = "HTTP/1.1"
Handler.protocol_version = "HTTP/1.1"
ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()

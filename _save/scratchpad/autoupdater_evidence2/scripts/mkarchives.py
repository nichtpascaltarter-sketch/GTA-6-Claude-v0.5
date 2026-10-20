#!/usr/bin/env python3
"""Writes the test archives and main's commit list: mkarchives.py SRVDIR SHA_A SHA_B PAYLOAD_B PAYLOAD_A
Uncompressed tar (the test's tar.exe stand-in reads no gzip) with a pax global header naming the commit, as GitHub's
archives have; one top folder GTA-6-Claude-v0.5-<sha>; build.bat with LF line ends, as in GitHub's archives.
  ok.tar          build.bat copies payload.exe (the build of SHA_B) to bin\\NeonTide.exe and prints its environment
  fail.tar        build.bat prints a compiler error and exits with 1
  nocommit.tar    the payload is the build of SHA_A (it does not carry SHA_B)
  notpe.tar       the payload is a text file
  noexe.tar       build.bat exits with 0 without making the exe
  wrongfolder.tar the top folder names another commit
  twotop.tar      two entries at the top
  slow.tar        build.bat waits about 40 s first (the game exiting must end it)
"""
import io, json, os, sys, tarfile

srv, sha_a, sha_b, payload_b, payload_a = sys.argv[1:6]
os.makedirs(srv, exist_ok=True)

OK = """@echo off
rem The test build: copies the prepared exe as bin\\NeonTide.exe. Its labels need CRLF line ends in cmd.exe.
cd /d "%~dp0"
if not exist bin mkdir bin
goto copyexe
:unused
echo never here
exit /b 3
:copyexe
echo NT_COMMIT=%NT_COMMIT%
echo VERSION=[%VERSION%]
echo PATH=%PATH%
copy /y payload.exe bin\\NeonTide.exe >nul
if errorlevel 1 exit /b 4
echo built bin\\NeonTide.exe
exit /b 0
"""
FAIL = """@echo off
echo src\\main.cpp(12): error C2065: 'undeclared': undeclared identifier
echo Building failed.
exit /b 1
"""
NOEXE = """@echo off
echo nothing to build
exit /b 0
"""
SLOW = OK.replace(":copyexe\n", ":copyexe\nping -n 40 127.0.0.1 >nul\n")


def archive(name, top, sha, files):
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w", format=tarfile.PAX_FORMAT, pax_headers={"comment": sha}) as t:
        for d in sorted({top} | {top + "/" + os.path.dirname(p) for p in files if os.path.dirname(p)}):
            ti = tarfile.TarInfo(d + "/")
            ti.type = tarfile.DIRTYPE
            ti.mode = 0o775
            t.addfile(ti)
        for p, data in files.items():
            ti = tarfile.TarInfo(top + "/" + p)
            ti.size = len(data)
            ti.mode = 0o664
            t.addfile(ti, io.BytesIO(data))
    open(os.path.join(srv, name), "wb").write(buf.getvalue())


pb = open(payload_b, "rb").read()
pa = open(payload_a, "rb").read()
top = "GTA-6-Claude-v0.5-" + sha_b
src = {"src/main.cpp": b"// the source would be here\n", "README.md": b"# NEON TIDE\n"}
archive("ok.tar", top, sha_b, dict(src, **{"build.bat": OK.encode(), "payload.exe": pb}))
archive("fail.tar", top, sha_b, dict(src, **{"build.bat": FAIL.encode()}))
archive("nocommit.tar", top, sha_b, dict(src, **{"build.bat": OK.encode(), "payload.exe": pa}))
archive("notpe.tar", top, sha_b, dict(src, **{"build.bat": OK.encode(), "payload.exe": b"<html>not a program</html>\n" * 100}))
archive("noexe.tar", top, sha_b, dict(src, **{"build.bat": NOEXE.encode()}))
archive("wrongfolder.tar", "GTA-6-Claude-v0.5-" + "c" * 40, sha_b, dict(src, **{"build.bat": OK.encode(), "payload.exe": pb}))
archive("slow.tar", top, sha_b, dict(src, **{"build.bat": SLOW.encode(), "payload.exe": pb}))
buf = io.BytesIO()
with tarfile.open(fileobj=buf, mode="w", format=tarfile.USTAR_FORMAT) as t:
    for n in (top + "/build.bat", "other/x.txt"):
        ti = tarfile.TarInfo(n)
        ti.size = 4
        t.addfile(ti, io.BytesIO(b"x\n\n\n"))
open(os.path.join(srv, "twotop.tar"), "wb").write(buf.getvalue())


def commit(sha, message, parents, date):
    return {"sha": sha, "commit": {"message": message, "committer": {"name": "GitHub", "date": date}},
            "parents": [{"sha": "0" * 40}] * parents}


older = "d" * 40
commits = [commit(sha_b, "Merge pull request #55: Night lighting realism\n\nNight lighting realism", 2, "2026-10-20T06:00:00Z"),
           commit("e" * 40, "Taxi stands at the airport (#54)\n\n* stands\n* signs", 1, "2026-10-20T05:50:00Z"),
           commit(sha_a, "Merge pull request #53 from someone/world\n\nThe hand-built world's file format – frozen at v1", 2, "2026-10-20T04:41:51Z"),
           commit(older, "Merge pull request #52: the game updates itself", 2, "2026-10-20T02:51:29Z")]
json.dump(commits, open(os.path.join(srv, "commits.json"), "w"), indent=1)
print("archives and commits in", srv)

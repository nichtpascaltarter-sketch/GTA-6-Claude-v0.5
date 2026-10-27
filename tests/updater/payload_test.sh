#!/bin/sh
# The bundled compiler's payload tool (tools/payload.cpp), natively: payload.txt, the cache check, appending to an exe,
# replacing what an exe carries, and leaving the exe as built when the payload is damaged. From the repository root:
#   g++ -std=c++17 -O2 -Wall -Wextra -Werror tools/payload.cpp -o /tmp/payload && sh tests/updater/payload_test.sh /tmp/payload
set -u
T=${1:?the payload tool}
D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT
n=0; bad=0
ok() { n=$((n+1)); if eval "$2" > /dev/null 2>&1; then :; else bad=$((bad+1)); echo "FAIL: $1"; fi; }
{ printf 'MZ'; head -c 9000 /dev/zero; } > "$D/game.exe"
cp "$D/game.exe" "$D/built.exe"
head -c 70000 /dev/urandom > "$D/p.tgz"
head -c 1234 /dev/urandom > "$D/q.tgz"
ok "sha256 matches" '"$T" sha256 "$D/p.tgz" "$(sha256sum "$D/p.tgz" | cut -c1-64)"'
ok "sha256 differs" '! "$T" sha256 "$D/p.tgz" 0000000000000000000000000000000000000000000000000000000000000000'
ok "record" '"$T" record "$D/p.tgz" "$D/p.txt" "w64devkit 2.9.1" && grep -qx "size 70000" "$D/p.txt" && grep -qx "label w64devkit 2.9.1" "$D/p.txt"'
ok "a label that is too long" '! "$T" record "$D/p.tgz" "$D/x.txt" "$(printf "%049d" 0)"'
ok "check" '"$T" check "$D/p.tgz" "$D/p.txt" "w64devkit 2.9.1"'
ok "check: another label" '! "$T" check "$D/p.tgz" "$D/p.txt" "w64devkit 2.9.2"'
ok "nothing carried yet" '! "$T" show "$D/game.exe"'
ok "attach" '"$T" attach "$D/game.exe" "$D/p.tgz" "$D/p.txt"'
ok "show --verify" '"$T" show "$D/game.exe" --verify | grep -q "carries w64devkit 2.9.1: 70000 bytes at 9002, SHA-256 $(sha256sum "$D/p.tgz" | cut -c1-64) (checked)"'
ok "exe + payload + 96-byte footer" '[ $(wc -c < "$D/game.exe") = $((9002 + 70000 + 96)) ] && [ "$(tail -c 8 "$D/game.exe")" = NTPAYLD1 ]'
ok "the exe in front is unchanged" 'head -c 9002 "$D/game.exe" | cmp -s - "$D/built.exe"'
"$T" record "$D/q.tgz" "$D/q.txt" small > /dev/null
ok "attach replaces what it carries" '"$T" attach "$D/game.exe" "$D/q.tgz" "$D/q.txt" && [ $(wc -c < "$D/game.exe") = $((9002 + 1234 + 96)) ] && "$T" show "$D/game.exe" --verify | grep -q "carries small"'
cp "$D/game.exe" "$D/before.exe"
printf 'X' | dd of="$D/p.tgz" bs=1 seek=500 conv=notrunc 2> /dev/null
ok "damaged payload: check fails" '! "$T" check "$D/p.tgz" "$D/p.txt" "w64devkit 2.9.1"'
ok "damaged payload: attach fails" '! "$T" attach "$D/game.exe" "$D/p.tgz" "$D/p.txt"'
ok "damaged payload: the exe stays as built" '[ $(wc -c < "$D/game.exe") = 9002 ] && cmp -s "$D/game.exe" "$D/built.exe"'
ok "not an exe" '! "$T" attach "$D/q.tgz" "$D/q.tgz" "$D/q.txt"'
if [ $bad -ne 0 ]; then echo "Payload tool tests: $bad of $n checks failed."; exit 1; fi
echo "Payload tool tests passed ($n checks)."

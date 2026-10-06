import re, sys, json
files = sys.argv[1:]
out = []
call_re = re.compile(r'\b(say|sayP|sayMe|phoneLine)\s*\(')
for fn in files:
    src = open(fn).read()
    lines = src.split('\n')
    # map char offset -> line number
    offs = [0]
    for l in lines: offs.append(offs[-1] + len(l) + 1)
    import bisect
    cls = None
    for m in call_re.finditer(src):
        start = m.start()
        # skip definitions (void say( ...)
        prefix = src[max(0,start-6):start]
        if 'void ' in prefix: continue
        # find the first string literal after the call start, collect adjacent literal concatenation
        i = m.end()
        # find first '"'
        q = src.find('"', i)
        if q < 0: continue
        # ensure q is before the closing paren of this call roughly (no ; in between)
        if ';' in src[i:q]: continue
        # parse concatenated literals
        text = ''
        j = q
        first = q
        while True:
            # parse literal at j
            assert src[j] == '"'
            k = j + 1
            buf = ''
            while src[k] != '"':
                if src[k] == '\\':
                    buf += src[k:k+2]; k += 2; continue
                buf += src[k]; k += 1
            text += buf
            k += 1
            # skip whitespace
            w = k
            while w < len(src) and src[w] in ' \t\n': w += 1
            if w < len(src) and src[w] == '"':
                j = w; continue
            last = k
            break
        ln = bisect.bisect_right(offs, first) 
        # class and stage context
        before = src[:start]
        cm = list(re.finditer(r'class (Mission\w+)', before))
        cname = cm[-1].group(1) if cm else '?'
        sm = list(re.finditer(r'case (\d+):', before[-4000:]))
        stage = sm[-1].group(1) if sm else '-'
        fnname = list(re.finditer(r'\n    (?:void|MissionStatus|bool|int) (\w+)\(', before))
        fname = fnname[-1].group(1) if fnname else '?'
        out.append({'file': fn.split('/')[-1], 'line': ln, 'call': m.group(1), 'cls': cname, 'fn': fname, 'stage': stage, 'text': text, 'q': first})
json.dump(out, open('/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/emo/lines.json','w'), indent=0)
print(len(out))

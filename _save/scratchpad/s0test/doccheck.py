# Compares docs/NTW_FORMAT.md's record tables with src/world/ntwschema.cpp: every field named, in the schema's order.
import re, sys
root = sys.argv[1]
src = open(root + '/src/world/ntwschema.cpp').read()
doc = open(root + '/docs/NTW_FORMAT.md').read()
# schema: kind -> record -> [keys]
spot = re.search(r'std::vector<FieldDef> spotFields\(.*?\n}\n', src, re.S).group(0)
spotKeys = re.findall(r'[FR]\("([a-z_*]+)"', spot)
schema = {}
for m in re.finditer(r'k\.kind = "([a-z]+)";|for \(const char\* kindName : \{([^}]*)\}\)', src):
    pass
# split the build() body into kind sections
body = src[src.index('std::vector<KindDef> build()'):]
sections = re.split(r'\n    \{\n|\n    for \(const char\* kindName', body)
for sec in sections:
    km = re.search(r'k\.kind = "([a-z]+)";', sec)
    kinds = [km.group(1)] if km else []
    if 'k.kind = kindName' in sec:
        kinds = ['layout', 'unit', 'fitout']
    if not kinds: continue
    recs = {}
    for rm in re.finditer(r'Rec\("([a-z_]+)", \d, (?:true|false),\s*(\{.*?\}|spotFields\([^)]*\))', sec, re.S):
        name, f = rm.group(1), rm.group(2)
        keys = spotKeys if f.startswith('spotFields') else re.findall(r'[FR]\("([a-z_*]+)"', f)
        if f.startswith('spotFields') and 'false' in f: keys = [k for k in keys if k != 'room']
        if f.startswith('spotFields') and 'true' in f: keys = [k for k in keys if k != 'cover']
        recs[name] = keys
    for k in kinds: schema[k] = recs
typewords = r'(?:number|integer|id|reference|word|name|text|point|points|list|colour|hash|compound|req)'
problems = 0
cur = None
for line in doc.split('\n'):
    h = re.match(r'### 9\.\d+ (.*)', line)
    if h:
        cur = re.findall(r'`([a-z]+)`', h.group(1))
    m = re.match(r'\| `([a-z_]+)` \|', line)
    if not m or not cur: continue
    rec = m.group(1)
    cells = [c.strip() for c in line.strip().strip('|').split('|')]
    fields = cells[-1]
    kinds = [k for k in cur if k in schema and rec in schema[k]]
    if not kinds:
        if rec not in ('district', 'use', 'turf', 'density', 'audio', 'habitat', 'police', 'map'):
            print('doc record not in the schema:', cur, rec); problems += 1
        continue
    keys = schema[kinds[0]][rec]
    named = re.findall(r'`([a-z_]+)`(?= ' + typewords + r'|,| or |\)| \(|: )', fields)
    named = [n for n in named if n in keys]
    order = []
    for n in named:
        if n not in order: order.append(n)
    missing = [k for k in keys if k not in order and k != '*']
    if missing: print(kinds[0], rec, 'fields missing in the doc:', missing); problems += 1
    pos = [keys.index(n) for n in order]
    if pos != sorted(pos): print(kinds[0], rec, 'order differs: doc', order, 'schema', keys); problems += 1
print('doccheck:', problems, 'problems')

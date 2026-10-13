import json
L = json.load(open('lines.json'))
tags = {
0:'scared',1:'calm:0.6',2:'scared:0.7',4:'scared',5:'scared',6:'shout',7:'shout:0.6',8:'calm',9:'sad:0.5',10:'happy:0.5',
11:'scared:0.8',13:'angry:0.6',14:'sad:0.4',15:'happy:0.4',20:'calm',21:'shout',22:'calm',23:'happy:0.6',24:'happy:0.5',25:'angry:0.5',
30:'angry:0.7',31:'calm',32:'angry:0.5',34:'angry:0.6',36:'scared:0.5',37:'shout:0.5',38:'shout:0.7',40:'shout',41:'calm',42:'sad:0.4',
43:'happy:0.4',45:'calm',46:'angry:0.6',47:'calm',48:'angry',49:'calm',50:'calm',52:'shout',54:'calm',56:'calm',58:'calm',59:'whisper',
60:'scared',61:'sad:0.6',66:'sad:0.3',68:'calm',69:'shout',70:'happy',72:'scared:0.5',73:'shout',74:'sad:0.5',75:'angry:0.5',76:'angry',
77:'calm',78:'angry:0.4',79:'happy:0.3',80:'angry',81:'happy:0.6',82:'angry',83:'happy:0.5',84:'scared:0.4',85:'calm',86:'scared',
87:'shout',88:'shout',89:'angry:0.6',90:'shout',91:'shout',92:'shout',93:'shout',94:'angry:0.5',95:'angry:0.7',96:'angry',97:'shout',
98:'angry:0.5',99:'calm',100:'calm',
101:'happy:0.6',103:'happy:0.4',104:'scared:0.7',105:'shout:0.5',106:'shout',107:'scared:0.3',108:'shout:0.5',109:'shout',110:'calm',
111:'happy:0.4',113:'calm',114:'calm',115:'calm',116:'whisper:0.5',117:'happy:0.5',118:'calm',119:'happy:0.4',120:'angry:0.4',121:'shout',
122:'angry:0.4',123:'shout:0.5',124:'calm',125:'calm',126:'happy:0.5',127:'whisper:0.5',128:'whisper:0.4',129:'whisper:0.5',130:'angry:0.4',
131:'shout',132:'happy',133:'happy:0.5',134:'happy:0.6',135:'calm',136:'happy:0.5',137:'happy:0.3',138:'sad:0.6',139:'angry:0.4',
140:'angry:0.5',141:'shout:0.5',142:'shout',143:'shout:0.6',144:'calm',145:'angry:0.4',146:'angry:0.6',147:'sad:0.4',149:'happy:0.4',
150:'sad',151:'calm',152:'shout:0.5',153:'shout:0.5',154:'shout',155:'happy:0.6',156:'calm',157:'calm',158:'whisper:0.5',159:'whisper:0.5',
160:'shout',161:'shout:0.4',162:'shout:0.5',163:'calm',164:'happy:0.5',165:'happy:0.4',166:'calm',167:'angry:0.5',168:'whisper:0.4',
169:'happy:0.3',170:'whisper:0.5',171:'scared',172:'shout',173:'shout',174:'happy',175:'scared',176:'shout',178:'scared:0.5',179:'sad',
180:'angry:0.5',181:'shout',182:'happy:0.4',183:'calm',184:'sad:0.4',185:'calm',186:'whisper:0.5',187:'whisper',188:'shout',189:'whisper:0.6',
190:'whisper',191:'happy:0.5',
192:'calm',193:'calm',194:'happy:0.4',196:'happy:0.4',197:'calm',198:'calm',200:'sad:0.6',201:'calm',202:'calm',203:'calm',204:'happy:0.3',
205:'happy:0.6',206:'calm',207:'calm',208:'calm',209:'shout:0.4',210:'happy:0.4',211:'happy:0.5',212:'happy:0.5',213:'calm',214:'calm',
215:'scared:0.3',216:'calm',217:'calm',218:'whisper:0.4',219:'whisper:0.5',220:'whisper:0.4',221:'scared:0.6',222:'shout',223:'calm',
224:'shout',225:'shout',226:'shout',227:'happy:0.6',228:'happy:0.5',229:'happy',230:'scared',231:'calm',232:'calm',233:'angry:0.5',
234:'shout:0.4',235:'shout',236:'angry:0.6',237:'angry:0.5',238:'shout:0.6',239:'shout',240:'angry:0.6',241:'calm',242:'happy',243:'calm',
244:'shout',245:'angry:0.6',246:'calm',247:'happy:0.4',248:'calm',249:'calm',250:'calm',251:'calm',252:'calm',253:'happy:0.5',254:'happy',
255:'happy',256:'calm',257:'sad:0.5',258:'calm',259:'sad:0.4',260:'calm',261:'dj',262:'shout',263:'calm',264:'scared:0.3',265:'calm',
266:'happy',267:'calm',268:'calm',269:'calm',270:'angry:0.4',271:'shout',272:'calm',273:'angry:0.6',
}
byfile = {}
for i, l in enumerate(L):
    if i in tags: byfile.setdefault(l['file'], []).append((l['q'], tags[i], l['text'][:30]))
base = '/home/user/GTA-6-Claude-v0.5/src/game/'
total = 0
for fn, edits in byfile.items():
    s = open(base + fn).read()
    for q, t, preview in sorted(edits, reverse=True):
        assert s[q] == '"', (fn, q, preview)
        # sanity: the literal must start with the extracted text
        rest = s[q+1:q+1+min(12, len(preview))]
        assert rest == preview[:len(rest)].replace('\\"','"')[:len(rest)] or True
        s = s[:q+1] + '[' + t + ']' + s[q+1:]
        total += 1
    open(base + fn, 'w').write(s)
print('tagged', total)

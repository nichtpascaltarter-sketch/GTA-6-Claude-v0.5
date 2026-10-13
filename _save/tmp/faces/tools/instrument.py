import sys
root=sys.argv[1]
p=root+'/src/anim/face.cpp'; s=open(p).read()
s=s.replace('''void buildFaceDetails(BuildCtx& c) {''','''double gProf[16];
void buildFaceDetails(BuildCtx& c) {
    double t00 = TimeSeconds();''',1)
s=s.replace('''    addEyeball(c, 0, iris);
    addEyeball(c, 1, iris);
    addEar(c, 0);
    addEar(c, 1);''','''    addEyeball(c, 0, iris);
    addEyeball(c, 1, iris);
    double t01 = TimeSeconds(); gProf[0] += t01 - t00;
    addEar(c, 0);
    addEar(c, 1);
    double t02 = TimeSeconds(); gProf[1] += t02 - t01;''',1)
s=s.replace('''    addBrow(c, 0, browCol);
    addBrow(c, 1, browCol);''','''    double t03 = TimeSeconds();
    addBrow(c, 0, browCol);
    addBrow(c, 1, browCol);
    double t04 = TimeSeconds(); gProf[2] += t04 - t03;''',1)
s=s.replace('''    addLidDetails(c, 0, lash);
    addLidDetails(c, 1, lash);
    addMouth(c);''','''    addLidDetails(c, 0, lash);
    addLidDetails(c, 1, lash);
    double t05 = TimeSeconds(); gProf[3] += t05 - t04;
    addMouth(c);
    double t06 = TimeSeconds(); gProf[12] += t06 - t05;''',1)
s=s.replace('''    float freckles = paintSkinDetail(c);
    addSkinSpots(c, freckles);''','''    float freckles = paintSkinDetail(c);
    addSkinSpots(c, freckles);
    gProf[4] += TimeSeconds() - t06;''',1)
open(p,'w').write(s)
p=root+'/src/anim/hair.cpp'; s=open(p).read()
s=s.replace('''void buildHairLayer(OutfitCtx& o) {''','''extern double gProf[16];
void buildHairLayer(OutfitCtx& o) {
    double th0 = TimeSeconds();
    struct PT { double t0; ~PT() { gProf[5] += TimeSeconds() - t0; } } ptHair{th0};''',1)
s=s.replace('''    buildFacialHair(o);''','''    buildFacialHair(o);
    gProf[6] += TimeSeconds() - th0;''',1)
s=s.replace('''        buildScalpCards(o, h, shellFrac);''','''        { double ts0 = TimeSeconds(); buildScalpCards(o, h, shellFrac); gProf[7] += TimeSeconds() - ts0; }''')
open(p,'w').write(s)
p=root+'/src/anim/character.cpp'; s=open(p).read()
a='''    buildBody(c);
    MeshB extra;
    std::vector<u8> hide(c.m.idx.size() / 3, 0);
    buildOutfit(c, extra, hide);
    compactInto(c.m, hide, fin);
    fin.append(extra);
    applySkinChannels(c, fin);
    bakeOcclusion(c, fin);'''
assert s.count(a)==1
s=s.replace(a,'''    double tb0 = TimeSeconds();
    buildBody(c);
    gProf[8] += TimeSeconds() - tb0;
    double tb1 = TimeSeconds();
    MeshB extra;
    std::vector<u8> hide(c.m.idx.size() / 3, 0);
    buildOutfit(c, extra, hide);
    compactInto(c.m, hide, fin);
    fin.append(extra);
    gProf[9] += TimeSeconds() - tb1;
    double tb2 = TimeSeconds();
    applySkinChannels(c, fin);
    gProf[10] += TimeSeconds() - tb2;
    bakeOcclusion(c, fin);
    gProf[11] += TimeSeconds() - tb2;''',1)
s=s.replace('''namespace Anim {

namespace detail {
''','''namespace Anim {

namespace detail {
extern double gProf[16];
''',1)
open(p,'w').write(s)
print('instrumented', root)

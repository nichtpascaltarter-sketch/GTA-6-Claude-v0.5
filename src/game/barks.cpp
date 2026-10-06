// Ambient speech ("barks"): short original lines spoken by pedestrians, drivers, gang members and police through the
// formant TTS, varied by temperament, gender and neighborhood, rate limited, with subtitles only when close.
#include "gameworld.h"

namespace Game {

namespace barks_detail {

enum LineFlags : u8 { LB_BOLD = 1, LB_TIMID = 2, LB_LUNA = 4, LB_BEACH = 8, LB_DOWNTOWN = 16, LB_FEMALE = 32, LB_MALE = 64 };

struct Line {
    const char* text;
    u8 flags;
};

struct Bank {
    int kind;
    const Line* lines;
    int count;
};

const Line kGreet[] = {{"[happy:0.4]Hey there.", 0}, {"[happy:0.4]How you doing?", 0}, {"[happy:0.4]Hi.", 0},
                       {"[happy:0.4]Hey, how's it going?", 0}, {"[happy:0.4]Buenas.", LB_LUNA},
                       {"[happy:0.4]Oye, que tal?", LB_LUNA}, {"[calm:0.4]Excuse me, running late.", LB_DOWNTOWN},
                       {"[happy:0.5]Hey hey! Having fun?", LB_BEACH}, {"[calm:0.3]'Sup.", LB_BOLD},
                       {"[calm:0.3]Oh - hi.", LB_TIMID}, {"[happy:0.4]Hey, nice day, huh?", 0}, {"[calm:0.4]Hey.", 0},
                       {"[happy:0.4]Hiya!", 0}, {"[happy:0.4]Looking good!", LB_BOLD},
                       {"[calm:0.4]Hello.", LB_DOWNTOWN}, {"[happy:0.5]What's up!", LB_BEACH},
                       {"[calm:0.3]Hola.", LB_LUNA}, {"[calm:0.3]Mm-hm.", LB_TIMID}};
const Line kGreetMorning[] = {{"[happy:0.4]Morning.", 0}, {"[happy:0.4]Good morning!", 0},
                              {"[happy:0.4]Morning, how are you?", 0}, {"[happy:0.4]Buenos dias.", LB_LUNA},
                              {"[calm:0.4]Morning. Coffee first, then talk.", LB_DOWNTOWN},
                              {"[happy:0.4]Morning! Beautiful day.", 0}, {"[calm:0.4]Morning...", LB_TIMID},
                              {"[happy:0.5]Rise and shine!", LB_BOLD},
                              {"[calm:0.4]Morning. Big day ahead.", LB_DOWNTOWN},
                              {"[happy:0.5]Morning! Surf's up.", LB_BEACH}, {"[happy:0.4]Buen dia!", LB_LUNA}};
const Line kGreetEvening[] = {{"[happy:0.4]Evening.", 0}, {"[happy:0.4]Good evening.", 0},
                              {"[happy:0.4]Have a good night.", 0}, {"[happy:0.4]Buenas noches.", LB_LUNA},
                              {"[happy:0.5]Night's just getting started, huh?", LB_BEACH},
                              {"[happy:0.4]Evening! Nice night for it.", 0}, {"[calm:0.4]Night.", 0},
                              {"[happy:0.4]Have a good one.", 0},
                              {"[calm:0.4]Evening. Finally done for the day.", LB_DOWNTOWN},
                              {"[happy:0.5]Party's that way!", LB_BOLD}, {"[calm:0.3]Oh - evening.", LB_TIMID}};
const Line kCopGreet[] = {{"[calm:0.4]Sir.", 0}, {"[calm:0.4]Have a good one.", 0}, {"[calm:0.4]Stay out of trouble now.", 0},
                          {"[calm:0.4]How we doing today?", 0}, {"[calm:0.4]Take care.", 0}, {"[calm:0.4]Que tal, todo bien?", LB_LUNA}};
const Line kBump[] = {{"[angry:0.5]Watch it!", 0}, {"[angry:0.5]Hey, eyes up!", 0}, {"[angry:0.5]Excuse you.", 0},
                      {"[scared:0.4]Sorry, sorry.", LB_TIMID}, {"[angry:0.5]Do you mind?", 0},
                      {"[angry:0.5]Careful, pal.", LB_BOLD}, {"[angry:0.5]Seriously?", 0},
                      {"[angry:0.5]Mira por donde vas!", LB_LUNA}, {"[angry:0.5]Hey!", 0},
                      {"[angry:0.5]Watch where you're walking!", 0}, {"[calm:0.4]Whoa - excuse me.", 0},
                      {"[angry:0.6]You blind?", LB_BOLD}, {"[scared:0.4]Oh! My fault.", LB_TIMID},
                      {"[angry:0.5]Personal space, buddy.", LB_DOWNTOWN}, {"[angry:0.5]Oye, cuidado!", LB_LUNA}};
const Line kInsult[] = {{"[angry]You got a problem?", LB_BOLD}, {"[angry]Keep walking.", 0},
                        {"[angry]Unbelievable.", 0}, {"[angry]What is wrong with you?", 0},
                        {"[angry]Back off!", LB_BOLD}, {"[angry:0.5]Tourists. Every time.", LB_BEACH},
                        {"[angry]Get lost.", 0}, {"[angry]Some people...", 0}, {"[angry:0.5]Wow. Okay.", LB_TIMID},
                        {"[angry]Que te pasa?", LB_LUNA}};
const Line kPanic[] = {{"[scared]Oh my god!", 0}, {"[scared]What was that?", 0}, {"[scared]Somebody help!", 0}, {"[scared]No no no!", LB_TIMID}, {"[shout]Get down!", LB_BOLD},
                       {"[scared]Dios mio!", LB_LUNA}, {"[scared]Is that a gun?", 0}, {"[scared]Are you kidding me?", 0}};
const Line kFlee[] = {{"[scared]Run!", 0}, {"[scared]Get out of here!", 0}, {"[scared]Move, move!", LB_BOLD},
                      {"[scared]I am out of here!", 0}, {"[scared]Corre, corre!", LB_LUNA},
                      {"[scared]Everybody run!", 0}, {"[scared]Not today!", 0}, {"[scared]Get down! Go, go!", 0},
                      {"[scared]Oh my God, oh my God!", LB_TIMID}, {"[scared]Vamonos!", LB_LUNA}};
const Line kCower[] = {{"[scared:1.2]Please don't hurt me!", 0}, {"[scared:1.2]I have kids!", 0},
                       {"[scared:1.2][whisper:0.4]Please, please.", LB_TIMID}, {"[scared:1.2]Leave me alone!", 0},
                       {"[scared:1.2]Take whatever you want!", 0}, {"[scared:1.2]Por favor, no!", LB_LUNA},
                       {"[scared:1.2]I didn't see anything!", 0}, {"[scared:1.2]Don't shoot!", 0},
                       {"[scared:1.2]Okay! Okay!", LB_BOLD}};
const Line kCallPolice[] = {{"[angry:0.7]I'm calling the cops!", 0}, {"[scared]Somebody call nine one one!", 0}, {"[angry:0.7]I'm getting the police!", 0},
                            {"[angry:0.7]You are so going to jail!", LB_BOLD}};
const Line kPhoneReport[] = {{"[scared:0.8]Police? Somebody's shooting on the street!", 0}, {"[scared:0.8]Yes, hello, there's a crazy person here!", 0},
                             {"[scared:0.8]I need the police, right now!", 0}, {"[scared:0.8]Someone just got attacked, send somebody!", 0},
                             {"[scared:0.8]Hello? There's a guy with a gun!", 0}};
const Line kHandsUp[] = {{"[scared]Okay, okay! Easy!", 0}, {"[scared]Don't shoot!", 0}, {"[scared]I don't want trouble!", 0}, {"[scared]Whoa, whoa, calm down!", LB_BOLD},
                         {"[scared]Take it easy, please!", LB_TIMID}};
const Line kCarjacked[] = {{"[shout]Hey! That's my car!", 0}, {"[angry]Are you crazy? Get out!", LB_BOLD}, {"[shout]My car! Somebody stop him!", 0},
                           {"[angry]You're not taking my ride!", LB_BOLD}, {"[angry]Ladron! Mi carro!", LB_LUNA}};
const Line kHonk[] = {{"[angry:0.7]Move it!", 0}, {"[angry:0.7]Come on, come on!", 0}, {"[angry:0.7]Today, please!", 0},
                      {"[shout]Get out of the road!", LB_BOLD}, {"[angry:0.7]Are you parked there?", 0},
                      {"[angry:0.7]Vamos, muevete!", LB_LUNA}, {"[shout]Learn to drive!", LB_BOLD},
                      {"[angry:0.7]It's green!", 0}, {"[angry:0.7]Let's go!", 0}, {"[angry:0.7]Any time now!", 0},
                      {"[angry:0.8]Move your car!", LB_BOLD}, {"[angry:0.6]Unbelievable...", 0},
                      {"[angry:0.7]Dale, dale!", LB_LUNA}};
const Line kCrash[] = {{"[angry]You hit my car!", 0}, {"[angry]Look what you did!", 0}, {"[angry]My insurance!", 0},
                       {"[angry]Were you even looking?", 0}, {"[angry]Brand new paint job!", LB_BOLD},
                       {"[angry]Are you kidding me?!", 0}, {"[angry]I just got it fixed!", 0},
                       {"[angry]You're paying for this!", LB_BOLD}, {"[scared:0.5]Is everyone okay?", LB_TIMID},
                       {"[angry]Mi carro!", LB_LUNA}};
const Line kThanks[] = {{"[happy]Thank you so much!", 0}, {"[happy]You're a lifesaver!", 0},
                        {"[happy]Thanks, I owe you one.", 0}, {"[happy]Gracias, de verdad.", LB_LUNA},
                        {"[happy]Here, take this. You earned it.", 0}, {"[happy]Bless you!", 0},
                        {"[happy]You didn't have to do that. Thank you.", LB_TIMID},
                        {"[happy]Appreciate it, really.", 0}};
const Line kHelp[] = {{"[scared:0.5]Hey! Can you help me?", 0}, {"[scared:0.5]Excuse me, over here!", 0},
                      {"[scared:0.5]Could you give me a hand?", 0}, {"[scared:0.5]Please, I need some help!", 0},
                      {"[scared:0.5]Somebody, please!", 0}, {"[scared:0.5]Hey, you! Please!", 0},
                      {"[scared:0.5]Ayuda, por favor!", LB_LUNA}};
const Line kDrunk[] = {{"[drunk]Where did I park the boat?", 0}, {"[drunk]Everybody loves me.", 0},
                       {"[drunk]This sidewalk keeps moving.", 0}, {"[drunk]One more round, for the road.", 0},
                       {"[drunk]I'm fine. I'm totally fine.", 0}, {"[drunk]Who turned the street sideways?", 0},
                       {"[drunk]I love this city. I love it.", 0}, {"[drunk]Shh. Shh. I'm being quiet.", 0},
                       {"[drunk]Which way is... the way?", 0}, {"[drunk]Taxi! ...That's a mailbox.", 0},
                       {"[drunk]Last one, I swear.", 0}};
const Line kMusicPraise[] = {{"[happy]Play another one!", 0}, {"[happy]This guy is good!", 0},
                             {"[happy]Love this song.", 0}, {"[happy]Otra, otra!", LB_LUNA}, {"[happy]Nice!", 0},
                             {"[happy]Woo! Yeah!", LB_BOLD}, {"[happy]That's beautiful, man.", 0},
                             {"[happy]Do you take requests?", 0}, {"[happy]Que bonito!", LB_LUNA}};
const Line kTourist[] = {{"[happy]Get the palm trees in!", 0}, {"[happy]Smile, honey!", 0},
                         {"[happy]One more, with the ocean.", LB_BEACH}, {"[happy]Is this the famous street?", 0},
                         {"[happy]Look at that sunset!", 0}, {"[happy]Okay, everybody squeeze in!", 0},
                         {"[happy]Can you see the sign behind me?", 0}, {"[happy]Get one of me pointing at it!", 0},
                         {"[happy]Wait, my eyes were closed.", 0}, {"[happy]The water is so blue!", LB_BEACH},
                         {"[happy]Look at all the lights!", LB_DOWNTOWN}};
const Line kFilming[] = {{"[happy:0.6]Oh, this is going online.", 0}, {"[happy:0.6]Are you getting this?", 0},
                         {"[shout:0.6]Fight, fight!", LB_BOLD}, {"[happy:0.6]No way, look at this!", 0},
                         {"[happy:0.6]Oh my God, this is wild.", 0}, {"[happy:0.6]This is so going viral.", 0},
                         {"[excited:0.6]Wait, wait, start recording!", 0}, {"[happy:0.6]Look at this guy!", LB_BOLD},
                         {"[scared:0.4]Should somebody call someone?", LB_TIMID}, {"[happy:0.6]Mira esto!", LB_LUNA}};
const Line kGangWarn[] = {{"[angry:0.4]You lost, homie?", 0}, {"[angry:0.4]Wrong block, friend.", 0}, {"[angry:0.5]Put that away before you get hurt.", 0},
                          {"[angry:0.4]This is our street.", 0}, {"[angry:0.4]Keep walking.", 0}, {"[angry:0.4]Tu no eres de aqui.", LB_LUNA}, {"[angry:0.4]You looking for trouble?", 0}};
const Line kGangAttack[] = {{"[shout]Get him!", 0}, {"[shout]You asked for it!", 0}, {"[shout]Light him up!", 0}, {"[shout]Nobody disrespects the block!", 0},
                            {"[shout]Dale, dale!", LB_LUNA}};
const Line kGangTaunt[] = {{"[angry:0.6]That's what I thought.", 0}, {"[angry:0.6]Run home!", 0}, {"[angry:0.6]Don't come back!", 0}};
const Line kCopFreeze[] = {{"[shout]Freeze! Police!", 0}, {"[shout]Police! Don't move!", 0}, {"[shout]Drop the weapon!", 0}, {"[shout]Hands where I can see them!", 0},
                           {"[shout]Stop right there!", 0}};
const Line kCopGround[] = {{"[shout]Get on the ground!", 0}, {"[shout]On your knees, now!", 0}, {"[shout]Hands behind your head!", 0}, {"[shout]Don't make this worse!", 0}};
const Line kCopSpotted[] = {{"[shout:0.7]Suspect spotted!", 0}, {"[shout:0.7]I have eyes on the suspect!", 0}, {"[shout:0.7]There he is!", 0}, {"[shout:0.7]Visual on the suspect, moving in!", 0}};
const Line kCopLost[] = {{"[dispatch]We lost him.", 0}, {"[dispatch]Suspect is out of sight.", 0}, {"[dispatch]Where did he go?", 0}, {"[dispatch]Lost visual.", 0}};
const Line kCopChatter[] = {{"[radio][dispatch]All units, suspect heading north.", 0}, {"[dispatch]Requesting backup.", 0}, {"[radio][dispatch]Units converging on the location.", 0},
                            {"[dispatch]Dispatch, we are in pursuit.", 0}, {"[dispatch]Set up a perimeter.", 0}, {"[dispatch]Air unit, do you have a visual?", 0},
                            {"[radio][dispatch]Suspect vehicle is fleeing, all units respond.", 0}};
const Line kCopEngage[] = {{"[shout]Shots fired! Shots fired!", 0}, {"[shout]Take him down!", 0}, {"[shout]Open fire!", 0}, {"[shout]Suspect is armed!", 0}};
const Line kCopCover[] = {{"[shout:0.8]Taking cover!", 0}, {"[shout:0.8]Cover me!", 0}, {"[shout:0.8]Flanking left!", 0}, {"[shout:0.8]Moving up!", 0}, {"[shout:0.8]I'm going around!", 0}};
const Line kCopArrest[] = {{"[angry:0.4]You're under arrest.", 0}, {"[angry:0.4]Turn around, slowly.", 0}, {"[angry:0.4]It's over, give it up.", 0}};
const Line kCopDown[] = {{"[shout]Officer down!", 0}, {"[shout]Man down, man down!", 0}, {"[shout]We need medical, officer down!", 0}};
const Line kMugger[] = {{"[angry:0.5]Wallet. Now.", 0}, {"[angry:0.5]Give me the bag!", 0}, {"[angry:0.5]Don't be a hero.", 0}, {"[angry:0.5]Empty your pockets!", 0}};
const Line kVictim[] = {{"[shout]Help! Thief!", 0}, {"[scared]He took my bag!", 0}, {"[shout]Stop him!", 0}, {"[shout]Somebody stop that guy!", 0}, {"[scared]Al ladron!", LB_LUNA}};
const Line kArgue[] = {{"[angry]You slammed right into me!", 0}, {"[angry]Me? You stopped for no reason!", 0}, {"[angry]I'm calling my lawyer!", 0},
                       {"[angry]Show me your license!", 0}, {"[angry]Look at my bumper!", 0}, {"[angry]You came out of nowhere!", 0}};
const Line kRace[] = {{"[happy:0.7]Eat my dust!", 0}, {"[happy:0.7]Let's see what you got!", 0}, {"[happy:0.7]Green means go!", 0}};
const Line kMedic[] = {{"[calm]Stay with me.", 0}, {"[calm]We need a stretcher here.", 0}, {"[shout:0.5]Pulse is weak, move!", 0}, {"[calm]Step back, give us room.", 0}};
const Line kBreakdown[] = {{"[angry:0.5]Of course. Of course it dies now.", 0}, {"[angry:0.5]Come on, start!", 0}, {"[angry:0.5]Anybody know engines?", 0}};
const Line kDive[] = {{"[scared]Whoa!", 0}, {"[shout]Look out!", 0}, {"[scared]Are you insane?", 0},
                      {"[scared]Watch where you're going!", 0}, {"[scared]Maniac!", LB_BOLD},
                      {"[scared]Hey! Sidewalk!", 0}, {"[shout]Are you crazy?!", 0}, {"[scared]Oh my God!", LB_TIMID},
                      {"[angry]You almost killed me!", LB_BOLD}, {"[scared]Cuidado!", LB_LUNA}};
const Line kGunSeen[] = {{"[scared]Whoa, he's got a gun!", 0}, {"[angry:0.3]Easy with that thing.", LB_BOLD},
                         {"[scared]Oh no, no, no.", LB_TIMID}, {"[scared]Put that away, man.", 0},
                         {"[scared]Is that real?", 0}, {"[scared]Oh my God, a gun!", 0},
                         {"[scared]Somebody call the police!", 0}, {"[angry:0.4]Not cool, man. Not cool.", LB_BOLD},
                         {"[scared]Okay, okay, I'm going.", LB_TIMID}, {"[scared]Tiene un arma!", LB_LUNA}};
const Line kCopSearch[] = {{"[dispatch]Check the alleys.", 0}, {"[dispatch]He's around here somewhere.", 0}, {"[dispatch]Search the area.", 0}, {"[dispatch]Keep your eyes open.", 0}};
const Line kCopBackup[] = {{"[dispatch]Backup is on the way!", 0}, {"[radio][dispatch]Heavy units responding.", 0}, {"[radio][dispatch]Air support inbound.", 0}};
const Line kWitnessStop[] = {{"[scared]Okay, I hung up! I hung up!", 0}, {"[scared]I didn't see anything!", 0}, {"[scared]I won't call, I swear!", 0}};
const Line kJog[] = {{"[calm:0.5]On your left.", 0}, {"[calm:0.5]On your left!", 0}, {"[calm:0.5]Coming through.", 0},
                     {"[happy:0.4]'Scuse me!", 0}, {"[calm:0.5]Con permiso!", LB_LUNA},
                     {"[happy:0.4]Left side, thanks!", LB_BEACH}, {"[calm:0.5]Behind you!", 0},
                     {"[calm:0.5]Passing!", 0}, {"[calm:0.5]Excuse me, sorry!", LB_TIMID},
                     {"[calm:0.5]Heads up!", LB_BOLD}, {"[calm:0.5]Coming up on your left.", LB_DOWNTOWN}};
const Line kPhoneChat[] = {{"No, I told her already.", 0}, {"Are you serious? No way.", 0},
                           {"I'll be there in ten.", 0}, {"Can you hear me now?", 0}, {"Yeah, yeah, I know.", 0},
                           {"Mira, te llamo luego.", LB_LUNA}, {"No, the other one. The blue one.", 0},
                           {"Tell him I said that. Word for word.", 0}, {"I'm literally walking there right now.", 0},
                           {"Okay, okay, love you too. Bye.", 0}, {"Did you get my text? ...No? Check again.", 0},
                           {"The meeting got moved to three.", LB_DOWNTOWN},
                           {"Bring sunscreen, it's hot out here.", LB_BEACH}, {"Hold on, you're breaking up...", 0},
                           {"Ay, no me digas!", LB_LUNA}, {"[angry:0.4]I'm not doing this over the phone.", 0}};

const Line kBouncer[] = {{"[calm:0.4]Line starts back there.", 0}, {"[calm:0.4]Not tonight, pal.", 0}, {"[calm:0.4]Private party.", 0}, {"[calm:0.4]Wait your turn.", 0},
                         {"[calm:0.4]No sneakers after ten.", 0}, {"[calm:0.4]Esperate en la fila.", LB_LUNA}};
const Line kRoadRage[] = {{"[shout]Get out of the car!", 0}, {"[shout]You think you can just hit me?", 0}, {"[shout]I got your plate, buddy!", 0},
                          {"[shout]Come here! Look at this!", 0}, {"[shout]You're paying for that!", LB_BOLD}, {"[shout]Estas loco o que?", LB_LUNA}};

const Line kCopMegaphone[] = {{"[megaphone][shout]Pull over! Pull over now!", 0}, {"[megaphone][shout]Stop the vehicle!", 0},
                              {"[megaphone][shout]This is the police, pull over to the side of the road!", 0},
                              {"[megaphone][shout]Driver, stop your vehicle immediately!", 0}, {"[megaphone][shout]Pull over and turn off the engine!", 0},
                              {"[megaphone][shout]Stop the car! Hands where we can see them!", 0}};

const Line kNiceCar[] = {{"[happy:0.5]Nice ride!", 0}, {"[happy:0.5]Whoa, check out that car!", 0}, {"[happy:0.4]Dang, that thing is clean.", 0},
                         {"[laugh]Must be nice.", 0}, {"[happy:0.5]Hey, how fast does it go?", LB_BOLD}, {"[happy:0.5]Que carro, mira eso!", LB_LUNA},
                         {"[happy:0.4]Somebody's doing well.", LB_DOWNTOWN}, {"[happy:0.5]Beautiful machine.", LB_BEACH}};

const Line kTicket[] = {{"[calm:0.5]License and registration, please.", 0}, {"[calm:0.5]Do you know why I pulled you over?", 0},
                        {"[calm:0.5]You were doing fifty in a thirty.", 0}, {"[calm:0.5]Your tail light's out.", 0}, {"[calm:0.5]Wait here, I'll be right back.", 0},
                        {"[calm:0.5]I'm letting you off with a warning. Drive safe.", 0}, {"[calm:0.5]Hands on the wheel where I can see them.", 0}};
const Line kTicketed[] = {{"[scared:0.3]Is there a problem, officer?", 0}, {"[angry:0.4]I was barely speeding!", LB_BOLD}, {"[scared:0.3]Sorry, officer, I'm late for work.", 0},
                          {"[angry:0.4]Come on, man, really?", 0}, {"[scared:0.3]Ay, no me diga.", LB_LUNA}, {"[scared:0.3]It's my cousin's car, I swear.", 0}};

// two acquaintances running into each other on the sidewalk: the hello, a few words, the goodbye
const Line kReunion[] = {{"[happy:0.7]Hey! Look who it is!", 0}, {"[happy:0.7]No way! How long has it been?", 0}, {"[happy:0.6]There you are! I was just thinking about you.", 0},
                         {"[happy:0.6]Hey, stranger!", 0}, {"[happy:0.6]Well, look at you!", 0}, {"[happy:0.7]Oye! Mira quien es!", LB_LUNA},
                         {"[happy:0.6]Que milagro, tanto tiempo!", LB_LUNA}, {"[happy:0.6]Hey! Back in town for the summer?", LB_BEACH},
                         {"[happy:0.5]Hey! Still with the same firm?", LB_DOWNTOWN}};
const Line kSmallTalk[] = {{"How's the family doing?", 0}, {"We have to get lunch one of these days.", 0}, {"Did you ever get that car fixed?", 0},
                           {"Work's been crazy, you know how it is.", 0}, {"You still living over by the park?", 0}, {"Tell your mom I said hi.", 0},
                           {"I saw your sister last week, she looks great.", 0}, {"Did you catch the game?", 0}, {"[happy:0.4]You look great, seriously.", 0},
                           {"Are you still working nights?", 0}, {"Can you believe this heat?", 0}, {"My rent went up again, can you believe it?", 0},
                           {"Y tu abuela, como sigue?", LB_LUNA}, {"Vamos a la fiesta del sabado?", LB_LUNA},
                           {"Have you tried the new place on the promenade?", LB_BEACH}, {"The water's been perfect this week.", LB_BEACH},
                           {"Still up on the twentieth floor?", LB_DOWNTOWN}, {"Traffic on the causeway this morning, unreal.", LB_DOWNTOWN}};
const Line kParting[] = {{"[happy:0.5]Good seeing you!", 0}, {"[happy:0.5]Take care, okay?", 0}, {"[happy:0.5]Call me this week!", 0},
                         {"[happy:0.4]Say hi to everyone for me.", 0}, {"[happy:0.4]Don't be a stranger!", 0}, {"[happy:0.4]Let's do that lunch, I mean it.", 0},
                         {"[happy:0.5]Nos vemos!", LB_LUNA}, {"[happy:0.4]Cuidate, eh?", LB_LUNA}};

// at the airport curb: the one who came to pick someone up, the traveler they came for; the one dropping someone off,
// the traveler going in
const Line kArrival[] = {{"[happy:0.7]There you are!", 0}, {"[happy:0.6]Welcome back!", 0}, {"[happy:0.5]How was the flight?", 0},
                         {"[happy:0.6]You made it!", 0}, {"[happy:0.5]Look at you, all rested.", 0}, {"[happy:0.7]Bienvenida a casa!", LB_FEMALE},
                         {"[happy:0.7]Bienvenido, hermano!", LB_MALE}};
const Line kArrived[] = {{"[happy:0.6]So good to see you!", 0}, {"[happy:0.5]Thanks for picking me up.", 0}, {"[happy:0.4]I'm starving, let's go.", 0},
                         {"[calm:0.5]That flight was forever.", 0}, {"[happy:0.6]Que alegria verte!", 0}, {"[happy:0.5]You didn't have to come, you know.", 0}};
const Line kSendoff[] = {{"[happy:0.5]Have a safe flight!", 0}, {"[calm:0.5]Text me when you land.", 0}, {"[calm:0.4]Got your passport? Your charger?", 0},
                         {"[happy:0.5]Buen viaje, eh?", 0}, {"[happy:0.4]Say hi to everyone up there.", 0}, {"[sad:0.4]Don't stay away so long this time.", 0}};
const Line kLeaving[] = {{"[calm:0.5]I'll call you when I land.", 0}, {"[sad:0.5]I'm going to miss you.", 0}, {"[happy:0.4]Thanks for the ride.", 0},
                         {"[calm:0.5]Te llamo cuando llegue.", 0}, {"[happy:0.4]See you in two weeks.", 0}, {"[calm:0.4]Water the plants, okay?", 0}};

// someone down hurt on the sidewalk, and a passer-by who stops to help (kneeling beside them, or on the phone for them)
const Line kHurt[] = {{"[scared:0.6]Somebody... call an ambulance...", 0}, {"[sad:0.6]Ahh... my leg... my leg...", 0}, {"[scared:0.6]Help me... please...", 0},
                      {"[sad:0.5]I can't get up...", 0}, {"[sad:0.5]Oh god, it hurts...", 0}, {"[scared:0.6]Ayuda... por favor...", LB_LUNA}};
const Line kSamaritan[] = {{"[calm:0.5]Are you okay? Don't try to move.", 0}, {"[calm:0.5]Stay with me, help is coming.", 0}, {"[shout:0.4]Somebody call an ambulance!", 0},
                           {"[calm:0.4]Yes, someone's hurt, they're on the ground. Please hurry.", 0}, {"[calm:0.5]Hang in there, they're on their way.", 0},
                           {"[calm:0.5]Quedate quieto, ya viene la ambulancia.", LB_LUNA}};

const Line kOnlooker[] = {{"[excited:0.5]Whoa, they got somebody.", 0}, {"[calm:0.4]What did they do?", 0}, {"[happy:0.5]Somebody's going to jail tonight.", 0},
                          {"[happy:0.6]You seeing this? Get it on camera.", LB_BOLD}, {"[sad:0.4]Every week on this street, I swear.", 0},
                          {"[calm:0.4]Don't stop, don't stare... okay, maybe a little.", 0}, {"[excited:0.4]Ay, se lo llevan.", LB_LUNA},
                          {"[angry:0.5]Hey! He wasn't doing nothing!", LB_BOLD}, {"[angry:0.5]Easy with him, man, I'm filming this!", LB_BOLD},
                          {"[scared:0.4]Let's just go. Let's not get involved.", LB_TIMID}};
const Line kCopRadio[] = {{"[radio][dispatch]Dispatch, one in custody, we're clear here.", 0}, {"[radio][dispatch]Copy. Show us back in service.", 0},
                          {"[radio][dispatch]Ten-four, heading back to the station with one.", 0}, {"[radio][dispatch]Dispatch, need a report number for that.", 0},
                          {"[radio][dispatch]All good here, just finishing up.", 0}};

const Line kSuspect[] = {{"[angry:0.5]I didn't do anything!", 0}, {"[angry:0.5]This is harassment, man.", 0}, {"[angry:0.4]Easy! Watch the arm!", 0},
                         {"[calm:0.4]I want a lawyer.", 0}, {"[angry:0.5]You got the wrong guy!", 0}, {"[sad:0.4]Ow, they're too tight...", 0},
                         {"[sad:0.5]Somebody call my mom.", 0}, {"[angry:0.5]Yo, I know my rights!", LB_BOLD}, {"[angry:0.5]Yo no hice nada!", LB_LUNA}};
const Line kCopEscort[] = {{"[calm:0.4]Keep walking.", 0}, {"[calm:0.4]You have the right to remain silent.", 0},
                           {"[calm:0.4]Anything you say can and will be used against you.", 0}, {"[calm:0.4]Watch your head.", 0},
                           {"[angry:0.3]Don't make this harder than it is.", 0}, {"[calm:0.4]Dispatch, one in custody, coming in.", 0}};
const Line kCopTransport[] = {{"[calm:0.4]Dispatch, I need a unit for a transport, one in custody.", 0}, {"[calm:0.4]Requesting transport at my location.", 0},
                              {"[calm:0.4]Copy. Holding one here.", 0}, {"[calm:0.4]Sit tight. Your ride's on the way.", 0}};

const Line kBrawl[] = {{"[angry:0.7]You got a problem with me?", 0}, {"[angry:0.7]Say that again. Say it again!", 0}, {"[angry:0.6]Back up out my face!", 0},
                       {"[angry:0.7]You want to go? Let's go!", 0}, {"[angry:0.6]You spilled my drink, man!", 0}, {"[angry:0.6]Who you think you're talking to?", 0},
                       {"[angry:0.6]Keep walking, I'm not playing!", 0}, {"[angry:0.7]Que te pasa? Eh? Que te pasa?", LB_LUNA}};
const Line kBrawlFriend[] = {{"[calm:0.5]Let it go, man. He's not worth it.", 0}, {"[calm:0.5]Come on, walk away. Walk away.", 0}, {"[scared:0.4]Not here, man, the cops...", 0},
                             {"[calm:0.5]Hey, hey, hey, chill. Both of you.", 0}, {"[calm:0.4]Dejalo, hermano, no vale la pena.", LB_LUNA}};

const Line kCopStatement[] = {{"[calm:0.4]Excuse me. Did you see what happened here?", 0}, {"[calm:0.4]Can you tell me what you saw?", 0},
                              {"[calm:0.4]Did he say anything before it started?", 0}, {"[calm:0.4]Which way did he come from?", 0},
                              {"[calm:0.4]Okay. Was anybody else with him?", 0}, {"[calm:0.4]Can I get a name and a number, in case we need you?", 0},
                              {"[calm:0.4]Take your time. From the start.", 0}, {"[calm:0.4]Usted vio lo que paso aqui?", LB_LUNA}};
const Line kCopStatementEnd[] = {{"[calm:0.4]Okay. Thanks for your help.", 0}, {"[calm:0.4]That's all I need. Thank you.", 0},
                                 {"[calm:0.4]We'll be in touch if we need anything else.", 0}, {"[happy:0.3]Appreciate it. Have a good night.", 0},
                                 {"[calm:0.4]Gracias. Eso es todo.", LB_LUNA}};
const Line kCopStop[] = {{"[calm:0.4]Excuse me. A moment, please.", 0}, {"[calm:0.4]Hold up a second for me.", 0},
                         {"[calm:0.4]Hey, hold on. A quick word.", 0}, {"[calm:0.4]Oiga, un momento.", LB_LUNA}};
const Line kCopStopAsk[] = {{"[calm:0.4]Can I see some ID?", 0}, {"[calm:0.4]Where are you headed today? ID, please.", 0},
                            {"[calm:0.4]Do you live around here? Let me see some ID.", 0}, {"[calm:0.4]Anything on you I should know about?", 0},
                            {"[calm:0.4]Identificacion, por favor.", LB_LUNA}};
const Line kCopStopOk[] = {{"[calm:0.4]Alright, you're good. Have a nice day.", 0}, {"[calm:0.4]Okay. Thanks for your time.", 0},
                           {"[happy:0.3]You're all set. Go ahead.", 0}, {"[calm:0.4]Sorry for the trouble. Take care.", 0}};
const Line kStopped[] = {{"[calm:0.4]What's this about, officer?", 0}, {"[calm:0.4]I'm just walking home.", 0},
                         {"[angry:0.4]Am I being detained?", LB_BOLD}, {"[calm:0.4]Okay, okay. Here's my ID.", 0},
                         {"[scared:0.4]Did I do something wrong?", LB_TIMID}, {"[angry:0.4]Seriously? Again?", LB_BOLD},
                         {"[calm:0.4]Que pasa, oficial?", LB_LUNA}};
const Line kStoppedEnd[] = {{"[angry:0.4]Unbelievable. Every time.", LB_BOLD}, {"[happy:0.3]Thanks, officer.", 0}, {"[calm:0.4]Can I go now? Great.", 0},
                            {"[sad:0.4]Whatever, man.", 0}, {"[calm:0.4]Have a good one.", 0}, {"[angry:0.3]Siempre lo mismo.", LB_LUNA}};
// a beat officer writing up a parked car (the ticket under the wiper), the owner who comes hurrying, the answer, the grumble
const Line kCopParking[] = {{"[calm:0.4]There we go.", 0}, {"[calm:0.4]Meter ran out an hour ago.", 0}, {"[calm:0.4]That's a tow zone, friend.", 0},
                            {"[calm:0.4]Third time this week, same car.", 0}, {"[calm:0.4]Right in front of a hydrant. Nope.", 0},
                            {"[calm:0.4]Should've fed the meter.", 0}, {"[calm:0.4]Zona de carga. Multa.", LB_LUNA}};
const Line kCopParkingReply[] = {{"[calm:0.4]Take it up with the city, sir.", 0}, {"[calm:0.4]It's already written. Have a nice day.", 0},
                                 {"[calm:0.4]There's a number on the back if you want to contest it.", 0}, {"[calm:0.4]Rules are rules.", 0},
                                 {"[calm:0.4]Next time read the sign.", 0}, {"[calm:0.4]Lo siento, ya esta escrita.", LB_LUNA}};
const Line kParkingOwner[] = {{"[excited:0.6]Hey! Hey, wait - that's my car!", 0}, {"[excited:0.6]No no no no, I'm right here!", 0},
                              {"[angry:0.5]Officer! Officer, I'm moving it!", 0}, {"[excited:0.6]Wait! I'm leaving, I'm leaving!", 0},
                              {"[angry:0.5]Oye, oye, ese es mi carro!", LB_LUNA}};
const Line kParkingProtest[] = {{"[angry:0.5]I was gone two minutes! Two!", 0}, {"[sad:0.4]Come on, I was just grabbing a coffee.", 0},
                                {"[angry:0.5]The meter's broken, I swear.", 0}, {"[sad:0.4]Can't you just tear it up? Please?", 0},
                                {"[angry:0.5]Are you kidding me? I was right there!", LB_BOLD}, {"[sad:0.4]Ay, por favor, fue un minuto nada mas.", LB_LUNA}};
const Line kParkingGrumble[] = {{"[angry:0.4]Unbelievable. Sixty bucks.", 0}, {"[sad:0.3]Great. Just great.", 0},
                                {"[angry:0.4]This city, man.", 0}, {"[sad:0.3]There goes lunch money.", 0}, {"[angry:0.4]Que suerte la mia.", LB_LUNA}};
// a proposal on the sidewalk: the lead-in, the question on one knee, the surprise, the answer, the one left kneeling,
// the people who stopped to watch
const Line kProposeAsk[] = {{"[calm:0.4]Hey... wait. Can I ask you something?", 0}, {"[calm:0.4]There's something I've wanted to say for a while.", 0},
                            {"[happy:0.4]Stay right there. Don't move.", 0}, {"[calm:0.4]You know this is where we met, right?", 0},
                            {"[calm:0.4]Oye, espera. Tengo algo que decirte.", LB_LUNA}};
const Line kPropose[] = {{"[happy:0.6]Will you marry me?", 0}, {"[happy:0.6]I love you. Will you marry me?", 0}, {"[happy:0.5]So... marry me?", 0},
                         {"[happy:0.6]Spend the rest of your life with me?", 0}, {"[happy:0.6]Te quieres casar conmigo?", LB_LUNA}};
const Line kProposed[] = {{"[excited:0.7]Oh my god... what are you doing?", 0}, {"[excited:0.7]Are you serious right now?", 0},
                          {"[excited:0.7]No way. No way!", 0}, {"[excited:0.7]Dios mio... en serio?", LB_LUNA}};
const Line kProposeYes[] = {{"[happy:0.9]Yes! Yes, of course!", 0}, {"[happy:0.9]YES! Oh my god, yes!", 0}, {"[happy:0.8]Yes! A thousand times yes!", 0},
                            {"[happy:0.9]Si! Claro que si!", LB_LUNA}};
const Line kProposeNo[] = {{"[sad:0.6]I... I'm sorry. I can't.", 0}, {"[sad:0.6]Get up. Please, just get up.", 0},
                           {"[sad:0.5]We need to talk. Not here.", 0}, {"[sad:0.6]Lo siento... no puedo.", LB_LUNA}};
const Line kProposeSad[] = {{"[sad:0.6]...okay. Okay.", 0}, {"[sad:0.6]I'll just... get up, then.", 0}, {"[sad:0.5]Well. That went great.", 0}};
const Line kCrowdAww[] = {{"[happy:0.6]Aww!", 0}, {"[happy:0.7]Congratulations!", 0}, {"[happy:0.7]Woo! Let's go!", LB_BOLD}, {"[happy:0.5]That's so sweet.", 0},
                          {"[happy:0.7]Felicidades!", LB_LUNA}, {"[happy:0.6]I'm not crying, you're crying.", 0}};
const Line kCrowdOoh[] = {{"[sad:0.5]Ohhh...", 0}, {"[sad:0.5]Oof. That hurts.", 0}, {"[sad:0.4]Yikes.", 0}, {"[sad:0.5]Brutal, man.", LB_BOLD},
                          {"[sad:0.5]Ay, pobrecito.", LB_LUNA}};
// two out walking together, overheard as the player passes: a line, and the answer
const Line kStrollChat[] = {{"So where do you want to eat?", 0}, {"I told you that place closes early.", 0}, {"Did you ever call your brother back?", 0},
                            {"I need a vacation from my vacation.", 0}, {"Remind me to grab milk on the way back.", 0},
                            {"He still hasn't paid me back, by the way.", 0}, {"We should've taken the car.", 0},
                            {"I swear this street gets longer every time.", 0}, {"Okay but hear me out - tacos.", 0},
                            {"Did you see what she posted last night?", 0}, {"My boss wants it done by Friday. This Friday.", 0},
                            {"I'm thinking of getting a dog.", 0}, {"Wait, which way is the car?", 0},
                            {"Vamos por un cafecito primero?", LB_LUNA}, {"Mi tia hizo pasteles para el domingo.", LB_LUNA},
                            {"Sunscreen. I forgot the sunscreen.", LB_BEACH}, {"Let's get a spot before the sunset crowd.", LB_BEACH},
                            {"I have a call at four, so we have to be quick.", LB_DOWNTOWN}, {"They're renting the whole fortieth floor now.", LB_DOWNTOWN}};
const Line kCopBeatChat[] = {{"[calm:0.4]My feet are killing me.", 0}, {"[calm:0.4]Two more blocks, then lunch.", 0},
                             {"[calm:0.4]Sarge wants the reports by four.", 0}, {"[calm:0.4]You hear about the thing on the causeway?", 0},
                             {"[calm:0.4]Quiet today. Too quiet.", 0}, {"[calm:0.4]That guy again. Every single day.", 0},
                             {"[calm:0.4]I'm putting in for bike patrol. Final answer.", 0}, {"[calm:0.4]Remember when this was all parking lots?", LB_DOWNTOWN},
                             {"[calm:0.4]Tourist season. God help us.", LB_BEACH}, {"[calm:0.4]Dona Marta's making flan again, I can smell it.", LB_LUNA}};
const Line kStrollReply[] = {{"Ha. Right?", 0}, {"No way.", 0}, {"I know, I know.", 0}, {"Mm-hm.", 0}, {"You always say that.", 0},
                             {"Seriously?", 0}, {"Okay, fine. Fine.", 0}, {"Don't start.", 0}, {"[happy:0.4]Ha! Stop it.", 0},
                             {"That's what I said!", 0}, {"Claro, claro.", LB_LUNA}, {"Ay, no.", LB_LUNA}};
// the rain coming down on someone without an umbrella
const Line kRain[] = {{"[angry:0.4]Oh, come on!", 0}, {"[sad:0.4]Great. Just did my hair.", 0}, {"[excited:0.5]Here it comes!", 0},
                      {"[angry:0.4]It was fine five minutes ago!", 0}, {"[happy:0.4]Run for it!", 0}, {"[angry:0.4]Ay, el aguacero!", LB_LUNA},
                      {"[sad:0.4]My shoes, man. My shoes.", 0}};
// a long wait at a red light to cross
const Line kCrossWait[] = {{"[calm:0.3]This light takes forever.", 0}, {"[angry:0.3]Come on, come on...", 0}, {"[calm:0.3]Every single time.", 0},
                           {"[calm:0.3]You'd think they'd time these better.", 0}, {"[angry:0.3]Dale, cambia ya.", LB_LUNA},
                           {"[calm:0.3]I'm going to be late. Again.", LB_DOWNTOWN}};
// a look at the map on the phone, and the wrong way
const Line kLost[] = {{"[calm:0.3]Wait... wrong way.", 0}, {"[calm:0.3]Hm. This isn't right.", 0}, {"[angry:0.3]Ugh, the map flipped again.", 0},
                      {"[calm:0.3]Where is this place?", 0}, {"[calm:0.3]Espera... es por alla.", LB_LUNA}};
// a driver braking for somebody out in the road off the crossing
const Line kHonkPed[] = {{"[shout]Hey! Use the crosswalk!", 0}, {"[angry:0.6]Watch where you're going!", 0}, {"[angry:0.6]You trying to get killed?", 0},
                         {"[angry:0.5]Eyes up, buddy!", 0}, {"[shout]Oye! Cuidado!", LB_LUNA}, {"[angry:0.6]It's a road, not a sidewalk!", LB_BOLD}};
// officers at a fender bender: a word with the two drivers, and the one at the back waving the traffic round
const Line kCopCrash[] = {{"[calm:0.4]Anybody hurt? No? Good.", 0}, {"[calm:0.4]License and insurance, both of you.", 0},
                          {"[calm:0.4]Okay. One at a time - what happened?", 0}, {"[calm:0.4]Let's get these cars out of the lane.", 0},
                          {"[calm:0.4]I've got what I need. Swap your details and move along.", 0}, {"[calm:0.4]Calma, calma. Uno a la vez.", LB_LUNA}};
const Line kCopWave[] = {{"[shout]Keep it moving!", 0}, {"[shout]Come on through - slowly!", 0}, {"[shout]Nothing to see, keep going!", 0},
                         {"[shout]Other lane, other lane!", 0}, {"[shout]Sigan, sigan!", LB_LUNA}};
// the player walking past hurt
const Line kAskOkay[] = {{"[scared:0.5]Hey - are you okay? You're bleeding.", 0}, {"[scared:0.5]Whoa. You need a doctor, man.", 0},
                         {"[scared:0.4]Should I call somebody?", LB_TIMID}, {"[calm:0.4]You look like you got hit by a truck.", LB_BOLD},
                         {"[scared:0.5]Oye, estas bien? Estas sangrando.", LB_LUNA}};
// a beat officer with somebody down hurt: the radio for an ambulance, a word to them
const Line kCopEms[] = {{"[radio][dispatch]Dispatch, I need EMS at my location, one down.", 0}, {"[calm:0.5]Stay with me. The ambulance is on its way.", 0},
                        {"[calm:0.5]Don't try to get up. Just breathe.", 0}, {"[radio][dispatch]Ten-four, EMS is rolling. Five minutes out.", 0},
                        {"[calm:0.5]Tranquilo. Ya viene la ayuda.", LB_LUNA}};
// the player leaning on the horn by people on foot
const Line kHorned[] = {{"[angry:0.6]Hey! I'm walking here!", 0}, {"[angry:0.5]Yeah, yeah, I hear you!", 0}, {"[angry:0.5]Relax, buddy!", 0},
                        {"[angry:0.6]Honk again. I dare you.", LB_BOLD}, {"[scared:0.4]Jeez! Okay, okay.", LB_TIMID}, {"[angry:0.5]Oye, que te pasa?", LB_LUNA}};
// a driver the player cut up at speed, through the window
const Line kCutOff[] = {{"[shout]Hey! Learn to drive!", 0}, {"[shout]Use your signal!", 0}, {"[shout]Whoa - watch it!", 0}, {"[shout]Are you blind?!", LB_BOLD},
                        {"[angry:0.7]Nice driving, genius!", 0}, {"[scared:0.6]Jesus Christ!", LB_TIMID}, {"[shout]Fijate, idiota!", LB_LUNA}};
// the player walking close behind somebody a good while: the look back and a word, then the sharper word (timid / bold)
const Line kFollowed[] = {{"[calm:0.5]Uh... can I help you?", 0}, {"[calm:0.5]Are you following me?", 0}, {"[angry:0.4]Do you mind?", 0},
                          {"[calm:0.5]Hey - what's your deal?", LB_BOLD}, {"[scared:0.4]Um. Hi?", LB_TIMID}, {"[calm:0.5]Me estas siguiendo?", LB_LUNA}};
const Line kFollowedScared[] = {{"[scared:0.6]Stop following me!", 0}, {"[scared:0.6]Leave me alone!", 0}, {"[scared:0.6]I'm calling the police!", 0},
                                {"[scared:0.5]Okay, this is creepy.", 0}, {"[scared:0.6]Dejame en paz!", LB_LUNA}};
const Line kFollowedBold[] = {{"[angry:0.6]You got a problem, pal?", 0}, {"[angry:0.6]Keep walking, man.", 0}, {"[angry:0.7]Back off. Now.", 0},
                              {"[angry:0.6]Que quieres, eh?", LB_LUNA}};
// a car alarm going off nearby: a word about it; the player seen breaking into the car
const Line kAlarmGrumble[] = {{"[angry:0.5]Somebody turn that thing off!", 0}, {"[calm:0.4]Oh, come on.", 0}, {"[angry:0.4]Every single night...", 0},
                              {"[angry:0.5]Whose car is that?!", 0}, {"[calm:0.4]Great. Just great.", 0}, {"[angry:0.6]Shut it off already!", LB_BOLD},
                              {"[sad:0.4]My head...", LB_TIMID}, {"[angry:0.5]Apaguen eso ya!", LB_LUNA}};
const Line kAlarmWitness[] = {{"[shout:0.7]Hey! That's not your car!", 0}, {"[shout:0.7]Hey! What do you think you're doing?!", 0},
                              {"[scared:0.6]Oh my God - they're stealing it!", LB_TIMID}, {"[shout:0.7]Somebody stop them!", 0},
                              {"[shout:0.7]Oye! Ese carro no es tuyo!", LB_LUNA}};
// the owner of a car whose alarm went off: hurrying over, the word after a look along its side, and to the player stood by
const Line kAlarmOwner[] = {{"[shout:0.6]Hey! Hey - that's my car!", 0}, {"[calm:0.5]Okay, okay - coming!", 0}, {"[angry:0.5]Not again...", 0},
                            {"[shout:0.6]Get away from it!", LB_BOLD}, {"[scared:0.5]Oh no, no, no...", LB_TIMID}, {"[shout:0.6]Ese es mi carro!", LB_LUNA}};
const Line kAlarmChecked[] = {{"[calm:0.5]Not a scratch. Lucky.", 0}, {"[angry:0.5]Unbelievable.", 0}, {"[calm:0.4]Stupid alarm.", 0},
                              {"[angry:0.6]Who did this?!", LB_BOLD}, {"[calm:0.5]Goes off if you breathe on it.", 0}, {"[calm:0.4]Bueno... nada.", LB_LUNA}};
const Line kAlarmAccuse[] = {{"[angry:0.6]Was that you?!", 0}, {"[angry:0.6]Did you touch my car?", 0}, {"[angry:0.7]You got a problem with my car?", LB_BOLD},
                             {"[calm:0.5]Uh... did you see who did that?", LB_TIMID}, {"[angry:0.6]Tocaste mi carro?", LB_LUNA}};
// ... the car being taken: after it, and the word as it goes
const Line kAlarmTheft[] = {{"[shout:0.8]Hey! That's my car!", 0}, {"[shout:0.8]Get out of my car!", 0}, {"[shout:0.8]Stop! Thief!", 0},
                            {"[shout:0.8]Hey! HEY!", LB_BOLD}, {"[scared:0.6]No, no, no - not my car!", LB_TIMID}, {"[shout:0.8]Sal de mi carro!", LB_LUNA}};
// an officer busy at a stop, a statement, a ticket or a crash scene, the player crowding them
const Line kCopStepBack[] = {{"[calm:0.5]Step back, please.", 0}, {"[calm:0.5]Give us some room here.", 0},
                             {"[calm:0.5]Nothing to see here. Move along.", 0}, {"[calm:0.6]Can I help you with something?", 0},
                             {"[angry:0.4]Back up. Now, please.", LB_BOLD}, {"[calm:0.5]Un poco de espacio, por favor.", LB_LUNA}};
// two waiting at a bus stop a while: a word about the bus, and the answer
const Line kBusWait[] = {{"[calm:0.5]This bus is never on time.", 0}, {"[calm:0.5]Twenty minutes I've been standing here.", 0},
                         {"[calm:0.5]You think it's still coming?", 0}, {"[calm:0.5]They changed the schedule again, didn't they?", 0},
                         {"[angry:0.4]Every single day with this bus.", LB_BOLD}, {"[calm:0.4]Um... has the 12 been by yet?", LB_TIMID},
                         {"[calm:0.5]Hot one today, huh?", LB_BEACH}, {"[calm:0.5]Esta guagua nunca llega...", LB_LUNA}};
const Line kBusWaitReply[] = {{"[calm:0.5]Tell me about it.", 0}, {"[calm:0.5]It'll come. Eventually.", 0}, {"[calm:0.5]Every single day.", 0},
                              {"[calm:0.5]I should've walked.", 0}, {"[angry:0.4]Don't get me started.", LB_BOLD},
                              {"[calm:0.4]I... don't think so.", LB_TIMID}, {"[calm:0.5]Ni me digas.", LB_LUNA}};
// the player knocked flat close by (a car, a fall, a blast): a gasp
const Line kPlayerDown[] = {{"[scared:0.7]Oh my God!", 0}, {"[scared:0.7]Whoa! Are you alright?!", 0}, {"[shout:0.7]Somebody call an ambulance!", 0},
                            {"[scared:0.6]Don't move, don't move!", 0}, {"[scared:0.8]Oh no, no, no!", LB_TIMID},
                            {"[shout:0.6]Did you see that?!", LB_BOLD}, {"[scared:0.7]Dios mio!", LB_LUNA}};
// a driver who knocked the player flat, out to see
const Line kDriverSorry[] = {{"[scared:0.7]Oh my God, I'm so sorry!", 0}, {"[scared:0.7]I didn't see you! Are you okay?!", 0},
                             {"[scared:0.6]Should I call an ambulance?", 0}, {"[angry:0.4]You stepped right out - I couldn't stop!", LB_BOLD},
                             {"[scared:0.8]Please be okay, please be okay...", LB_TIMID}, {"[scared:0.7]Ay, perdon! Esta bien?", LB_LUNA}};
// a witness to an officer on foot who has lost the wanted player: the way he went, or he is right there; the officer's word
const Line kTipOff[] = {{"[shout:0.6]Officer! He went that way!", 0}, {"[shout:0.6]Hey! The guy you're after - he ran down there!", 0},
                        {"[shout:0.6]Over here! He went that way, just now!", 0}, {"[calm:0.5]Looking for him? That way. He was in a hurry.", LB_BOLD},
                        {"[shout:0.6]That way! He went round the corner!", 0}, {"[scared:0.5]Um, officer? He went... that way.", LB_TIMID},
                        {"[shout:0.6]Oficial! Se fue por alla!", LB_LUNA}};
const Line kTipHere[] = {{"[shout:0.7]He's right there! Over there!", 0}, {"[shout:0.7]Officer! That's him, right there!", 0},
                         {"[shout:0.7]There! He's hiding over there!", 0}, {"[shout:0.7]That's the guy! Right there!", LB_BOLD},
                         {"[shout:0.7]Ahi esta! Es el!", LB_LUNA}};
const Line kCopTip[] = {{"[calm:0.6]Copy. All units, suspect seen heading that way.", 0}, {"[calm:0.6]Thanks. Stay back now.", 0},
                        {"[calm:0.6]Dispatch, a witness puts him moving that way.", 0}, {"[calm:0.6]Got it. Get somewhere safe.", 0},
                        {"[calm:0.6]Copy that - moving.", 0}, {"[calm:0.6]Gracias. Quedese aqui.", LB_LUNA}};
// a stranger who asked the player the way, the player off without a word
const Line kAskSnubbed[] = {{"[calm:0.4]Oh - okay. Thanks anyway.", 0}, {"[calm:0.4]Never mind, I'll figure it out.", 0},
                            {"[angry:0.4]Wow. Okay then.", LB_BOLD}, {"[calm:0.3]Sorry to bother you...", LB_TIMID},
                            {"[calm:0.4]Um... okay. Have a nice day?", 0}, {"[calm:0.4]Bueno... gracias igual.", LB_LUNA}};
// somebody knocked down by a car, up again: at the driver come to see; the driver with the one hit still down: the phone
const Line kHitAngry[] = {{"[angry:0.7]Are you blind?! I was right there!", 0}, {"[angry:0.7]You could've killed me!", 0},
                          {"[angry:0.6]Watch where you're driving!", 0}, {"[sad:0.5]Ow... I'm okay. I think I'm okay.", LB_TIMID},
                          {"[angry:0.8]Look at me! You hit me!", LB_BOLD}, {"[angry:0.7]Casi me matas!", LB_LUNA}};
const Line kDriverCall[] = {{"[scared:0.8]Hello? I need an ambulance - I hit somebody with my car!", 0},
                            {"[scared:0.8]Yes, there's someone down in the road, please hurry!", 0},
                            {"[scared:0.8]They're not moving - please send someone!", 0},
                            {"[scared:0.8]Necesito una ambulancia, rapido, por favor!", LB_LUNA}};
// somebody who saw a crash going past (not their car): a word
const Line kCrashSeen[] = {{"[excited:0.6]Whoa! Did you see that?", 0}, {"[scared:0.5]Oh! Oh, that's bad.", 0}, {"[calm:0.5]That's gonna leave a mark.", 0},
                           {"[laugh]Nice driving, genius!", LB_BOLD}, {"[scared:0.5]Is everybody okay?", LB_TIMID},
                           {"[calm:0.5]Somebody's calling their insurance tonight.", 0}, {"[shout:0.5]Learn to drive!", LB_BOLD},
                           {"[excited:0.5]Ay, que choque!", LB_LUNA}, {"[calm:0.5]Oof.", 0}};
// an officer searching on foot asking somebody about the suspect; the answer when they saw nobody
const Line kCopAskSeen[] = {{"[calm:0.6]Excuse me - did you see a man run through here?", 0}, {"[calm:0.6]Police. You see anybody come by in a hurry?", 0},
                            {"[calm:0.6]Hey - you see a guy go past here just now?", 0}, {"[calm:0.6]Quick question - anybody run by you?", 0},
                            {"[calm:0.6]Disculpe, vio pasar a un hombre corriendo?", LB_LUNA}};
const Line kNotSeen[] = {{"[calm:0.4]No, sorry.", 0}, {"[calm:0.4]Didn't see anybody.", 0}, {"[calm:0.4]Sorry, I just got here.", 0},
                         {"[calm:0.4]I mind my own business, officer.", LB_BOLD}, {"[scared:0.3]N-no... sorry.", LB_TIMID},
                         {"[calm:0.4]No, lo siento.", LB_LUNA}};
// the player's car show (donuts, a burnout, a slide going round in one spot): the crowd egging it on, the steadier sort
// walking past, the last word once it is over; an officer on foot calling out; tyres screeching close by
const Line kShowCheer[] = {{"[excited:0.7]Yeah! Send it!", 0}, {"[excited:0.7]Again! Again!", 0}, {"[excited:0.6]Light 'em up!", LB_BOLD},
                           {"[laugh]Ohhh, he's crazy!", 0}, {"[excited:0.6]Are you getting this?", 0}, {"[excited:0.6]Look at the smoke!", 0},
                           {"[excited:0.7]Eso! Dale, dale!", LB_LUNA}, {"[excited:0.6]That's going on my story.", LB_BEACH},
                           {"[excited:0.6]Oh, he's cooking the tires!", LB_BOLD}, {"[laugh]Woo! Okay, okay!", 0}};
const Line kShowTut[] = {{"[angry:0.4]Unbelievable.", 0}, {"[angry:0.4]Grow up.", LB_BOLD}, {"[scared:0.4]Somebody's going to get hurt.", LB_TIMID},
                         {"[angry:0.4]People live here, you know.", 0}, {"[calm:0.4]Idiot. Total idiot.", LB_DOWNTOWN},
                         {"[angry:0.4]Que barbaridad.", LB_LUNA}, {"[scared:0.4]Oh no, no no. I'm going.", LB_TIMID}};
const Line kShowEnd[] = {{"[excited:0.5]That was insane!", 0}, {"[laugh]Bro. Bro!", LB_BOLD}, {"[calm:0.4]Show's over, I guess.", 0},
                         {"[laugh]Okay, that was kind of sick.", 0}, {"[calm:0.4]And that's why my insurance is so high.", LB_DOWNTOWN},
                         {"[excited:0.5]Que locura!", LB_LUNA}, {"[happy:0.5]Got the whole thing.", 0}};
const Line kCopShow[] = {{"[shout]Hey! Knock it off!", 0}, {"[shout]Cut that out - right now!", 0}, {"[shout]Hey! Not on my street!", 0},
                         {"[angry:0.6]You want me to write you up? Stop!", 0}, {"[shout]That's enough! Pull it over!", 0}};
const Line kScreech[] = {{"[scared:0.4]Whoa!", 0}, {"[scared:0.4]Jeez!", 0}, {"[angry:0.4]Easy!", LB_BOLD}, {"[scared:0.4]Oh my god.", LB_TIMID},
                         {"[calm:0.4]Somebody's in a hurry.", LB_DOWNTOWN}, {"[scared:0.4]Ay!", LB_LUNA}, {"[angry:0.4]Slow down!", 0}};
// the player's car stood on the crosswalk they are crossing; up on the sidewalk they are walking
const Line kWalkingHere[] = {{"[angry:0.6]Hey! I'm walking here!", 0}, {"[angry:0.5]Nice parking.", 0}, {"[angry:0.6]Back it up, genius!", LB_BOLD},
                             {"[angry:0.5]Really? On the crosswalk?", 0}, {"[angry:0.5]Oye! El paso de peatones!", LB_LUNA},
                             {"[angry:0.4]Some of us have places to be.", LB_DOWNTOWN}, {"[scared:0.3]Um... excuse me.", LB_TIMID},
                             {"[angry:0.5]Ever heard of a stop line?", 0}, {"[angry:0.5]Move it, pal!", LB_BOLD}};
const Line kSidewalkCar[] = {{"[angry:0.5]Seriously? On the sidewalk?", 0}, {"[angry:0.5]It's a sidewalk, pal.", LB_BOLD},
                             {"[angry:0.4]Where am I supposed to walk?", 0}, {"[angry:0.4]Que falta de respeto.", LB_LUNA},
                             {"[calm:0.4]Somebody call a tow truck.", LB_DOWNTOWN}, {"[scared:0.3]Oh - okay, I'll go around.", LB_TIMID},
                             {"[angry:0.5]You can't park there!", 0}};
// the player running past close on foot
const Line kRunPast[] = {{"[calm:0.4]Where's the fire?", 0}, {"[angry:0.4]Jeez, slow down!", 0}, {"[laugh]Somebody's late.", 0},
                         {"[angry:0.5]Hey! Watch it!", LB_BOLD}, {"[scared:0.3]Whoa - okay.", LB_TIMID}, {"[calm:0.4]Oye, despacio!", LB_LUNA},
                         {"[calm:0.4]Must be some meeting.", LB_DOWNTOWN}, {"[happy:0.4]Go, go, go!", LB_BEACH}};
// the player's car stopped close by with the radio on: moving to it; the steadier sort against it
const Line kGoodSong[] = {{"[excited:0.6]Ayy, turn it up!", 0}, {"[happy:0.5]Oh, that's my song!", 0}, {"[happy:0.5]Okay, I see you!", LB_BOLD},
                          {"[excited:0.6]Eso! Subele!", LB_LUNA}, {"[happy:0.5]Now that's a vibe.", LB_BEACH}, {"[laugh]Don't stop!", 0}};
const Line kLoudMusic[] = {{"[angry:0.4]Turn that down!", 0}, {"[angry:0.4]Some of us are working here.", LB_DOWNTOWN},
                           {"[angry:0.4]Really? This loud?", 0}, {"[angry:0.4]Bajale, por favor.", LB_LUNA},
                           {"[scared:0.3]That's... loud.", LB_TIMID}, {"[angry:0.5]Nobody wants to hear that, pal.", LB_BOLD}};
// two walking together late at night: a line and the answer
const Line kNightChat[] = {{"I'm not even tired. Are you tired?", 0}, {"Where's the after-party, though?", 0}, {"My feet are killing me.", 0},
                           {"Did you see the line at that place?", 0}, {"Text me when you get home, okay?", 0},
                           {"Okay, one more place. One.", 0}, {"[laugh]I can't believe you said that to him.", 0},
                           {"Who has the keys? Tell me you have the keys.", 0}, {"I'm starving. Is anything still open?", 0},
                           {"The DJ was so good tonight.", LB_BEACH}, {"I have work in six hours. Six.", LB_DOWNTOWN},
                           {"Vamos a comer algo? Tengo un hambre...", LB_LUNA}};
const Line kNightReply[] = {{"[laugh]Absolutely not.", 0}, {"You said that an hour ago.", 0}, {"[happy:0.4]Okay, okay. One more.", 0},
                            {"Shh, people are sleeping.", 0}, {"Pizza. Pizza is open.", 0}, {"[laugh]Stop, I can't.", 0},
                            {"Mm. I'm calling a car.", 0}, {"Dale, dale.", LB_LUNA}};
const Line kAlarmGone[] = {{"[angry:0.7]Come back here!", 0}, {"[sad:0.6]Are you kidding me?!", 0}, {"[sad:0.6]I just paid it off!", 0},
                           {"[angry:0.6]Somebody call the cops!", 0}, {"[angry:0.7]You'd better run!", LB_BOLD}, {"[sad:0.6]No me lo puedo creer!", LB_LUNA}};
// the player standing right by somebody sat or stood somewhere for a while: a word, and the word on the way off
const Line kCrowded[] = {{"[calm:0.5]Can I help you?", 0}, {"[calm:0.4]Uh... hi?", LB_TIMID}, {"[calm:0.5]You need something?", 0},
                         {"[angry:0.4]Personal space, man.", LB_BOLD}, {"[calm:0.5]Do I know you?", 0}, {"[calm:0.5]Que pasa?", LB_LUNA}};
const Line kCrowdedLeave[] = {{"[calm:0.4]Okay... I'm going.", 0}, {"[angry:0.4]Weirdo.", LB_BOLD}, {"[calm:0.4]Alright, I'm out of here.", 0},
                              {"[scared:0.4]Creepy.", LB_TIMID}, {"[calm:0.4]Enjoy the spot.", 0}, {"[calm:0.4]Que raro...", LB_LUNA}};
// an officer on the beat calling to somebody crossing mid-block, and the word back
const Line kCopJaywalk[] = {{"[shout:0.6]Hey! Crosswalk's right there!", 0}, {"[shout:0.6]Use the crosswalk, please!", 0},
                            {"[shout:0.6]Hey - you want to get hit? Crosswalk!", 0}, {"[calm:0.5]Next time, the crosswalk, alright?", 0},
                            {"[shout:0.6]Oye! Por el paso de peatones!", LB_LUNA}};
const Line kJaywalkReply[] = {{"[scared:0.4]Sorry, officer!", 0}, {"[calm:0.4]My bad!", 0}, {"[angry:0.3]Yeah, yeah...", LB_BOLD},
                              {"[calm:0.4]Going, going!", 0}, {"[scared:0.4]Perdon, perdon!", LB_LUNA}};
// a car tearing past close to the kerb: after it
const Line kNearMiss[] = {{"[angry:0.7]Slow down!", 0}, {"[angry:0.7]Hey! It's a city street!", 0}, {"[scared:0.6]Jesus!", 0},
                          {"[angry:0.7]Are you out of your mind?!", LB_BOLD}, {"[angry:0.6]Idiot!", LB_BOLD}, {"[scared:0.6]Whoa - whoa!", LB_TIMID},
                          {"[angry:0.6]There are kids around here!", 0}, {"[scared:0.6]Dios mio!", LB_LUNA}, {"[angry:0.6]Tranquilo, loco!", LB_LUNA}};
// a passer-by dropping something in the cup of somebody sat asking for change
const Line kGiveChange[] = {{"[calm:0.5]Here you go.", 0}, {"[calm:0.5]Take care, man.", 0}, {"[calm:0.5]Hang in there.", 0},
                            {"[happy:0.4]Get yourself something to eat.", 0}, {"[calm:0.5]Toma, amigo.", LB_LUNA}};
// running for a bus pulled in at a stop: the call, the thanks to the driver for waiting, the word after it when it goes
const Line kBusRun[] = {{"[shout:0.7]Hold the bus! Hold it!", 0}, {"[shout:0.7]Wait! Wait, wait, wait!", 0}, {"[shout:0.6]Hey! Hang on!", 0},
                        {"[shout:0.7]Wait up, driver!", 0}, {"[shout:0.7]Espere! Espere!", LB_LUNA}};
const Line kBusThanks[] = {{"[happy:0.5]Thanks - thank you!", 0}, {"[happy:0.5]Made it. Thanks, man.", 0}, {"[calm:0.5]Appreciate it!", 0},
                           {"[happy:0.5]Gracias, gracias!", LB_LUNA}};
const Line kBusMissed[] = {{"[angry:0.5]Seriously?!", 0}, {"[angry:0.5]Oh, come on!", 0}, {"[sad:0.5]Every single time.", 0},
                           {"[angry:0.4]He saw me. He totally saw me.", 0}, {"[sad:0.4]Great. Twenty minutes.", 0}, {"[angry:0.5]No puede ser!", LB_LUNA}};
// somebody down on their luck asking the player for change, the thanks, and no hard feelings
const Line kPanhandle[] = {{"[sad:0.4]Hey, friend - spare a couple bucks?", 0}, {"[sad:0.4]Excuse me, anything helps. Anything.", 0},
                           {"[calm:0.4]You got a dollar for a sandwich, man?", 0}, {"[sad:0.4]Sorry to bother you - any change?", 0},
                           {"[sad:0.4]Oye, amigo, unas monedas?", LB_LUNA}};
const Line kPanhandleThanks[] = {{"[happy:0.6]God bless you, friend.", 0}, {"[happy:0.6]Thank you. Seriously, thank you.", 0},
                                 {"[happy:0.5]You're a good one. Take care.", 0}, {"[happy:0.6]Gracias, que Dios te bendiga.", LB_LUNA}};
const Line kPanhandleNo[] = {{"[calm:0.4]Alright. Have a good one.", 0}, {"[calm:0.4]No problem, no problem.", 0},
                             {"[sad:0.4]Yeah. Okay.", 0}, {"[calm:0.4]Next time, maybe.", 0}, {"[calm:0.4]Bueno, otro dia.", LB_LUNA}};
const Line kAskWay[] = {{"[calm:0.4]Excuse me, sorry - is the bus station this way?", 0}, {"[happy:0.4]Hi! Do you know where the pier is?", LB_BEACH},
                        {"[calm:0.4]Sorry to bother you, is there a pharmacy around here?", 0}, {"[calm:0.4]Excuse me, which way to the museum?", LB_DOWNTOWN},
                        {"[calm:0.4]Hi, I think I'm lost. Where's the nearest train stop?", 0}, {"[calm:0.4]Perdone, donde queda la parada del bus?", LB_LUNA}};
const Line kGiveWay[] = {{"[calm:0.4]Two blocks that way, then left. You can't miss it.", 0}, {"[happy:0.4]Straight down there, on your right.", 0},
                         {"[calm:0.4]Hmm, that way I think. Ask again at the corner.", 0}, {"[calm:0.4]Back the way I came, five minutes.", 0},
                         {"[calm:0.4]Por alli, dos cuadras, a la derecha.", LB_LUNA}};
const Line kAskCopWay[] = {{"[calm:0.4]Excuse me, officer - which way to the pier?", 0}, {"[calm:0.4]Officer? Sorry - we're a little lost.", 0},
                           {"[calm:0.4]Hi - is the station this way?", 0}, {"[calm:0.4]Perdone, oficial... la playa?", LB_LUNA}};
const Line kCopGiveWay[] = {{"[calm:0.4]Sure. Straight down there, two blocks, on your right.", 0}, {"[calm:0.4]Follow this street - you can't miss it.", 0},
                            {"[calm:0.4]Down past the lights. Stay safe now.", 0}, {"[calm:0.4]Right that way, folks. Enjoy your day.", 0},
                            {"[calm:0.4]Por alli, dos cuadras. Cuidese.", LB_LUNA}};
const Line kWayThanks[] = {{"[happy:0.4]Great, thank you!", 0}, {"[happy:0.4]Thanks so much. Have a good one.", 0}, {"[happy:0.4]Perfect, thanks!", 0},
                           {"[happy:0.4]Muchas gracias.", LB_LUNA}};
const Line kHero[] = {{"[shout]Hey! Stop right there!", 0}, {"[shout]Drop it! Drop the bag!", 0}, {"[shout]Got you! Not today, buddy!", 0},
                      {"[shout]Somebody call the cops! I'm on him!", 0}, {"[shout]Alto! Ladron!", LB_LUNA}};
const Line kHeroLost[] = {{"[sad:0.5]Ugh... he's too fast...", 0}, {"[angry:0.5]Forget it. He's gone.", 0}, {"[sad:0.4]I'm too old for this.", 0},
                          {"[angry:0.4]Se me fue... rapidisimo.", LB_LUNA}};
const Line kCopBreak[] = {{"[calm:0.4]Long shift. Feels like it started yesterday.", 0}, {"[happy:0.4]This coffee is terrible. I love it.", 0},
                          {"[calm:0.4]You put in for the weekend yet?", 0}, {"[happy:0.4]Sarge was in a mood this morning.", 0},
                          {"[calm:0.4]Quiet out today. Don't say it out loud.", 0}, {"[calm:0.4]You catch the game last night?", 0},
                          {"[calm:0.4]My kid wants to be a cop now. I told him to be a dentist.", 0}, {"[calm:0.4]Two more hours. Two.", 0},
                          {"[calm:0.4]Este cafe esta buenisimo.", LB_LUNA}};
const Line kWitness[] = {{"[excited:0.5]He was yelling at people before. I noticed him.", 0}, {"[excited:0.5]I saw the whole thing. It happened so fast.", 0},
                         {"[calm:0.4]He came from over there, then your guys showed up.", 0}, {"[scared:0.4]I was just walking by, I swear.", LB_TIMID},
                         {"[happy:0.4]I got it all on my phone if you need it.", 0}, {"[angry:0.4]About time you guys showed up.", LB_BOLD},
                         {"[calm:0.4]Nobody did anything. Everybody just watched.", 0}, {"[excited:0.5]Yo lo vi todo, oficial. Todo.", LB_LUNA}};

#define BANK(k, arr) {k, arr, (int)ARRAY_COUNT(arr)}
const Bank kBanks[] = {
    BANK(BK_GREET, kGreet), BANK(BK_BUMP, kBump), BANK(BK_INSULT, kInsult), BANK(BK_PANIC, kPanic), BANK(BK_FLEE, kFlee),
    BANK(BK_COWER, kCower), BANK(BK_CALL_POLICE, kCallPolice), BANK(BK_PHONE_REPORT, kPhoneReport), BANK(BK_HANDS_UP, kHandsUp),
    BANK(BK_CARJACKED, kCarjacked), BANK(BK_HONK, kHonk), BANK(BK_CRASH, kCrash), BANK(BK_THANKS, kThanks), BANK(BK_HELP, kHelp),
    BANK(BK_DRUNK, kDrunk), BANK(BK_MUSIC_PRAISE, kMusicPraise), BANK(BK_TOURIST, kTourist), BANK(BK_FILMING, kFilming),
    BANK(BK_GANG_WARN, kGangWarn), BANK(BK_GANG_ATTACK, kGangAttack), BANK(BK_GANG_TAUNT, kGangTaunt), BANK(BK_COP_FREEZE, kCopFreeze),
    BANK(BK_COP_GROUND, kCopGround), BANK(BK_COP_SPOTTED, kCopSpotted), BANK(BK_COP_LOST, kCopLost), BANK(BK_COP_CHATTER, kCopChatter),
    BANK(BK_COP_ENGAGE, kCopEngage), BANK(BK_COP_COVER, kCopCover), BANK(BK_COP_ARREST, kCopArrest), BANK(BK_COP_DOWN, kCopDown),
    BANK(BK_MUGGER, kMugger), BANK(BK_VICTIM, kVictim), BANK(BK_ARGUE, kArgue), BANK(BK_RACE, kRace), BANK(BK_MEDIC, kMedic),
    BANK(BK_BREAKDOWN, kBreakdown), BANK(BK_DIVE, kDive), BANK(BK_GUN_SEEN, kGunSeen), BANK(BK_COP_SEARCH, kCopSearch),
    BANK(BK_COP_BACKUP, kCopBackup), BANK(BK_WITNESS_STOP, kWitnessStop), BANK(BK_JOG, kJog), BANK(BK_PHONE_CHAT, kPhoneChat),
    BANK(BK_BOUNCER, kBouncer), BANK(BK_ROAD_RAGE, kRoadRage), BANK(BK_COP_MEGAPHONE, kCopMegaphone),
    BANK(BK_NICE_CAR, kNiceCar), BANK(BK_TICKET, kTicket), BANK(BK_TICKETED, kTicketed),
    BANK(BK_REUNION, kReunion), BANK(BK_SMALLTALK, kSmallTalk), BANK(BK_PARTING, kParting),
    BANK(BK_ARRIVAL, kArrival), BANK(BK_ARRIVED, kArrived), BANK(BK_SENDOFF, kSendoff), BANK(BK_LEAVING, kLeaving),
    BANK(BK_HURT, kHurt), BANK(BK_SAMARITAN, kSamaritan), BANK(BK_ONLOOKER, kOnlooker), BANK(BK_SUSPECT, kSuspect),
    BANK(BK_COP_ESCORT, kCopEscort), BANK(BK_COP_TRANSPORT, kCopTransport), BANK(BK_BRAWL, kBrawl), BANK(BK_BRAWL_FRIEND, kBrawlFriend),
    BANK(BK_COP_STATEMENT, kCopStatement), BANK(BK_COP_STATEMENT_END, kCopStatementEnd), BANK(BK_WITNESS, kWitness),
    BANK(BK_HERO, kHero), BANK(BK_HERO_LOST, kHeroLost), BANK(BK_COP_RADIO, kCopRadio), BANK(BK_ASK_WAY, kAskWay), BANK(BK_GIVE_WAY, kGiveWay), BANK(BK_WAY_THANKS, kWayThanks), BANK(BK_COP_BREAK, kCopBreak), BANK(BK_COP_STOP, kCopStop), BANK(BK_COP_STOP_ASK, kCopStopAsk), BANK(BK_COP_STOP_OK, kCopStopOk), BANK(BK_STOPPED, kStopped), BANK(BK_STOPPED_END, kStoppedEnd),
    BANK(BK_COP_PARKING, kCopParking), BANK(BK_COP_PARKING_REPLY, kCopParkingReply), BANK(BK_PARKING_OWNER, kParkingOwner),
    BANK(BK_PARKING_PROTEST, kParkingProtest), BANK(BK_PARKING_GRUMBLE, kParkingGrumble),
    BANK(BK_GREET_MORNING, kGreetMorning), BANK(BK_GREET_EVENING, kGreetEvening), BANK(BK_COP_GREET, kCopGreet),
    BANK(BK_PROPOSE_ASK, kProposeAsk), BANK(BK_PROPOSE, kPropose), BANK(BK_PROPOSED, kProposed), BANK(BK_PROPOSE_YES, kProposeYes),
    BANK(BK_PROPOSE_NO, kProposeNo), BANK(BK_PROPOSE_SAD, kProposeSad), BANK(BK_CROWD_AWW, kCrowdAww), BANK(BK_CROWD_OOH, kCrowdOoh),
    BANK(BK_STROLL_CHAT, kStrollChat), BANK(BK_STROLL_REPLY, kStrollReply), BANK(BK_COP_BEAT_CHAT, kCopBeatChat),
    BANK(BK_RAIN, kRain), BANK(BK_CROSS_WAIT, kCrossWait), BANK(BK_LOST, kLost), BANK(BK_HONK_PED, kHonkPed),
    BANK(BK_COP_CRASH, kCopCrash), BANK(BK_COP_WAVE, kCopWave), BANK(BK_ASK_OKAY, kAskOkay),
    BANK(BK_COP_EMS, kCopEms), BANK(BK_HORNED, kHorned),
    BANK(BK_PANHANDLE, kPanhandle), BANK(BK_PANHANDLE_THANKS, kPanhandleThanks), BANK(BK_PANHANDLE_NO, kPanhandleNo), BANK(BK_GIVE_CHANGE, kGiveChange),
    BANK(BK_BUS_RUN, kBusRun), BANK(BK_BUS_THANKS, kBusThanks), BANK(BK_BUS_MISSED, kBusMissed), BANK(BK_NEAR_MISS, kNearMiss),
    BANK(BK_COP_JAYWALK, kCopJaywalk), BANK(BK_JAYWALK_REPLY, kJaywalkReply), BANK(BK_CROWDED, kCrowded), BANK(BK_CROWDED_LEAVE, kCrowdedLeave),
    BANK(BK_ASK_COP_WAY, kAskCopWay), BANK(BK_COP_GIVE_WAY, kCopGiveWay), BANK(BK_CUT_OFF, kCutOff), BANK(BK_FOLLOWED, kFollowed), BANK(BK_FOLLOWED_SCARED, kFollowedScared), BANK(BK_FOLLOWED_BOLD, kFollowedBold),
    BANK(BK_ALARM_GRUMBLE, kAlarmGrumble), BANK(BK_ALARM_WITNESS, kAlarmWitness), BANK(BK_ALARM_OWNER, kAlarmOwner), BANK(BK_ALARM_CHECKED, kAlarmChecked),
    BANK(BK_ALARM_ACCUSE, kAlarmAccuse), BANK(BK_ALARM_THEFT, kAlarmTheft), BANK(BK_ALARM_GONE, kAlarmGone),
    BANK(BK_COP_STEP_BACK, kCopStepBack), BANK(BK_BUS_WAIT, kBusWait), BANK(BK_BUS_WAIT_REPLY, kBusWaitReply),
    BANK(BK_PLAYER_DOWN, kPlayerDown), BANK(BK_DRIVER_SORRY, kDriverSorry),
    BANK(BK_TIP_OFF, kTipOff), BANK(BK_TIP_HERE, kTipHere), BANK(BK_COP_TIP, kCopTip), BANK(BK_ASK_SNUBBED, kAskSnubbed),
    BANK(BK_HIT_ANGRY, kHitAngry), BANK(BK_DRIVER_CALL, kDriverCall), BANK(BK_NIGHT_CHAT, kNightChat), BANK(BK_NIGHT_REPLY, kNightReply),
    BANK(BK_CRASH_SEEN, kCrashSeen), BANK(BK_COP_ASK_SEEN, kCopAskSeen), BANK(BK_NOT_SEEN, kNotSeen),
    BANK(BK_SHOW_CHEER, kShowCheer), BANK(BK_SHOW_TUT, kShowTut), BANK(BK_SHOW_END, kShowEnd), BANK(BK_COP_SHOW, kCopShow),
    BANK(BK_SCREECH, kScreech), BANK(BK_WALKING_HERE, kWalkingHere), BANK(BK_SIDEWALK_CAR, kSidewalkCar), BANK(BK_RUN_PAST, kRunPast),
    BANK(BK_GOOD_SONG, kGoodSong), BANK(BK_LOUD_MUSIC, kLoudMusic),
};
#undef BANK

const char* speakerName(const Ped& p, u8 role) {
    if (p.faction == FAC_POLICE) return "Officer";
    if (p.faction == FAC_GANG_CUERVOS) return "Cuervo";
    if (p.faction == FAC_GANG_SAINTS) return "Saint";
    if (p.faction == FAC_MEDIC) return "Paramedic";
    switch (role) {
        case PR_TOURIST: return "Tourist";
        case PR_DRUNK: return "Drunk";
        case PR_MUSICIAN: return "Musician";
        case PR_JOGGER: return "Jogger";
        default: break;
    }
    if (p.state == PS_INVEHICLE) return "Driver";
    return p.female ? "Woman" : "Man";
}

}  // namespace barks_detail

using namespace barks_detail;

void GameWorld::aiSay(int pid, int kind, float chance, bool important) {
    if (pid < 0 || pid >= (int)peds.size() || !peds[pid].used || kind < 0 || kind >= BK_COUNT) return;
    Ped& p = peds[pid];
    if (p.isPlayer || p.health <= 0.f) return;
    PedAI& pa = pedAI(pid);
    if (pa.barkCooldown > 0.f || p.speechCooldown > 0.f) return;
    if (!important && ai.barkGlobal > 0.f) return;
    u32 h = hash32(p.uid * 2654435761u + (u32)kind * 7919u + (u32)(time * 13.0));
    if (hashToFloat(h) > chance) {
        pa.barkCooldown = 1.5f;
        return;
    }
    // speech only matters near the listener
    vec3 head = pedHeadPos(p);
    float camD = length(rel(dvec3(head), rig.cam.pos));
    if (camD > 60.f) return;
    const Bank* bank = nullptr;
    for (const Bank& b : kBanks)
        if (b.kind == kind) bank = &b;
    if (!bank || bank->count == 0) return;
    World::Region reg = map->regionAt(head.x, head.y);
    u8 want = 0;
    if (reg == World::REG_CALLE_LUNA || reg == World::REG_FLATS) want |= LB_LUNA;
    if (reg == World::REG_BEACH || reg == World::REG_KEY_CORAL) want |= LB_BEACH;
    if (reg == World::REG_DOWNTOWN || reg == World::REG_FINANCIAL) want |= LB_DOWNTOWN;
    int candidates[24];
    int n = 0;
    for (int i = 0; i < bank->count && n < 24; i++) {
        u8 f = bank->lines[i].flags;
        if ((f & LB_BOLD) && pa.temper != 2) continue;
        if ((f & LB_TIMID) && pa.temper != 0) continue;
        if ((f & (LB_LUNA | LB_BEACH | LB_DOWNTOWN)) && !(f & want)) continue;
        if ((f & LB_FEMALE) && !p.female) continue;
        if ((f & LB_MALE) && p.female) continue;
        candidates[n++] = i;
    }
    if (n == 0) return;
    const Line& line = bank->lines[candidates[hash32(h) % n]];
#ifdef HAVE_AUDIO
    float vol = kind == BK_COP_CHATTER ? 0.75f : 1.f;
    // a different "take" each time a ped repeats a line (pitch/range/pace/accent details vary)
    std::string spoken = StrFormat("[take:%u]", hash32(p.uid * 31u + (u32)(time * 7.0)) % 9u) + line.text;
    Audio::speakAt(spoken.c_str(), p.voice, head, vol);
    startLipSync(pid, spoken.c_str(), p.voice);
    float dur = Audio::estimateSpeechDuration(spoken.c_str(), p.voice);
#else
    float dur = 1.5f;
#endif
    p.speechCooldown = dur + 0.4f;
    pa.barkCooldown = dur + 3.f + hashToFloat(hash32(h + 1u)) * 4.f;
    ai.barkGlobal = Min(dur * 0.5f, 1.2f);
    // subtitles only when close to the player (or for important lines within earshot)
    Ped* pl = playerPed();
    if (pl && settingsSubtitles && subTimer <= 0.2f) {
        float d = length(rel(p.pos, pl->pos));
        if (d < 12.f || (important && d < 25.f)) subtitle(kind == BK_BOUNCER ? "Bouncer" : speakerName(p, pa.role), line.text, Max(dur, 1.2f) + 0.6f, 0xffd0d0d0u);
    }
}

}  // namespace Game

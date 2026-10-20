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

const Line kGreet[] = {{"[happy:0.4]Morning.", 0}, {"[happy:0.4]Nice day, huh?", 0}, {"[happy:0.4]Hey there.", 0}, {"[happy:0.4]Buenas.", LB_LUNA}, {"[happy:0.4]Beautiful out here.", LB_BEACH},
                       {"[happy:0.4]Excuse me, running late.", LB_DOWNTOWN}, {"[happy:0.4]How you doing?", 0}, {"[happy:0.4]Oye, que tal?", LB_LUNA}};
const Line kBump[] = {{"[angry:0.5]Watch it!", 0}, {"[angry:0.5]Hey, eyes up!", 0}, {"[angry:0.5]Excuse you.", 0}, {"[scared:0.4]Sorry, sorry.", LB_TIMID}, {"[angry:0.5]Do you mind?", 0},
                      {"[angry:0.5]Careful, pal.", LB_BOLD}, {"[angry:0.5]Seriously?", 0}, {"[angry:0.5]Mira por donde vas!", LB_LUNA}};
const Line kInsult[] = {{"[angry]You got a problem?", LB_BOLD}, {"[angry]Keep walking.", 0}, {"[angry]Unbelievable.", 0}, {"[angry]What is wrong with you?", 0},
                        {"[angry]Back off!", LB_BOLD}, {"[angry:0.5]Tourists. Every time.", LB_BEACH}};
const Line kPanic[] = {{"[scared]Oh my god!", 0}, {"[scared]What was that?", 0}, {"[scared]Somebody help!", 0}, {"[scared]No no no!", LB_TIMID}, {"[shout]Get down!", LB_BOLD},
                       {"[scared]Dios mio!", LB_LUNA}, {"[scared]Is that a gun?", 0}, {"[scared]Are you kidding me?", 0}};
const Line kFlee[] = {{"[scared]Run!", 0}, {"[scared]Get out of here!", 0}, {"[scared]Move, move!", LB_BOLD}, {"[scared]I am out of here!", 0}, {"[scared]Corre, corre!", LB_LUNA},
                      {"[scared]Everybody run!", 0}, {"[scared]Not today!", 0}};
const Line kCower[] = {{"[scared:1.2]Please don't hurt me!", 0}, {"[scared:1.2]I have kids!", 0}, {"[scared:1.2][whisper:0.4]Please, please.", LB_TIMID}, {"[scared:1.2]Leave me alone!", 0},
                       {"[scared:1.2]Take whatever you want!", 0}, {"[scared:1.2]Por favor, no!", LB_LUNA}};
const Line kCallPolice[] = {{"[angry:0.7]I'm calling the cops!", 0}, {"[scared]Somebody call nine one one!", 0}, {"[angry:0.7]I'm getting the police!", 0},
                            {"[angry:0.7]You are so going to jail!", LB_BOLD}};
const Line kPhoneReport[] = {{"[scared:0.8]Police? Somebody's shooting on the street!", 0}, {"[scared:0.8]Yes, hello, there's a crazy person here!", 0},
                             {"[scared:0.8]I need the police, right now!", 0}, {"[scared:0.8]Someone just got attacked, send somebody!", 0},
                             {"[scared:0.8]Hello? There's a guy with a gun!", 0}};
const Line kHandsUp[] = {{"[scared]Okay, okay! Easy!", 0}, {"[scared]Don't shoot!", 0}, {"[scared]I don't want trouble!", 0}, {"[scared]Whoa, whoa, calm down!", LB_BOLD},
                         {"[scared]Take it easy, please!", LB_TIMID}};
const Line kCarjacked[] = {{"[shout]Hey! That's my car!", 0}, {"[angry]Are you crazy? Get out!", LB_BOLD}, {"[shout]My car! Somebody stop him!", 0},
                           {"[angry]You're not taking my ride!", LB_BOLD}, {"[angry]Ladron! Mi carro!", LB_LUNA}};
const Line kHonk[] = {{"[angry:0.7]Move it!", 0}, {"[angry:0.7]Come on, come on!", 0}, {"[angry:0.7]Today, please!", 0}, {"[shout]Get out of the road!", LB_BOLD},
                      {"[angry:0.7]Are you parked there?", 0}, {"[angry:0.7]Vamos, muevete!", LB_LUNA}, {"[shout]Learn to drive!", LB_BOLD}};
const Line kCrash[] = {{"[angry]You hit my car!", 0}, {"[angry]Look what you did!", 0}, {"[angry]My insurance!", 0}, {"[angry]Were you even looking?", 0},
                       {"[angry]Brand new paint job!", LB_BOLD}};
const Line kThanks[] = {{"[happy]Thank you so much!", 0}, {"[happy]You're a lifesaver!", 0}, {"[happy]Thanks, I owe you one.", 0}, {"[happy]Gracias, de verdad.", LB_LUNA},
                        {"[happy]Here, take this. You earned it.", 0}};
const Line kHelp[] = {{"[scared:0.5]Hey! Can you help me?", 0}, {"[scared:0.5]Excuse me, over here!", 0}, {"[scared:0.5]Could you give me a hand?", 0}, {"[scared:0.5]Please, I need some help!", 0}};
const Line kDrunk[] = {{"[drunk]Where did I park the boat?", 0}, {"[drunk]Everybody loves me.", 0}, {"[drunk]This sidewalk keeps moving.", 0},
                       {"[drunk]One more round, for the road.", 0}, {"[drunk]I'm fine. I'm totally fine.", 0}, {"[drunk]Who turned the street sideways?", 0}};
const Line kMusicPraise[] = {{"[happy]Play another one!", 0}, {"[happy]This guy is good!", 0}, {"[happy]Love this song.", 0}, {"[happy]Otra, otra!", LB_LUNA}};
const Line kTourist[] = {{"[happy]Get the palm trees in!", 0}, {"[happy]Smile, honey!", 0}, {"[happy]One more, with the ocean.", LB_BEACH},
                         {"[happy]Is this the famous street?", 0}, {"[happy]Look at that sunset!", 0}};
const Line kFilming[] = {{"[happy:0.6]Oh, this is going online.", 0}, {"[happy:0.6]Are you getting this?", 0}, {"[shout:0.6]Fight, fight!", LB_BOLD}, {"[happy:0.6]No way, look at this!", 0}};
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
const Line kDive[] = {{"[scared]Whoa!", 0}, {"[shout]Look out!", 0}, {"[scared]Are you insane?", 0}, {"[scared]Watch where you're going!", 0}, {"[scared]Maniac!", LB_BOLD}};
const Line kGunSeen[] = {{"[scared]Whoa, he's got a gun!", 0}, {"[angry:0.3]Easy with that thing.", LB_BOLD}, {"[scared]Oh no, no, no.", LB_TIMID},
                         {"[scared]Put that away, man.", 0}, {"[scared]Is that real?", 0}};
const Line kCopSearch[] = {{"[dispatch]Check the alleys.", 0}, {"[dispatch]He's around here somewhere.", 0}, {"[dispatch]Search the area.", 0}, {"[dispatch]Keep your eyes open.", 0}};
const Line kCopBackup[] = {{"[dispatch]Backup is on the way!", 0}, {"[radio][dispatch]Heavy units responding.", 0}, {"[radio][dispatch]Air support inbound.", 0}};
const Line kWitnessStop[] = {{"[scared]Okay, I hung up! I hung up!", 0}, {"[scared]I didn't see anything!", 0}, {"[scared]I won't call, I swear!", 0}};
const Line kJog[] = {{"On your left.", 0}, {"Morning!", 0}, {"Five more miles.", 0}};
const Line kPhoneChat[] = {{"No, I told her already.", 0}, {"Are you serious? No way.", 0}, {"I'll be there in ten.", 0},
                           {"Can you hear me now?", 0}, {"Yeah, yeah, I know.", 0}, {"Mira, te llamo luego.", LB_LUNA}};

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
const Line kAskWay[] = {{"[calm:0.4]Excuse me, sorry - is the bus station this way?", 0}, {"[happy:0.4]Hi! Do you know where the pier is?", LB_BEACH},
                        {"[calm:0.4]Sorry to bother you, is there a pharmacy around here?", 0}, {"[calm:0.4]Excuse me, which way to the museum?", LB_DOWNTOWN},
                        {"[calm:0.4]Hi, I think I'm lost. Where's the nearest train stop?", 0}, {"[calm:0.4]Perdone, donde queda la parada del bus?", LB_LUNA}};
const Line kGiveWay[] = {{"[calm:0.4]Two blocks that way, then left. You can't miss it.", 0}, {"[happy:0.4]Straight down there, on your right.", 0},
                         {"[calm:0.4]Hmm, that way I think. Ask again at the corner.", 0}, {"[calm:0.4]Back the way I came, five minutes.", 0},
                         {"[calm:0.4]Por alli, dos cuadras, a la derecha.", LB_LUNA}};
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

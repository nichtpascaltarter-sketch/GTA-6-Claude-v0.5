#include <cstdarg>
#include <time.h>
#include "audio/speech.cpp"
void LogPrintf(const char*, ...) {}
[[noreturn]] void FatalError(const char*, ...) { exit(1); }
std::string StrFormat(const char*, ...) { return ""; }
double TimeSeconds() { return 0; }
static double cpuNow() { timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
int main() {
    const char* texts[] = {
        "Get in the car, we have to go now!", "The police are coming. Drive!",
        "Welcome to Porto Sol, the city that never sleeps.", "You've got two hundred and fifty dollars. Is that all?",
        "It's ten thirty on a beautiful Tuesday morning.", "I need the money by tomorrow night, or you're dead.",
        "Put your hands up and step away from the vehicle.", "Suspect is heading north on the highway in a red sedan.",
        "Hey, watch where you're going, buddy!", "My brother works down at the docks near the beach.",
        "Don't worry about it, I'll take care of everything.",
        "This is the hottest station in the city, playing the best music all day long."};
    Audio::VoiceParams vm = Speech::presetVoice(false, 3), vf = Speech::presetVoice(true, 5);
    for (int rate : {22050, 44100, 48000}) {
        double audio = 0, cpu = 0, front = 0;
        for (int rep = 0; rep < 3; rep++)
            for (const char* t : texts)
                for (int g = 0; g < 2; g++) {
                    std::vector<float> x;
                    double c0 = cpuNow();
                    Speech::synthesize(t, g ? vf : vm, rate, x);
                    double c1 = cpuNow();
                    Speech::estimateDuration(t, g ? vf : vm);
                    double c2 = cpuNow();
                    cpu += c1 - c0; front += c2 - c1; audio += (double)x.size() / rate;
                }
        printf("%5d Hz: %.1f s audio, %.1f ms CPU -> %.0fx real time per core; estimateDuration avg %.3f ms\n", rate,
               audio, cpu * 1000, audio / cpu, front * 1000 / (3 * 12 * 2));
    }
}

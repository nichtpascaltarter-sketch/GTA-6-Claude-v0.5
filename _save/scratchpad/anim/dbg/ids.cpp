#include "anim/character.h"
#include "anim/anim_internal.h"
#include <cstdio>
int main() { printf("CLIP_COUNT %d IC_JOG_SLOW %d IC_GAIT_FIRST %d elderly-normal %d swagger-normal %d IC_END %d\n", (int)Anim::CLIP_COUNT, (int)Anim::detail::IC_JOG_SLOW, (int)Anim::detail::IC_GAIT_FIRST, Anim::detail::gaitClip(3,1), Anim::detail::gaitClip(4,1), (int)Anim::detail::IC_END); }

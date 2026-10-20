// Character module (unity include): skeleton, procedural body/face/outfit/hair meshes, clips and animation runtime.
#include "character.h"
#include "anim_internal.h"
#include "skeleton.cpp"
#include "meshutil.cpp"
#include "bodymesh.cpp"
#include "face.cpp"
#include "clothing.cpp"
#include "hair.cpp"
#include "decimate.cpp"
#include "character.cpp"
#include "pose.cpp"
#if __has_include("clips.cpp")
#include "clips.cpp"
#include "animator.cpp"
#define ANIM_HAVE_CLIPS 1
#endif

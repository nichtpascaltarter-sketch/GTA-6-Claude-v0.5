// Game version: shown in the main menu and compared with the published releases by the updater. Release builds set
// it from the release tag: VERSION=0.02.7 ./build.sh (Linux), or set VERSION=0.02.7 before build.bat (Windows).
// Between releases the source carries the next version with a pre-release suffix, which sorts before that release.
#pragma once

#ifndef NT_VERSION
#define NT_VERSION "0.02.7-dev"
#endif

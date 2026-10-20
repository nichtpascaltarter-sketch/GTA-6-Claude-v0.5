// Game version and source commit, shown in the main menu (the version, a dot, the commit's first 7 digits) and the log.
// NT_VERSION names the version (VERSION=x ./build.sh, or set VERSION=x before build.bat, overrides it). NT_COMMIT is
// the commit the build was made from, which the updater compares with GitHub's main: build.sh and build.bat pass the
// NT_COMMIT environment variable (the updater sets it for the builds it makes), else git's HEAD when the source
// folder is a git checkout, else "unknown".
#pragma once

#ifndef NT_VERSION
#define NT_VERSION "0.02.7-dev"
#endif

#ifndef NT_COMMIT
#define NT_COMMIT "unknown"
#endif

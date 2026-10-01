#!/bin/sh
# One-time setup of the Linux test environment (NOT needed to build or play on Windows).
# Installs the MinGW-w64 cross compiler, Wine, Xvfb and Mesa software rendering, and fetches
# Microsoft's d3dcompiler_47.dll (the same shader compiler that ships with Windows 10/11)
# so shaders are validated with the real FXC compiler under Wine. The DLL is used only for
# testing and is never shipped: Windows provides it in System32.
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update || true
apt-get install -y --no-install-recommends g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix mingw-w64-x86-64-dev \
  binutils-mingw-w64-x86-64 wine64 mesa-vulkan-drivers libvulkan1 xvfb imagemagick
mkdir -p /opt/neontide-test
if [ ! -f /opt/neontide-test/d3dcompiler_47.dll ]; then
  cd /tmp
  curl -sSL -o electron.zip https://github.com/electron/electron/releases/download/v30.0.0/electron-v30.0.0-win32-x64.zip
  python3 -c "import zipfile;z=zipfile.ZipFile('electron.zip');open('/opt/neontide-test/d3dcompiler_47.dll','wb').write(z.read('d3dcompiler_47.dll'))"
  rm -f electron.zip
fi
echo "Test environment ready."

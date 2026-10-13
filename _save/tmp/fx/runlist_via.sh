#!/bin/sh
# SkyLine viaduct curve (diagonal dry strip under the deck) in daytime rain, before/after, roughness split view
until [ -f /tmp/fx/more.done ]; do sleep 20; done
cd /tmp/fx
for B in a b; do
  EXE=bin/nt_d12$B.exe TIMEOUT=2400 SHOTS="3226.4,-580.1,5,-45,-20,13.0,via_a 3230.7,-584.4,7,45,-25,13.0,via_b" sh /tmp/fx/shoot.sh d12${B}_via --rain 1.0 --settle 8 --quality 1 --debugsplit 5
done
echo done > /tmp/fx/via.done

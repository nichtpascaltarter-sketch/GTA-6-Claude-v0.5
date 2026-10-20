#!/bin/bash
until grep -q "syntax rc=" /tmp/city/snap_b4.out 2>/dev/null; do sleep 15; done
rm -f /tmp/city/jobs6.status
exec /tmp/city/jobs6.sh

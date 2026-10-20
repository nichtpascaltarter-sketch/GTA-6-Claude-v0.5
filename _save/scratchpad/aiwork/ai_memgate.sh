#!/bin/sh
# AI agent: wait until tools/memfree.sh reports at least MB free (default 3000) and the lead's flag
# (/tmp/neontide_lead_wants, raised by the lead's snapshot gates: the scratchpad's memgate.sh) is not fresh (younger than
# 5 minutes) - the lead's launches go first. Just a wait: it raises no flag itself.
NEED=${1:-3000}
while [ -n "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ] || [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt "$NEED" ]; do sleep 15; done

#!/bin/sh
# AI agent: wait until tools/memfree.sh reports at least MB free (default 3000). Just a wait - it sets no flag (the
# scratchpad's memgate.sh is the lead's: it raises /tmp/neontide_lead_wants, which holds everybody else's launches).
NEED=${1:-3000}
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt "$NEED" ]; do sleep 15; done

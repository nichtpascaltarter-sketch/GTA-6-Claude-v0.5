#!/bin/sh
# usage: sh verify.sh <tree>   builds and runs worldv's test and worldv itself in <tree> (with CI's GCC flags)
cd "${1:?usage: verify.sh <tree>}" || exit 1
g++ -std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined tests/world/worldv_test.cpp -o /tmp/neontide-worldv-test && /tmp/neontide-worldv-test || exit 1
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tools/worldv.cpp -o /tmp/neontide-worldv || exit 1
/tmp/neontide-worldv --world=world --no-cache || exit 1
/tmp/neontide-worldv tests/world/fixtures/samples || exit 1
/tmp/neontide-worldv --world=tests/world/fixtures/world --no-cache || exit 1
echo "verify: all ok"

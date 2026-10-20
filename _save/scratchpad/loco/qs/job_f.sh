#!/bin/bash
# job_f.sh: LM_FIXSWW (the late re-scan's keep, SW, at a walk only: faded out from 2 to 3 m/s as the run's lead fades in).
# Rebuild lm_d15 (dtree15 + the switch), check the default is unchanged (c14bdbg fs_p0_30), then player speeds and the
# full eval with it -> r9/pl_dbg_sww_*, r9/ev2_c14w_*
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
B14="LM_FIXLEAD=1 LM_FIXQS=1 LM_FIXQSG=4 LM_FIXQSU=0.5 LM_FIXSW=0.15 LM_FIXSEEN=1"
sh mk2.sh qs/dtree15 lm_d15
env $B14 LM_HZ=30 LM_TURNPROF=0 ./sl/lm_d15 > r9/chk_sww_default.txt 2>/dev/null
cmp -s <(grep "^M" r9/chk_sww_default.txt) <(grep "^M" r9/ev2_c14bdbg_fs_p0_30.txt) && echo "default unchanged" || echo "DEFAULT CHANGED"
for v in 4.1 6.9; do for hz in 60 30; do pr=$([ $hz = 60 ] && echo 2 || echo 0)
  env $B14 LM_FIXSWW=1 LM_IREP=4 LM_IV=$v LM_HZ=$hz LM_TURNPROF=$pr ./sl/lm_d15 switchback > r9/pl_dbg_sww_sb_${v}_$hz.txt 2>/dev/null
  env $B14 LM_FIXSWW=1 LM_SV=$v LM_HZ=$hz LM_TURNPROF=$pr ./sl/lm_d15 stairs > r9/pl_dbg_sww_s_${v}_$hz.txt 2>/dev/null
done; done
sh qs/evalv2.sh lm_d15 c14w $B14 LM_FIXSWW=1 >/dev/null 2>&1
echo "job_f done"

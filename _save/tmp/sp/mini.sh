#!/bin/sh
# usage: mini.sh BINARY LISTFILE OUTDIR [asr]
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
rm -rf $3 && mkdir -p $3 && $1 --out $3 --focus $2 > /dev/null
cd $S && ./asrenv/bin/python /home/user/GTA-6-Claude-v0.5/tests/speech/asr_eval.py --models $S/models --manifest $3/manifest_focus.tsv --asr ${4:-parakeet} 2>&1 | grep -v "sherpa-onnx/csrc\|in_sample_rate\|output_sample_rate\|^$" > $3/result.txt
grep "==" $3/result.txt
grep -A1 "ERR" $3/result.txt | grep "HYP\|REF" | paste - - | awk -F'REF: |HYP: ' '{print $2 " => " $3}' | sed 's/  */ /g' | sort | uniq -c | sort -rn

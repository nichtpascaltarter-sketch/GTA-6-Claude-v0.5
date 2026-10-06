#!/bin/sh
# quick focused ASR experiment: build, synthesize focus sentences, evaluate
cd /home/user/GTA-6-Claude-v0.5 && g++ -std=c++17 -O2 -I src tests/speech/test_speech.cpp -o /tmp/test_speech_f -lpthread || exit 1
rm -rf /tmp/focus && mkdir -p /tmp/focus && /tmp/test_speech_f --out /tmp/focus --focus /tmp/sp/focus.txt
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd $S && ./asrenv/bin/python /home/user/GTA-6-Claude-v0.5/tests/speech/asr_eval.py --models $S/models --manifest /tmp/focus/manifest_focus.tsv --asr ${1:-whisper-base,parakeet} 2>&1 | grep -v "sherpa-onnx/csrc\|in_sample_rate\|output_sample_rate\|^$" > /tmp/focus/result.txt
grep "==" /tmp/focus/result.txt
grep -A1 "ERR" /tmp/focus/result.txt | grep "HYP\|REF" | paste - - | awk -F'REF: |HYP: ' '{print $2 " => " $3}' | sort | uniq -c | sort -rn

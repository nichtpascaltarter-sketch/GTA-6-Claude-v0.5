#!/bin/sh
# usage: asr.sh manifest models...
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
MAN=$1; shift
cd $S && ./asrenv/bin/python /home/user/GTA-6-Claude-v0.5/tests/speech/asr_eval.py --models $S/models --manifest $MAN --asr ${1:-whisper-base,parakeet} 2>&1 | grep -v "sherpa-onnx/csrc\|in_sample_rate\|output_sample_rate\|^$"

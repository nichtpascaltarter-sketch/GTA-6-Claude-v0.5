#!/bin/sh
# Full evaluation: words + main + heldout, parakeet + whisper-base. Output to $1
OUT=$1
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
/tmp/test_speech --out /tmp/words --words
/tmp/test_speech --out /tmp/speech_out > /tmp/speech_out/log.txt
cd $S
./asrenv/bin/python /home/user/GTA-6-Claude-v0.5/tests/speech/asr_eval.py --models $S/models --manifest /tmp/words/manifest_words.tsv --asr parakeet,whisper-base --words --batch 8 --threads 3 2>&1 | grep -v "sherpa-onnx/csrc\|in_sample_rate\|output_sample_rate\|^$" > $OUT.words
./asrenv/bin/python /home/user/GTA-6-Claude-v0.5/tests/speech/asr_eval.py --models $S/models --manifest /tmp/speech_out/manifest.tsv --asr parakeet,whisper-base --batch 8 --threads 3 2>&1 | grep -v "sherpa-onnx/csrc\|in_sample_rate\|output_sample_rate\|^$" > $OUT.main
./asrenv/bin/python /home/user/GTA-6-Claude-v0.5/tests/speech/asr_eval.py --models $S/models --manifest /tmp/speech_out/manifest_heldout.tsv --asr parakeet,whisper-base --batch 8 --threads 3 2>&1 | grep -v "sherpa-onnx/csrc\|in_sample_rate\|output_sample_rate\|^$" > $OUT.held
echo DONE

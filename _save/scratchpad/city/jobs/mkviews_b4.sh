#!/bin/bash
# batch-4 clay views: block houses (North City, Westbrook), folk Victorians (Okahatchee, Fort Castell, Harlow), a MiMo
# carport, strip courts and a gas station; finds the buildings with citylab_dev4 --find
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5
$S/citylab/citylab_dev4 --find 52,2495,4012 --find 52,-1317,-845,1 --find 53,144,6475,1 --find 53,4089,7290,0 --find 53,-4886,5241,1 --find 22,2495,4012 > /tmp/city/find_b4.txt 2>&1
grep "^find" /tmp/city/find_b4.txt

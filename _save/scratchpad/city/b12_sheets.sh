#!/bin/sh
# pair sheets for batches 12 + 14 + 15 (before nt_dev23b, after nt_dev23)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
B=/tmp/city/dev22_before_shots; A=/tmp/city/dev22_shots; O=$S/city/sheets/b12; mkdir -p $O
P="python3 $S/city/pair_sheet.py"
$P $B $A $O/storefront1.png "Batch 12: storefront only on street walls (1/3) - North Porto Sol corner walk-up" nps_corner_day nps_corner_night nps_back_day nps_back_night side_nps_b170_day side_nps_b170_night
$P $B $A $O/storefront2.png "Batch 12: storefront only on street walls (2/3) - side walls in the Grove, Calle Luna, Canvas, downtown" side_grove_b1151_day side_cl_b25294_day side_cl_b25294_night side_canvas_b29768_day side_dt_b30990_day side_dt_b30990_night
$P $B $A $O/storefront3.png "Batch 12: storefront only on street walls (3/3) - street fronts, should be unchanged" front_cl1_day front_cl1_night front_mt1_day front_sb1_day
$P $B $A $O/bags.png "Batch 14: soft trash bags" bags_nps_close_day bags_nps_close_night bags_nps_mid_day
$P $B $A $O/beacons.png "Batch 15: aviation beacons at night" beacon_solaris_night beacon_pier_night beacon_cityhall_night

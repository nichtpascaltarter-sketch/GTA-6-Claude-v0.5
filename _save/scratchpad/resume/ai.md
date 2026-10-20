# AI agent: 17:58 PARKED by the lead (user's usage nearly gone). Everything of mine stopped by PID; nothing running.
- SET 20 (incl. the stair pace cap) is CODE-COMPLETE, NOT signed off. Final sources AW/s20f, blobs on HEAD 7b9146d
  (AW/graft_s20f.txt): traffic_core 3e295667, ai.cpp 4043ee13, ai_game.h e53d15c4, pedai 71147af4, police 58b0776a,
  population c0bbd994; ai_core.h = HEAD; harness tools/traffic_sim.cpp 2f9e6172 (= AW/s20f_traffic_sim.cpp = AW/s20/).
  Exes built: bin/nt_s20f.exe (clean, -Wall 0 warnings) and bin/nt_s20ft.exe (+ test app.cpp c8bb416c, locostairs).
- Still to do for the sign-off: (1) harness chain AW/ts_chain20f.sh 61f was killed after q_ramp_61f_1/2, q_mid_61f_1
  (restart-safe: AW/ai_detach.sh AW/ts_chain20f.out bash AW/ts_chain20f.sh 61f; table: python3 AW/ts_table20.py);
  (2) game tests: queue AW/queue.txt still holds locostairs (s20ft), soak, life, chase (s20f); hwysoak was killed mid-run
  -> re-add "bin/nt_s20f.exe hwysoak:300:12:30:960x540"; start the runner: AW/ai_detach.sh AW/queue_runner_s20f.out bash
  AW/ai_queue.sh; (3) sign-off text: AW/signoff_set20_draft.txt (FILES done; WHAT/HOW/TESTS from the notes below).
- Evidence so far (tree61): highway lane keeping s20 vs base: cars RMS 0.06 vs 0.27-0.34 m, long 0.04 vs 0.10-0.19,
  gore-barrier hits 0 vs 3 (h_* runs); meet test 2907 (old world): 0 touched / 0 not past both pairs (base 5 and 7
  touched); u-turn 4870 all 38 round; overlaps61 = 0 pairs.
- NEXT after set 20: RoadOverrides and the hand-mode assumptions question (the lead: waiting for me after the pace cap).
- Exes: nt_ai105 may go (my rm was denied by the permission system); nt_s20b/s/t (old stair rule) may go after sign-off.

# AI agent: 17:40 SET 20 FINAL CANDIDATE = AW/s20f (frozen; blobs in AW/graft_s20f.txt on HEAD 7b9146d):
  traffic_core 3e295667, ai.cpp bb1a2616, ai_game.h ae7ea24d, pedai 71147af4, police 58b0776a, population 51d67e96;
  ai_core.h = HEAD (unchanged). Harness tools/traffic_sim.cpp = AW/s20f_traffic_sim.cpp be496fc5 (copy of AW/s20).
- Item 4 found the real cause: maxSteerAt (the AI's command normaliser) used 1.25 g + 1.1 x alphaPeak at any speed while
  the vehicle sim (vehicle_sim_wheels.cpp carSteer, sim agent's speed-sensitive lock) gives Lerp(1.25,1.05,hs) g +
  alphaPeak x Lerp(0.5,0.25,hs): at 30 m/s the wheels turned ~0.35 of the wanted angle. Matched it. Highway lane keeping
  (h_* runs, tree61): cars in lane RMS 0.27-0.34 -> 0.06 m, over 0.5 m 8-13% -> <0.1%; long vehicles RMS 0.10-0.19 ->
  0.04, max 0.58-1.18 -> 0.26-0.32; static impacts 3 -> 0 (4 runs). Plus lane-change room on motorway links
  (chooseConnector per-shift room v*3.3+12 with a fallback pass; need-lcMin 2.5 s for long vehicles; discretionary room).
- Step-over guards added after harness finds: road works (World::roadWorkZoneAt along the strip; a Stevedore and a
  Packhorse stepped into the barriers), start only below 7 m/s, never at a standstill (v > 0.5), only for a meeting out on
  the road (oncoming driver on a lane) and with our tail 3 m past the lane start (61m seed 3: a truck at node 5038 held in
  its step by a coach turning left = 28 waits). Unused bestK / Sample.k removed.
- RUNNING: harness chain AW/ts_chain20f.sh 61f (PID in AW/ts_chain20f.pid; marker SP/ts/chain20f.done; restart-safe,
  relaunch: AW/ai_detach.sh AW/ts_chain20f.out bash AW/ts_chain20f.sh 61f): overlaps61, 15 runs + meettest + uturntest for
  61f and 61b, ramps seeds 4-6, stair probes. Game builds AW/bld_s20f.sh (nt_s20f clean, nt_s20ft = + test app.cpp
  c8bb416c) -> AW/bld_s20f.out; queue runner waits for nt_s20f (AW/runner_after_s20f.sh); queue: locostairs (s20ft),
  hwysoak, soak, life, chase (s20f). Base for locostairs = nt_s20b's earlier run (same AI/anim as HEAD).
- lanes.cpp corner links: NOT cheap (walk nodes on the inside of sharp bends sit on the other approach's carriageway:
  set back by crossS/cut+1 < where the inner kerbs cross; fixing moves crosswalks/stop lines at 3+ way nodes) -> report.

# AI agent: 16:50 SET 20 IN PROGRESS (2). HEAD is now 7b9146d (snapshot 61 MERGED as c7a63ae: set 19 blobs + anim c14 +
  overlap fix + world model; then 7b9146d updater). My base files on HEAD = set 19's exactly; ai_game.h / pedai /
  police / population / app.cpp / lanes.cpp unchanged since bcb8a71. bin/nt_ai105.exe may now go - my rm was DENIED by
  the permission system (do not retry; report it). Test exes nt_s20b/s/t (old stair rule) also to delete at the end.
- STAIR RULE (final, ai.cpp + harness --stairprobe): ground at -0.3/0/+0.3/+0.6/+0.9 m, each from the one before; a flight
  = >=2 steps of 10-40 cm all one way and nothing >=40 cm (kills the downtown corner false positives). nt_s20b/s/t have
  the OLD rule (10-60 cm) - the final exe must be rebuilt.
- 3 MEETING (AW/s20/traffic_core.cpp): meetWait REMOVED (standoffs). Now: meetTight (oncoming, < 0.9 m between bodies,
  from our lane line without our own nudge; also while alongside until past) -> crawl 3 / 6 m/s, AND a step over into
  the kerb-side paved strip (stripW = E.halfWidth - |L.offset| - L.width/2, two-way, no bridge/elevated/highway/ramp/
  one-way; strip clear from our tail to 30 m on; lane left > 15 m): nudgeTarget = min(laneHalf+0.8-myHW,
  laneHalf+strip-0.3-myHW), timer 1.5 s. Meet test 2907 (old world): fire engine v truck 0 touched / 0 not past, least
  gap 1.49 m (base 5 touched, -0.42); two buses 0 / 0, least 1.18 m (base 7 touched -0.88, 1 not past).
- Harness on HEAD's world: AW/ts_tree61.sh -> SP/ts/tree61 + ts_world61.o; ts_build20.sh takes TREE=tree61
  WOBJ=ts_world61.o; ts_run.sh takes TS_TREE=tree61. Binaries traffic_sim2061b (base = HEAD AI), 2061x (s20), 2061h
  (s20h = s20 + item 4 try). Node 4870 and lane 7176 (edge 2907) are the same ids on HEAD's world.
- 4 I-38 (from watch w82: Packhorse 26 respawned as car 82): lane change 72->71 squeezed into 43.5 m at 26 m/s on an
  88.9 m link -> lag +1.1 m then overshoot -0.69 m left into the median barrier (STATIC t=217.3). Also a full 86 m change
  overshoots -0.56 m. Try s20h: chooseConnector on highway lanes needs room per shift = v_lim*3.3+12 (fallback pass to
  the town thresholds when no way on); need-lcMin 2.5 s for long vehicles >15 m/s; discretionary needs v*3.3+12.
  Harness metric added: "highway lane keeping, cars|long vehicles, in lane|changing lanes" (RMS, % over 0.5 m, max;
  median lane % over 0.3 m left, max left). Runs: AW/ts_hwy20.sh V (h_ramp_V_1..3, h_hwy_V); chain ts_hwychain.sh.

# AI agent: 16:10 SET 20 IN PROGRESS (lead's order: 1 stair pace cap, 2 three-point turns at tight dead-end circles on the
  driving side, 3 two long vehicles meeting on a narrow bend, 4 trucks drifting to the I-38 median, 5 population.cpp
  parked width +0.1; lower: lanes.cpp sidewalk corners if cheap). Set 19 goes into snapshot 61 (all four blobs). Anim
  c12-c14 land there too: build on animator.cpp d30f47a5 + character.h bd6d781f. Snapshot 61 also: the city's overlap fix
  and world model v0 (roads.cpp 120a1113 + src/world/roadnet.cpp; TS_ROADS_CPP needs roadnet.cpp beside it).
- Sources: AW/s20 (base = set 19 blobs + HEAD's ai_game.h / pedai / police / population). Exes deleted: nt_ai102c/103/104;
  keep nt_ai105 until snapshot 61 merges.
- 1 STAIR CAP (ai.cpp ai_pace::stairs + PedAI stairT/stairLook/stairUp + calls at all 12 AI movePed sites): probes the
  ground every 0.3 m from -0.3 to +0.9 m along the walk every 0.2 s; >= 2 steps of 10-60 cm = a flight (kerb = 1);
  cap 0.5 up / 0.55 down below 2.4 m/s asked, held 0.5 s. Test exes (bcb8a71 + set 19 + c14 + the anim agent's st20
  app.cpp = test-only locostairs): nt_s20b (base), nt_s20s (cap), nt_s20t (cap + TEST-ONLY "stairpace" LOG,
  AW/s20_ai_trace.cpp). locostairs: base up slide p90 2.43 sunk3 3.7% scuffs 58 / capped 0.45, 1.9%, 38 (anim ref at
  0.5: 0.52, 1.2%, 51); trips 14 -> 8; held walkers = the stair's + 2-3 at the station's other stairs. life on nt_s20t
  queued (false-positive check: stairpace lines should list only stair walkers).
- 2 K-TURN FIX (traffic_core control()): trigger only with full lock INTO the turn. --uturntest (new harness option):
  node 4870 all 38 models round (Sawgrass was the only fail); city 177 circles x 13 models: Sawgrass 221 -> 223/223,
  stuck 7 -> 0, strayed 9.6 -> 2.0 m. Big ones (wb >= 5 m) still ~4% not round (router keeps them out of dead ends:
  weight 0.001). A turnaround mode (fix 2) was tried and REVERTED (no gain: needs the bulb's paved extent).
- 3 MEETING (traffic_core plan()): meetTight (oncoming body leaving < 0.9 m, seen 3 s ahead) -> crawl 3 m/s on a bend /
  6 on the straight; meetWait (not in the bend yet, other in it or lower id) -> wait. --meettest EDGE A B (new):
  base 5/11 touched at 8-11 m/s; crawl 3 touched at <=3 m/s + standoffs?; chain AW/ts_chain20m.sh running (b / c / x).
- 5 population.cpp: boxHalf.x > shoulder*0.5 + 0.1 (was +0.5); harness spawnParked mirrors it (AW/s20/traffic_sim.cpp).
- Harness for set 20: AW/s20/traffic_sim.cpp (--uturntest, --meettest, parked rule); AW/ts_build20.sh SRC= V=.

# AI agent: 13:36 FOURTH RESTART (~13:31; HEAD still bcb8a71). Nothing of mine was running (set 19 signed off 10:12; queue
  empty, runner stopped 10:13, all chain markers present) -> nothing to rerun. Verified after the restart: the blobs
  f174c1a / 3babf61 / fce68d5 / 3bf2159 are in the object store; AW/s19 hashes to them; the shared tree's
  tools/traffic_sim.cpp is still 3bf2159; bin/nt_ai105.exe (+103/104/102c), the sign-off text, the table and all game logs
  are intact. Still HELD: the stair pace cap (waits for the lead's word on the animation agent's stair fix).

# AI agent: 10:15 SET 19 SIGNED OFF (v7) to main - read this first. Text: AW/signoff_set19_v7.txt (sent 10:12).
- Blobs on HEAD bcb8a71: traffic_core.cpp f174c1a, ai_core.h 3babf61, ai.cpp fce68d5 (sources AW/s19, list AW/graft105.txt);
  tools/traffic_sim.cpp 3bf2159 = the shared tree's copy (keep --overlaps / --walkcheck stable for the world engineer).
  The shared tree's AI files are still HEAD's (do not copy mine in until the lead / coordinator grafts them).
- Evidence: harness table SP/ts/set19_table.txt (base vs s19 = v7, 15 runs each); game logs SP/log_{soak,hwysoak}_nt_ai103,
  SP/log_{soak,life,chase}_nt_ai104, SP/log_soak_nt_ai105 (all PASS / no crash). Exes kept: bin/nt_ai105.exe (+ 103/104
  until the lead has read the sign-off; nt_ai102c for the animation agent).
- Nothing running: queue empty, runner stopped (queue_runner.pid = none), no harness chains.
- Set 20 candidates (in the sign-off): lanes.cpp sidewalk corner links across sharp bends; dead-end turning circles too
  tight for pickups (node 4870); two long vehicles meeting on a narrow bend (kerb nudge 0.2 m); trucks drifting toward the
  I-38 median at speed. For others: roads.cpp overlaps (world engineer), I-38 median width, population.cpp parked width.
- HELD (unchanged): the stair pace cap waits for the lead's word that the animation agent's slow-pace stair fix landed.

# AI agent: 10:00 v7 HARNESS COMPLETE (SP/ts/set19_table.txt = base vs s19=v7; full SP/ts/set19_table_full.txt). v7 is
  identical to v6 everywhere except Sol Beach (the stop-sign hand-over never arises elsewhere). chase nt_ai104 done
  (no crash; boxed at t=44, player down t=94 -> hospital). nt_ai105 BUILT 09:19 (bcb8a71 + graft105 exactly; -Wall 0
  warnings); its soak RUNNING (clean to t=200, same route as nt_ai104). Sign-off text final except [SOAK105]:
  AW/signoff_set19_v7.txt -> send to main when soak105 ends, then hand back.
- Ramps' 3 long waits: 5040 van held through a green by a full exit (car 20 m past the box; watch SP/ts/w53_ramp_s19y_1.txt),
  5038 one-lane approach (12 s green / 51 s). Stuck: ramp spawn artifact, Midtown/Beach at overlaps, Luna turning circle.

# AI agent: 09:20 life nt_ai104 PASS (census as nt_ai102c: nobody hurt / in the water, no panic / near misses / crash
  scenes). chase on nt_ai104 RUNNING (the runner took it before my drop; keep it); nt_ai105 build compiling; queue after
  chase104: nt_ai105 soak only (chase105 dropped). Chain u running (v7 everything).
- v7 beach seed 1 hard hit (98748 Ns, t=359) = a fire engine (respawned at t=346) and a Packhorse 26 meeting at an S-bend
  of edge 2907 (2 x 3.2 m street, 5340 243): both on their lanes (latErr <0.2), kerb nudge only 0.2 m, the bodies swing
  over at the bend; the truck lost it from its corridor at 30 m. meetBig = HEAD's (+ parked exclusion) -> pre-existing
  class (base seed 2: Tanuki vs Boulevard 24715 Ns, opposite lanes of e3176). Set 20 item. Watches: SP/ts/w17_*, w134_*.
- Sign-off draft for v7: AW/signoff_set19_v7.txt ([WALL] [TABLE] [TABLENOTES] [SOAK105] [CHASE104]);
  table: python3 AW/ts_table.py --signoff (base vs s19 = v7).

# AI agent: 09:02 v7 (fixes a v6 regression found at Sol Beach). v6's in-box look-ahead held a car for good at an all-way
  stop whose line is at the box exit of the junction before (nodes 2546 signal / 2544 stop, 11 m apart, 1.1 m link,
  part of the 35th St / Solano Causeway overlap): 127 waits>60 vs base 35. v7 = v6 + preStop/preGo (Driver) - the stop
  and the stop's turn order are made from the box (stopGrant from the box, queue sees such cars), gate() takes over on
  the lane. Beach v7: 24+6 = 30 deadlocks (base 35, v6 127), all at the overlap cluster; boulevard 16-17 km/h (base 9-10).
- Blobs v7: traffic_core f174c1a, ai_core.h 3babf61, ai.cpp fce68d5 (AW/graft105.txt). Backups of v6: AW/s19v6_*.
- bin/nt_ai105.exe building (PID AW/build105.pid, AW/runner_after_105.sh restarts the runner). Queue: nt_ai105 soak, chase
  (nt_ai104 chase dropped; nt_ai104 life running).
- Chain u (restart-safe, marker SP/ts/chain19u.done): watch of v7 beach car 17 (fire engine head-on at t=359), then all
  v7 runs (ts_table.py has a v7 column = s19y). Relaunch: AW/ai_detach.sh AW/ts_chain19u.out bash AW/ts_chain19u.sh
- Calle Luna v6 "3 stuck" = 1 car: Sawgrass (wb 3.07, lock 0.60 -> r 4.5 m) at the dead-end circle of node 4870 (r 5.0,
  entry bends 4.3, lanes.cpp R = clamp(|offset|+3.4, 4.8, 6.5)); its k-turns walked it 10 m off, path jumped to lane
  18223 u 0, then into the marina (lifted). control() unchanged from HEAD -> pre-existing; set 20 (circle size / k-turn).
- I-38 gore: median barrier faces 0.16-0.37 m outside the fast lanes' edges (lanes 68/71 4.7 m apart, 0.6 m barrier,
  deck edge 18 shoulder 0): a truck 0.3-0.7 m left of centre touches it (pre-existing, base too).

# AI agent: 08:30 nt_ai104 (v6) SOAK PASS - identical to nt_ai103's (deterministic route; no margin case on it): clean all 300
  s. life running on nt_ai104 (then chase). Chain s on the beach/district runs; v6 table so far in SP/ts/set19_table.txt
  (Midtown v6: all collisions/waits at the overlap; the one hard hit = 2 straight connectors on top of each other at node
  283 = overlap geometry). Sign-off text ready: AW/signoff_set19_send.txt (fill [TABLE] [I38] [SOAK104] [LIFE] [CHASE]).

# AI agent: 07:35 nt_ai103 HWYSOAK PASS (v5; log SP/log_hwysoak_nt_ai103.txt): traffic health clean all 300 s (no stuck /
  blocked / waits > 90, core red 0, impacts 0), the player's car a long city drive, unsticks 0.
- v6 parked Midtown (p_mid_s19x): stuck 0 (v5 3), red 0, parked waits 0, deadlocks all overlap; v6 ramps/downtown parked clean.
- nt_ai104 (v6, bcb8a71 + graft104) BUILT 07:28; its soak RUNNING since 07:33, then life, chase (runner PID 905).
- Chain s (PID 1707) continuing: p_mid_s19x_2, q_* seeds, beach, districts, i38 probe.

# AI agent: 07:40 v6 = v5 + FIXED 0.25 m corridor margin for still parked cars (BF_PARKED, speed<0.3). Cause of the 3636 2851
  "stuck": NOT the pass (nudge 0) - a Sawgrass (hw 1.02) by a Parcel P70 (hw 1.42) parked 2.75 m off an avenue lane centre:
  clearance 0.31 = right at the speed-dependent margin 0.3+0.015v -> inside at 0.5 m/s, clear at 0.1 -> brake/pull away
  twice a second (limit cycle) -> stuck detection, reverse, lift. Fixed margin removes the cycle.
- traffic_core blob v6 2ba6cdc (graft104.txt: + ai_core.h 3e7f9ad, ai.cpp fce68d5) -> bin/nt_ai104.exe building on bcb8a71
  (PID AW/build104.pid; AW/runner_after_104.sh restarts the runner). Queue: nt_ai104 soak, life, chase (after nt_ai103
  hwysoak, running now - v5).
- Harness: chain s (PID 1707, restart-safe, marker chain19s.done) = v6 build (traffic_sim19x) + full list; ts_table.py now
  reads the s19x outputs (column v6). v5 chains r/q killed (their outputs stay for reference).

# AI agent: 07:10 v5 parked: ramps waits 0 / dead 0 / stuck 0 (static 3 = I-38 gore barrier, base too); Midtown waits 3->0,
  red 16->1, deadlocks = overlap jam. BUT cars stuck at 3636 2851 (lane 24167 u~64, throttle on, nothing within 4.5 m) in
  BOTH v3 (car 110) and v5 (car 71, t=313-325) parked Midtown runs, never in base -> pass-related? Chain r (PID 31507)
  watches v5 car 71 first (SP/ts/w71_mid_s19w.txt), then reruns chain q (skips done runs; marker chain19q.done).

# AI agent: 06:45 nt_ai103 SOAK PASS (log SP/log_soak_nt_ai103.txt, shots /tmp/ai/soak_nt_ai103, PNG SP/ts/soak103_0{4,5}.png):
  300 s, traffic health clean all through (stuck/blocked/wait>90 all 0, core red 0, stuckEv 0, deadlockBreaks 0, kturns 1,
  reloc 1), max wait 36 s; the player's car took the same I-38 on-ramp at t=140 and was on the highway at 34.5 m/s by t=160
  (nt_ai102c: stuck 150 s there; car 20 145 s behind the Compactor). unsticks 0.
- Game queue now: hwysoak, life, chase (runner PID 905). Harness chain q continuing (v5 ramps: red 1 / dead 3 (long reds) /
  coll 0 / hard 0 over 3 seeds; Midtown collisions + deadlocks all at the overlap site).

# AI agent: 06:00 nt_ai103 soak RUNNING (started 05:47; log SP/log_soak_nt_ai103.txt at the end). v5 harness built 05:47
  (traffic_sim19w); chain q (PID 1002) running but memory-blocked by 3 games. hwysoak/life/chase HELD in
  AW/queue_held103.txt so the harness can run after the soak (re-add with qedit add + restart the runner if it idled out).
- AW/ts_table.py = the sign-off's harness table (base / v2 / v5 per scenario).

# AI agent: 05:25 THIRD RESTART (~05:15). HEAD bcb8a71 (snapshot 60: world format + validator; my files unchanged).
- bin/nt_ai103.exe WAS BUILT 05:00 (837b39c + graft103 = v5: traffic_core 12fe477 has toHold + edgeOneWay). Its soak was
  killed at 05:15 -> requeued at the top. Queue: nt_ai103 soak, hwysoak, life, chase; runner PID AW/queue_runner.pid.
- SP/ts/traffic_sim19w (04:20) is v4 (no room fix) - chain q rebuilds v5 (log AW/ts_build19w5.out).
- RESTART-SAFE CHAIN: AW/ts_chain19q.sh (PID 1002, marker SP/ts/chain19q.done) skips runs whose output has rc=0 - after a
  restart just relaunch it: AW/ai_detach.sh AW/ts_chain19q.out bash AW/ts_chain19q.sh. Game runs: re-add the killed line
  (qedit.py top) and relaunch the runner (AW/ai_detach.sh /dev/null bash -c "exec AW/ai_queue.sh >> AW/run_queue.log 2>&1").

# AI agent: 05:05 v3 parked results: ramp waits>30s 2->0 dead 19->0; Midtown parked waits 3->0 (deadlocks now the overlap
  jam); downtown 5->0. v3 Midtown: car 110 stuck x3 at 3636 2851 (nothing ahead, throttle on) -> chain o (PID 14328)
  watches it after chain n. Chain p (PID 22766): I-38 gore barrier probe (pre-existing hits at 2480-2492, 3668-3776).
- Memory very tight (3 games of others + compiles): v5 harness build and nt_ai103 game build waiting at gates.
- Sign-off draft: AW/signoff_set19_draft.txt (fill TESTS / EVIDENCE / blobs).

# AI agent: 04:35 s19 v5 + GAME EXE BUILDING
- Harness HEAD 837b39c base rebuilt OK (shared traffic_sim.cpp compiles on HEAD); overlaps4.txt == 31 pairs.
- w62 watch: car 62 was NOT slowed by the s19 preview (13 m/s through the box). Cause = gate(): a permissive left (bus 118)
  creeping to its hold point on green was COMMITTED by crossing its stop line (-> FREE, accelerated), advancePath reset
  committed in the box, the box's check then stopped it in car 62's way. FIX v4: toHold (permissive left/U, holdS>0, green)
  skips the line commit; amber/red over the line commit as before.
- v3 parked ramp (p_ramp_s19v): waits >30 s behind parked 2 -> 0, deadlocks 19 -> 0, red 7 -> 0, avenue 10.8 -> 15.5 km/h.
  Static impacts 2 -> 5 = pre-existing I-38 barrier hits (LATDEV ~0.9 m at 30 m/s, base has them at 2480,3680 too).
- FIX v5: pass room - no shoulder left of a divided road's leftmost lane (median/barrier); left shoulder only on one-way
  edges (lanesF/B == 0); right hard shoulder on highway/ramp lanes.
- AW/s19 = v5: graft AW/graft103.txt (traffic_core 12fe477, ai_core.h 3e7f9ad, ai.cpp fce68d5) on 837b39c ->
  bin/nt_ai103.exe building (AW/ai_buildgraft2.sh: archives world/ too - build.sh needs it since 837b39c; PID
  AW/build103.pid, log AW/build_nt_ai103.out). Queue: nt_ai103 soak, hwysoak, life, chase (runner PID AW/queue_runner.pid;
  AW/runner_after_103.sh restarts it after the build).
- Harness: chain m (v3) stops after p_dt1_s19v; chain n (PID 13760, marker chain19n.done) builds v5 = traffic_sim19w and runs
  q_*_s19w seeds, p_*_s19w, p_mid_{base,s19w}_2, q_beach_{base,s19w}_{1,2}, r_{dt1,luna,hwy}_s19w.
- Findings for the sign-off (not fixed): population.cpp parks 2.84-3.0 m wide vans/trucks in 2.4 m strips (half width <=
  shoulder/2 + 0.5); lanes.cpp corner links cut across the inside of sharp bends (Key Lime Blvd 2984,-2049; 234 corner
  links / 1.7 km non-overlap) -> next set; walk path 56305 through building 35588 (campus, 2041,3820).

# AI agent: 03:45 SECOND CONTAINER RESTART (~03:37; disk intact). HEAD = 837b39c (PR #52: updater + --world=hand; app.cpp
  changed, my AI files unchanged: traffic_core/ai/ai_core = HEAD blobs). Coordinator: snapshot 60 (world format + validator,
  no AI files) starts within the hour -> its flag holds my gates.
- v2 traffic_core.cpp preserved as AW/s19v2_traffic_core.cpp (= SP/ts/tree19/src/game/traffic_core.cpp; binary
  SP/ts/traffic_sim19 = v2 + parked harness, 02:55). AW/s19 = v3 (pass logic) + ai_core.h (pass fields) + ai.cpp (diag).
- RUNNING: AW/ts_chain19m.sh (PID 1199, log AW/ts_chain19m.out, marker SP/ts/chain19m.done): base rebuilt from HEAD (all),
  overlaps4.txt (must = 31), traffic_sim19v build, w62 watch (v2), p_*_s19v, q_*_s19v seeds 1-3, p_{mid,ramp}_{base,s19v}_2.

# AI agent: 03:45 answered the tools engineer's worldv question (via main): LaneGraph fields for lanes/connectors/approaches/
  phases/walk graph/mapping/counts + gaps (no kerb flag, no box polygon, phases per axis, no kerb/building side, PATH links
  unmapped). Done - nothing more owed there.
- Found (pre-existing, gate()): a permissive left turner creeping to its hold point gets "committed" as its front crosses
  the stop line -> FREE -> accelerates; advancePath resets committed on entering the box -> in-box hold logic stops it with
  its nose in the oncoming lane (q_ramp_s19b_1 t=198.9: bus 118 hit by car 62, imp 17717; base runs have no hard collisions).
  Watching car 62 (chain l, PID 21354) to see whether the s19 in-box preview slowed it in the box.

# AI agent: 03:20 PARKED-CAR BLOCKS FOUND + FIX IN TEST (s19 v3)
- --overlaps (strict, shared tree) verified: SP/ts/overlaps3.txt == overlaps.txt (31 pairs). Wider-test extras (not gores, FYI):
  SP/ts/overlaps_wide_extra.txt (14: Bayshore Blvd vs 9th/10th/11th Ave etc.).
- Harness --parked (chain j, base): wide parked vans/trucks (Parcel P70 box half width 1.42, Compactor 1.50) in the 2.4 m strip
  stick ~0.5 m into the lane; cars stop (corridor margin 0.3) and NEVER get round: the overtake's nudge leaves the near
  corridor at the car's actual lane-parallel position, so the blocker never leaves it (stalemate); near a lane end (toEnd<30)
  no overtake at all; a lane change stuck behind one never progresses. p_mid_base: 3 vans -> 14 deadlocks (queues of 8).
  population.cpp (NOT mine) admits half width <= shoulder/2 + 0.5 = 1.7 -> suggest to the coordinator.
- FIX (AW/s19/traffic_core.cpp + AW/s19/ai_core.h: Driver::passHost/passT/passBacks): pass logic replaces the overtake: side
  step = clear the blocker by 0.3 m on its left/right, limited by room (lane beside / oncoming lane of a two-way road without
  median / one-way: shoulder only / kerb side: own lane); in-lane squeezes allowed near lane ends; lane borrow needs toEnd>30 +
  the old clear check; plan() judges the passHost body by our swept box (look 0.5-2.5 m, +0.08) instead of the corridor, speed
  cap 3 m/s while passing; back up 3 m (kturn-style, path kept) when stopped on it >1.2 s, max 2; nudge kept until it is behind
  us; a stuck lane change is taken over. meetBig ignores BF_PARKED (a parked truck facing us pushed the kerb nudge onto our
  parked cars). One-lane ramps: no more overtake into the barrier (the soak's wheelspin "show" at the ramp).
- ai.cpp (s19): waitText prints "(driving round car N, k back-ups)".
- RUNNING: chain j (PID 6891: p_dt1_base, w_ramp_s19b_1 watch of the q_ramp_s19b_1 hard collision t=198.9 cars 62/118 node
  5038) then chain k (PID 16656: build traffic_sim19v = v3, p_*_s19v, q_*_s19v seeds, p_*_{base,s19v}_2).
- NEXT: game exe nt_ai103 = f595703 + AW/s19 (traffic_core, ai_core.h, ai.cpp) once chain k looks right; soak + hwysoak + life.

# AI agent: 02:45 COORDINATOR: the world engineer fixes the 31 overlaps in roads.cpp (general pass, lower class gives way)
  and reruns my --overlaps from the SHARED TREE (read-only) as its before/after check -> keep --overlaps / --walkcheck STABLE
  there; no AI workarounds for overlap deadlocks; lists still go in the set 19 sign-off; the red-run fix is mine.
- Done: --overlaps restored to the strict criterion (centreline within 0.8 x max half width, edgesInRect +12) = the one behind
  SP/ts/overlaps.txt (31 pairs) - VERIFY it reproduces 31 after the next harness rebuild. (The 02:06 "wider" version gave 132
  pairs incl. ~70 normal highway/ramp gores: SP/ts/overlaps2.txt.) Ack sent to main.
- Shared tree traffic_sim.cpp also has --parked N (sim option: parked cars as population.cpp lays them out; PARKBLOCK log;
  "parked cars: N, waits over 30 s behind one"), syntax-checked. Further harness edits: in a copy first, never break it.
- AW/s19 now also holds ai.cpp (health line: waitText/queueText - head of the queue, the player's own car when AI-driven and
  held, a parked obstacle's offset across the path). Kept OUT of the shared tree (others build from it).
- Harness v2 (3 seeds): ramp red 18 -> 1, deadlocks 34 -> 1 (node 5040 box standoff); Midtown red 28 -> 0, deadlocks all at
  overlap nodes (68 -> 52). AW/ts_dead.py splits deadlocks by overlap nodes.

# AI agent: 02:30 RESUMED after the ~02:19 container restart (new process; disk intact). HEAD = main = f595703 (set 18 merged;
  my AI files in the shared tree = HEAD; tools/traffic_sim.cpp = my --overlaps (wider: carriageways overlapping >= 1 m) +
  --walkcheck + --seed WIP).
- Found on disk: chain e done (r_*_s19 v1 runs, walkcheck_all.txt); chain f done up to the q_* seeds (base vs s19 v1: red runs
  4-10 -> 0-1 per run; deadlocks noisy, mostly at the Midtown overlap nodes 281-288); r_dt1_base cut off; chains g/h never ran.
  AW/s19/traffic_core.cpp = v2 (stopsAhead + in-box preview + box mutual blocks up to 6 m: node 5040).
- ai_memgate.sh now also waits while /tmp/neontide_lead_wants is fresh (< 5 min).
- RUNNING: AW/ts_chain19i.sh (PID 4380, log AW/ts_chain19i.out, marker SP/ts/chain19i.done): base + s19 v2 rebuild,
  overlaps2.txt, q_*_s19b seeds, r_{dt1,luna,hwy,beach}_{base,s19b}, walkmaps.
- Soak (nt_ai102c) notes: the player's car stuck on the on-ramp at 2465 3880 behind a stopped wagon + yellow car (a ped on the
  ramp in shot 05; skid marks; "showing off" at t=158 = wheelspin) - cause still open. Compactor = population parked-car
  spawner most likely (kerb strip, wide truck inside the lane's corridor: the parked exemption needs the inner edge beyond
  laneHalf - 0.15) - car 20's blockedTime fell while stuck (overtake never fired).

# AI agent: 01:55 SET 19 - findings and the fix in progress (HEAD now 2b3102e = set 18 committed; my AI files = HEAD)
- --overlaps: 31 overlapping edge pairs >= 12 m (23 acute Y-splits at a shared node, 6 parallel, 2 duplicates; list SP/ts/overlaps.txt).
  Midtown's 37 deadlocks = Bayshore Blvd / 9th Ave overlap (nodes 281-288); Beach's 18 = node 2478. roads.cpp (not mine) - the
  coordinator decides who fixes them; send the list WITH set 19.
- --walkcheck: sidewalks on carriageways 482 links (6.9 km) + corners 323 (2.4 km) - top ones are the overlaps; through buildings
  only 1 path link. Full listing coming (SP/ts/walkcheck_all.txt).
- RED RUNS (mine, traffic_core.cpp): a car through junction A on green met B's red only on the short link (6-15 m at 8-15 m/s) ->
  ran it or stopped on the crosswalk (node 306 by the I-38 ramps: 10 in 5 min; nodes 272/274: 3). FIX in AW/s19/traffic_core.cpp:
  stopsAhead() + preview of the next junction from inside the box; from the lane, a link too short to hold the car -> wait at
  A's line (signals only, < 30 s wait). Harness before/after: AW/ts_chain19e.sh (base tree rebuilt from HEAD, overlay build
  AW/ts_build19.sh -> SP/ts/traffic_sim19; outputs SP/ts/r_*_{base,s19}.txt; marker chain19e.done).
- Soak (nt_ai102c): the player's car stuck 150 s on the I-38 ramp at 2465 3880 (cars ahead stopped; cause unknown - harness has
  no deadlock there); a "Compactor parked empty" blocked car 20 at 2471 3752 (lane 19795, single lane) 145 s - cause unknown.
- Queue held: AW/queue_held19.txt (night, chase, panic on nt_ai102c) - restore with qedit add after the harness work.

# AI agent: 01:15 SET 19 IN PROGRESS
- 12.7 reply sent (all 13 agreed; 4 validator checks + 4 notes passed to the city agent for doc v1.1). The coordinator wants the
  --overlaps list for today's world WITH set 19 (then decides who fixes roads.cpp).
- Harness findings so far: Sol Beach 18 deadlocks all at node 2478 (5050, 490): edge 3287 (boulevard, 3 lanes) and edge 3284
  (street) overlap ~60 m (roads.cpp bug). Midtown 37 deadlocks + 5 red runs: verbose run pending. Luna 1 red. Downtown, highway clean.
- tools/traffic_sim.cpp (mine, shared tree): new --overlaps and --walkcheck (walk links through buildings / on carriageways);
  near/far identifiers renamed (nbE, farD). Copied into SP/ts/tree; AI-part rebuild behind the gate (AW/ts_build.sh ai, PID 8540,
  output AW/ts_build_ai.out). Chains (one harness job at a time): AW/ts_chain19.sh (overlaps -> SP/ts/overlaps.txt, Midtown -v ->
  SP/ts/sim_mid_v.txt; marker SP/ts/chain19.done) then AW/ts_chain19b.sh (--walkcheck -> SP/ts/walkcheck.txt; chain19b.done).
- In game: queue on nt_ai102c (soak running, then night, chase, panic); runner PID AW/queue_runner.pid.

# AI agent: SET 19 STARTED 2026-10-19 ~23:40 UTC (the coordinator: pick the most visible remaining problems in the procedural
  world from my own observations; host-side checks first; snapshot-safe blobs; or park with a note if nothing is worth a set)
- Host side: the native traffic harness (tools/traffic_sim.cpp) building from a clean HEAD archive into SP/ts/ (AW/ts_build.sh,
  PID AW/ts_build.pid, output AW/ts_build.out); then --sim runs for collisions / static impacts / deadlocks / red runs / ped hits.
- In game (observation): nt_ai102c (= HEAD + set 18) life, soak, night queued (runner PID AW/queue_runner.pid).
- Then: pick the top problems, fix, test, sign off as set 19.

# AI agent: HANDED BACK 2026-10-19 ~23:35 UTC - read this first
- Set 17 committed (snapshot 54, HEAD 1a83e24); set 18 accepted (joins snapshot 57; the coordinator grafts my blobs into the
  shared tree after 57 - until then the shared tree's AI files = HEAD's). Requirements for the hand-built world sent.
- Open item (held, on the coordinator's word): the stair pace cap (walkers / police on foot / followers on a flight, one tread
  per step ~0.5 m/s up, 0.55 down) once the animator's slow-pace fix lands. Detect "on a flight" by a ground probe along the
  walk (risers) or the animator's stairSeen (character.h) for near peds; cap in ai_turn-style at the movePed callers.
- Nothing running: queue empty, runner stopped (queue_runner.pid = none). Kept exe: bin/nt_ai102c.exe (follower-flip count).
- No new AI content for the generated city (the user's direction change to a hand-built world).

# AI agent: 23:45 SET 18 SIGNED OFF + ACCEPTED (joins snapshot 57; the coordinator grafts the blobs into the shared tree after
  57 commits - do NOT touch the shared tree's AI files meanwhile). Blobs (on HEAD 0c288db = 1a83e24 for my files): ai.cpp 35f1bc0,
  ai_game.h 0519098, app.cpp 6f3ee5d, pedai.cpp 2de065e, police.cpp 1b1a28b (sources AW/s18; text AW/signoff_set18_send.txt,
  results AW/set18_results.txt). Kept: bin/nt_ai102c.exe (the animation agent may count follower flips on it). nt_ai102b deleted.
- HAND-BUILT WORLD REQUIREMENTS SENT (AW/handbuilt_reqs.txt; for the city agent's docs/HANDBUILT_WORLD.md sections 12 and 4.4;
  its draft is in SP/hand0/docs/). No new AI content for the generated city from now on.
- HELD: stair pace cap for walkers / police on foot / followers on a flight (one tread per step: ~0.5 m/s up, 0.55 down) - wait
  for the coordinator's word that the animator's slow-pace fix has landed.
- Nothing of mine running (the queue runner idles out).
# AI agent: set 18 extras in AW/s18 (not yet built into any exe): ai_turn ease caps creeping/standing turns too (4 rad/s on the
  spot; nt_ai102c HAS it), and app.cpp parking mode `if (!Platform::argValue("time")) env.timeOfDay = 10.5f;` (AW/parkingtime.py,
  the coordinator's request for the night agent; NOT in nt_ai102c - test-mode only). The autoupdater's run() hook: the coordinator
  3-way merges it at snapshot time (no action needed).
# AI agent: 21:40 SET 17 COMMITTED as snapshot 54 = HEAD 1a83e24 (my five files there = the signed blobs). Set 17 exes deleted.
  Shared tree = HEAD for my files (set 18 lives in AW/s18 until its sign-off; the autoupdater adds a hook line to app.cpp's
  run() - rebase the test modes onto HEAD's app.cpp at the sign-off). DIRECTION CHANGE (user, via the coordinator): a hand-built
  world replaces the procedural one; after set 18's sign-off, NO new AI content for the generated city - write the AI's data
  requirements for the new world (draft AW/handbuilt_reqs.txt) and send them to the coordinator (for the city agent's
  docs/HANDBUILT_WORLD.md).
# AI agent: 19:46 SET 17 ai.cpp RE-SIGNED (the coordinator's review fix): ai_board::step `ok` also fails when boardThen 1 + a
  passenger seat and the wheel is taken meanwhile -> ai.cpp 6dd49d0 (was 0c0566b), syntax clean on 731a731 + AW/graft101e.txt.
  Set 17 blobs now: ai.cpp 6dd49d0, ai_game.h e3f9263, app.cpp 17333c6, events.cpp e8db7b4, pedai.cpp b4dff69 (= the shared
  tree). s18/ai.cpp has the fix too (35f1bc0); nt_ai102b predates it (runs go ahead; rebuild before set 18's sign-off).
# AI agent: 19:30 SET 17 SIGNED OFF (sent to main; text AW/signoff_set17_send.txt, results AW/set17_results.txt)
- Blobs on HEAD 731a731 (= b71d339 = 0d67933 for my files): ai.cpp 0c0566b, ai_game.h e3f9263, app.cpp 17333c6, events.cpp e8db7b4,
  pedai.cpp b4dff69. The shared tree = exactly these: DO NOT touch it until the lead has integrated set 17 (set 18 lives in AW/s18).
- Keep bin/nt_ai101.exe, nt_ai101c.exe, nt_ai101d.exe until set 17 is accepted, then delete them.
- Set 18 (AW/s18 -> AW/graft102b.txt): change 2 + change 3 + followfix + gotofollow/turntest; nt_ai102b building (PID
  AW/build102b.pid; AW/runner_after_102b.sh restarts the runner after it). Queue: the nt_ai102b runs (gotofollow, turntest, knock,
  crossing, crowded, copreturn, walkby). Baselines: SP/log_crossing_nt_ai_sep3.txt, log_crowded_nt_ai_sep3.txt,
  log_walkby_nt_ai_sep3.txt, log_knock_nt_ai101.txt, log_copreturn_nt_ai101.txt.

# AI agent: RESUMED 2026-10-19 13:35 UTC (after the usage pause and a reboot; disk intact)
- HEAD 0d67933 (snapshots 49-51 on PR #47; none touched my files since 48); main af8a066 (v0.02.6). My WIP = HEAD + set 17 in ai.cpp,
  ai_game.h, app.cpp, events.cpp, pedai.cpp (police/traffic/traffic_core = HEAD).
- Set 17 rebuild: AW/graft101.txt (mkgraft on 0d67933): ai.cpp 0c0566b, ai_game.h e3f9263, app.cpp f4ee4d4, events.cpp f46b56b,
  pedai.cpp b4dff69. nt_ai101 building (PID AW/build101.pid); AW/runner_after_101.sh starts the runner after it.
- Queue (AW/queue_set17.txt): nt_ai101 knock, trafficstop, crashscene, events, hitped, nearmiss, nightwalk, breakdown, copreturn.
- After set 17's sign-off: the hitped and nearmiss fixes, then change 2 (BRAIN_GOTO easing behind slower walkers).
- A lead showcase renders now: memory gate, one game at a time.
- 16:20 HEAD is b71d339 (PR #47 merged; only src/anim/animator.cpp changed since 0d67933 - my files equal 0d67933's).
  nearmiss on nt_ai101: TEST BUG - the lane pick (no highway/ramp/bridge, kerb lane, 150/115 m run) found no lane, so the player
  stayed on foot at the spawn (1784 160): no car, no kerb peds. Run stopped. Fixing the pick in app.cpp -> nt_ai101c (replaces the
  waiting nt_ai101b build; graft101c = graft101b + the new app.cpp): events, knock, nearmiss on it.
- QUEUED by the coordinator (after set 17 + the hitped/nearmiss fixes): pedai turnTo turns at 6 rad/s even at a full walk - cap the
  turn rate by speed (~2-3 rad/s at a full walk, faster slow/standing) or slow the walk through sharp turns; path following,
  crossings and avoidance must hold. The animation agent has a turn harness (turnwalk 90/135/180) + the locotour for before/after
  numbers - ask it through the coordinator. Plan: change 3, built with change 2 (gotofollow) in one exe.
- 16:46 nt_ai101c (b71d339 + AW/graft101c.txt: set 17 with app.cpp 6f094c2 = the nearmiss fix) compiling since 16:42 (PID
  AW/build101c.pid). AW/after101c.sh (PID in its .out) then builds nt_ai102 = b71d339 + AW/graft102.txt: set 17 + change 2
  (AW/change2.py: ai.cpp ai_pace::behind) + change 3 (AW/change3.py: ai_game.h ai_turn rate/follow/ease, used in pedai.cpp's
  final turn, ai.cpp BRAIN_FOLLOW/GOTO, police.cpp's 9 on-foot steps) + test modes gotofollow (AW/gotofollow.py) and turntest
  (AW/turntest.py); sources in AW/s18/. The shared tree still = set 17 exactly (copy AW/s18 in after the set 17 sign-off).
- Queue: nt_ai101 nightwalk (running/gated), breakdown, copreturn; nt_ai101c nearmiss, events, knock; nt_ai102 gotofollow,
  turntest, knock, crossing, crowded, copreturn, walkby. Sign-off texts: AW/signoff_set17_head.txt + AW/signoff_set17_body.txt.
- 18:30 Set 17 results so far (AW/set17_results.txt): nightwalk, breakdown, copreturn PASS (nt_ai101). nearmiss on nt_ai101c: the car
  was placed but a pickup queued at the light held it up -> second fix (AW/nearfix2.py, app.cpp 17333c6 = the shared tree now):
  nt_ai101d (graft101d) building (PID AW/build101d.pid), nearmiss on it at the queue's top. nt_ai101c events running, then knock.
  SET 17 BLOBS NOW: ai.cpp 0c0566b, ai_game.h e3f9263, app.cpp 17333c6, events.cpp e8db7b4, pedai.cpp b4dff69.
  AW/s18 (set 18 = changes 2+3 sources) has nearfix2 too, and a gotofollow comment fix: its app.cpp differs from nt_ai102's
  c99d2b1 (rebuild nt_ai102 before signing set 18 off).
- 18:45 The animation agent (via the coordinator): change 3's profile is FINE on its side (turnwalk 90/135/180 slides comparable to
  today's). NEW REQUEST folded into set 18: follower stance hysteresis (pedai ped flips 0<->7 "leader stopped: chat" / "chatting on
  the way" frame to frame) -> AW/followfix.py (PedAI::followMode/followModeT; enter/leave marks + 0.8 s hold), applied to AW/s18.
  nt_ai102 deleted (never run); nt_ai102b = b71d339 + AW/graft102b.txt (s18: set 17 + change 2 + change 3 + followfix + gotofollow
  + turntest) builds after nt_ai101d (AW/after101d.sh). Queue: nt_ai101d nearmiss, nt_ai101c knock, then the nt_ai102b runs.

# AI agent: PARKED 2026-10-13 ~09:10 UTC (wind-down: the user paused for usage) - read this first

## Where things stand
- Snapshot 47 COMMITTED = HEAD ab5995c: traffic.cpp ef101801 (the taxi waits through the boarding clip), events.cpp ff4a3f8d
  (calmActor accepts PS_EXITING: traffic stops and real fender benders no longer end when the actors step out).
- Snapshot 48 SIGNED OFF and ACCEPTED (running as tree 925a2974 with the quick-step fix and faces batch 9; text AW/signoff_s48.txt;
  results AW/taxi_results.txt), all on ab5995c's versions:
  - traffic_core.cpp 8efeb4a4 (the cab keeps to its lane and stops beside the hailer: no kerb nudge / clamp with a host stop point)
  - traffic.cpp a8bf23c5 (the drop-off waits for the fare's exit clip: VR_TAXI task 2, at most 6 s)
  - ai.cpp 0f8e00bb (ai_board::claimed / openSeat), pedai.cpp 98de395d (claims at every boarding seat choice), police.cpp 1eb24c98
    (claims + FT_RETURN: near enough within 1.2 m of the bodywork; the door walk holds the car; no "wait by the car" once on the door walk)
  - tested on bin/nt_ai100d.exe (ab5995c + those five + test app eb0df14): copreturn PASS (2 rounds), crashscene PASS; taxi on
    nt_ai100b (drop-off hold) and nt_ai99b (stop at the hailer, pull-overs as before). Delete nt_ai100d.exe once 48 commits.
- NOTHING RUNNING: queue runner stopped, queue empty, all helper chains finished or stopped, my wine prefix (/tmp/wine_ai) stopped.

## Set 17 (parked, WIP in the main tree - left as is)
- Files (main tree, vs HEAD ab5995c): ai.cpp, ai_game.h, app.cpp, events.cpp, pedai.cpp, police.cpp, traffic.cpp, traffic_core.cpp.
  police.cpp / traffic.cpp / traffic_core.cpp are byte-identical to the 48 blobs (1eb24c9, a8bf23c, 8efeb4a); once 48 is in, set 17's
  own diff is ai.cpp, ai_game.h, app.cpp, events.cpp, pedai.cpp (all carry the 48 seat-claim edits too, ported).
- What set 17 is: the events' people back into their cars through the door (ai_board::beginThen(g,id,veh,seat,then): 1 = away at
  the wheel, 2 = nothing; crash drivers, the breakdown driver, the traffic-stop officer), the boxed-in crash driver in by the
  passenger door and across to the wheel (ai_board::step slide-over), aiCarEndFree split out of aiCarEndToWalkRound (box centre),
  crash walk-back corners off the box centre (+0.2 past-the-end threshold), the crash status line with each driver's place off
  its box; pedai.cpp: the one hit waits for the sorry driver (PedAI waitDrv/waitDrvUid/waitT), the sorry driver's linger only within
  4 m or after the word, chatT retry for overheard lines; app.cpp test modes: hitped/nearmiss fixes, taxi (v3 + short ride and
  drop-off: AW/taxitest.py, taxifix2/3.py, taxidrop.py), trafficstop/breakdown (evdoortest.py), knock (knocktest.py), copreturn
  (copreturn.py + copreturn2.py). Plus calmActor (= 47) and the seat-claim slide-over check.
- Tested so far: crashscene PASS (nt_ai92), breakdown PASS (nt_ai95); events (nt_ai92) found the boxed-in case -> the slide-over
  (NOT yet seen in a run); trafficstop and knock passed on HEAD + fixes (nt_ai96) but the set 17 versions (door entries, ST_C
  walk-round, slide-over) are not yet run; hitped / nearmiss / nightwalk on set 17 not yet run. No set 17 exe is left (nt_ai97 deleted:
  it predates the seat claims).
- NEXT (when resumed): once 48 commits, on the new HEAD: bash AW/mkgraft.sh <HEAD> AW/graft101.txt; AW/ai_detach.sh
  AW/build_nt_ai101.out bash AW/ai_buildgraft.sh <HEAD> AW/graft101.txt bin/nt_ai101.exe; queue (AW/qedit.py add, under flock) the
  runs in AW/queue_held97.txt with the exe renamed to nt_ai101 (knock, trafficstop, crashscene, events, hitped, nearmiss,
  nightwalk) + breakdown + copreturn; restart the runner (AW/ai_detach.sh /dev/null bash -c "exec AW/ai_queue.sh >> AW/run_queue.log
  2>&1", PID into AW/queue_runner.pid). Then the set 17 sign-off: blobs = the main tree's five files on that HEAD (AW/set17_results.txt,
  AW/signoff_set17_body.txt to update).
- Memory is the bottleneck: builds/runs often wait 20-40 min at the gates; the coordinator can hold others' launches on request.

# AI agent: RESUMED 2026-10-12 13:30 UTC (after the reboot) - live status, newest first

- 10-13 08:00 snapshot 48 progress: drop-off hold PASS on nt_ai100b (taxi100b_dropoff.png). copreturn: nt_ai100b showed the stranding dance (the car leaving while the 2nd officer walks round to the far door) -> seatfix3 (door walk holds the car) -> nt_ai100c crawled (the officer's own hold sent it to "wait by the car") -> seatfix4 (wait branch only while doorWalkT <= 0). police.cpp HEAD blob now 1eb24c9. nt_ai100d = graft100d (ab5995c + a8bf23c + 8efeb4a + ai 0f8e00b + pedai 98de395 + police 1eb24c9 + app eb0df14) building (PID AW/build100d.pid); queue: nt_ai100d copreturn, crashscene; helper AW/runner_after_100d.sh restarts the runner after the build. nt_ai100/100b/100c deleted.
- 10-13 07:00 WIND-DOWN ordered by the coordinator (user pausing): finish the snapshot 48 sign-off, then start nothing new, park set 17, update this file, leave nothing running, send one short "parked" message.
  - Snapshot 48 items (all on HEAD ab5995c): traffic_core 8efeb4a (cab stops in its lane at the hailer), traffic a8bf23c (drop-off waits for the exit clip), seat claims ai.cpp 0f8e00b + pedai.cpp 98de395 + police.cpp 68a4207 (claims + the FT_RETURN approach within 1.2 m of the bodywork + the door walk holds the car). Results so far: AW/taxi_results.txt.
  - In flight: nt_ai100b taxi (drop-off hold; waiting at the memory gate), nt_ai100c build (police 68a4207; PID AW/build100c.pid) then its copreturn + crashscene; helper AW/runner_after_100c.sh restarts the runner after that build. The set 17 release helper was STOPPED (set 17 runs are NOT queued; their list: AW/queue_held97.txt).
- 10-13 06:05 copreturn on nt_ai100: round 1 - officer 112 to the wheel (clip t=19.5-22.7, seat 0); officer 113 STUCK pressed against the TAIL of the estate police car (FT_RETURN walks to the car's middle until within 3 m; a long car's middle is > 3 m from its tail) and its walking refreshed escortHold, so the car held (it crept 3 m once). Pre-existing FT_RETURN flaw. FIX (AW/seatfix2.py, police.cpp): near enough when within 1.2 m of the bodywork (box) too. Round 2 invalid (its car put onto round 1's) -> copreturn v2 (AW/copreturn2.py: each round 35 m further along, 24 shots each). police.cpp HEAD blob now 1c177e6; test app eb0df14. nt_ai100b = ab5995c + a8bf23c + 8efeb4a + ai 0f8e00b + pedai 98de395 + police 1c177e6 + app eb0df14, building (PID AW/build100b.pid). Queue: nt_ai100b copreturn, taxi, crashscene; AW/release97b.sh (PID 24858) re-adds set 17 after the build. nt_ai100.exe can be deleted.
- 10-13 04:35 SNAPSHOT 47 COMMITTED = HEAD ab5995c (traffic ef101801, events ff4a3f8d). NEW CODEX P2 (ai.cpp ai_board::begin: the seat not reserved during the door clip; two officers into one seat). FIX (AW/seatfix.py, applied to HEAD copies AW/seat_head/ and the WIP): ai_board::claimed(g, veh, seat, except) = an O(peds) scan for PS_ENTERING peds with boardVeh/boardVehUid/boardSeat; ai_board::openSeat(g, veh, driver, except) = freeSeat minus claims. Used at: pedai.cpp ride-along, taxi fare, EMS crew, cuffed, coffee break, road rage/errand/drive-off carOk; police.cpp FT_RETURN (the Codex case), the car brought round (escort), the chase jump-in; WIP events.cpp slide-over. HEAD blobs: ai.cpp 0f8e00b, pedai.cpp 98de395, police.cpp 0030516. New test mode "copreturn" (AW/copreturn.py): two officers 7 m behind their car, FT_RETURN at once; logs OVERLAP / both in / away. nt_ai100 = ab5995c + traffic a8bf23c + traffic_core 8efeb4a + seat-claim ai/pedai/police + test app 0b1ac74 (AW/s48/app.cpp), building (PID AW/build100.pid). Queue: nt_ai100 copreturn, taxi, crashscene; set 17 (nt_ai97) re-added by AW/release97b.sh after the build. Snapshot 48 sign-off = traffic_core 8efeb4a + traffic a8bf23c + seat claims (ai/pedai/police), on HEAD ab5995c. nt_ai96/nt_ai99c deleted; nt_ai99b after its crashscene.
- 10-13 04:12 Pair accepted: snapshot 47 running (tree a25c5bd5: my two blobs + clasp + city 13). nt_ai96 deletable once 47 commits (the coordinator will say). nt_ai99b taxi: round 2 the cab stopped BESIDE the hailer (8efeb4a works); round 1 blocked by a car pulling out (lane obstacle). FOUND: the drop-off drives off during the fare's EXIT clip (main too) -> traffic.cpp a8bf23c (= ef10180 + task 2 hold). nt_ai99c = c1af87a + a8bf23c + 8efeb4a + ff4a3f8 + app b1df55d building (PID AW/build99c.pid). Queue: nt_ai99b crashscene (police pull-over), nt_ai99c taxi, then set 17 (nt_ai97). Follow-up sign-off = traffic_core 8efeb4a + traffic a8bf23c.
- 10-13 03:47 PAIR SIGN-OFF SENT to main (AW/signoff_s47_pair.txt): traffic.cpp ef101801 + events.cpp ff4a3f8d for snapshot 47; knock on the rebuilt nt_ai96 PASSED round 1 (knock96_a/b.png). Now: nt_ai99b (c1af87a + ef10180 + traffic_core 8efeb4a + ff4a3f8 + app b1df55d) taxi running (03:45), then crashscene (police pull-over); then the 8efeb4a sign-off. Then the set 17 runs on nt_ai97 (queued). nt_ai96 can be deleted once 47 is in.
- 10-13 02:40 COORDINATOR SPLIT: snapshot 47 = traffic.cpp ef101801 + events.cpp ff4a3f8d (+ the clasp); traffic_core 8efeb4a follows later. Sign off the PAIR from nt_ai96 + nt_ai98 once KNOCK passes on the exact pair exe. nt_ai96 is being REBUILT (I had deleted it; graft96 = c1af87a + ef10180 + ff4a3f8 + app ad5e571; chain AW/chain96b.sh, PID AW/chain96b.pid; the builds wait on build.sh's flock behind another agent's build). Then nt_ai99b (AW/chain99b.sh). Queue: nt_ai96 knock first; chain96b then adds nt_ai99b taxi + crashscene (AW/queue_held99b.txt) and the set 17 nt_ai97 runs (AW/queue_held97.txt). Pair sign-off draft: AW/signoff_s47_pair.txt (to write). I told the coordinator: no reason to hold the pair (ef10180 alone is not worse for short-stopped cabs).
- 10-13 02:30 (after the 02:15 worker restart; my waiters were lost, the runner 594 survived) nt_ai98 taxi: the guard WORKED (clip through stopTimer 25, cab held) but the cab got STUCK behind a parked car in the kerb strip after the fare was in -> 2819f95 withdrawn; 02ed981 dropped (nt_ai99 build killed). FINAL traffic_core blob 8efeb4a: with a host stop point in its lane the cab neither nudges to the kerb nor takes the kerb-stop clamp (stops in the lane beside the hailer). nt_ai99b = c1af87a + ef10180 + 8efeb4a + ff4a3f8 + app b1df55d building (PID AW/build99b.pid). Queue: nt_ai99b taxi, knock, crashscene. Set 17 runs held in AW/queue_held97.txt. The coordinator still thinks 2819f95 - tell them 8efeb4a in the sign-off (or before). Snapshot 46 = 67e6159 (none of my files).
- 10-13 02:20 trafficstop on nt_ai96 PASS (AW/taxi_results.txt, trafficstop96.png). nt_ai98 taxi: the cab (clamp skipped) still stopped ~24 m short - it nudged into the kerb strip and stopped behind a PARKED car. 2nd traffic_core tweak: while the stop point is > 12 m ahead, no kerb nudge (in the lane, then over). traffic_core blob now 02ed981 (NOT 2819f95 - tell the coordinator). nt_ai99 = c1af87a + traffic ef10180 + traffic_core 02ed981 + events ff4a3f8 + app b1df55d, building (PID AW/build99.pid). Queue: nt_ai99 taxi, knock, crashscene (the police pull-over check). The set 17 runs on nt_ai97 are HELD in AW/queue_held97.txt - re-add them once nt_ai99 is built. Clasp FYI: no conflict (peds.cpp not in my WIP; the couple test only logs the hand gap).
- 10-13 01:20 COORDINATOR: all three fixes go into SNAPSHOT 47 (traffic.cpp ef101801, traffic_core.cpp 2819f95e, events.cpp ff4a3f8d, on c1af87a's versions). Snapshot 46 (night B, landing predictions, faces batch 8) touches none of my files: my bases stay valid on 46's commit. SIGN OFF when nt_ai98 and nt_ai96 have run: taxi (stops at the hailer, waits through the clip), trafficstop, knock, and a quick check that police pull-overs and drop-offs behave as before. PR #46's merge waits for 47.
  - The taxi test now also does a short ride + drop-off (AW/taxidrop.py; in the WIP app.cpp and AW/calmfix/app.cpp -> nt_ai98's app blob b1df55d). Police pull-over check: crashscene on nt_ai98 (the unit pulls over behind the scene, no stop point).
  - Queue: nt_ai96 trafficstop, nt_ai98 taxi, nt_ai96 knock, nt_ai98 crashscene; then nt_ai95 hitped/nearmiss/nightwalk; nt_ai97 knock/trafficstop/crashscene/events (set 17).
- 10-13 01:25 Taxi round 1 on nt_ai94c FAILED for another reason: the cab stopped at the kerb at once (traffic_core DM_PULLOVER clamp: remaining nudge*4 + v*0.5), 36 m short of the hailer; the fare walked up, stuck behind a kerb bench 3.4 m from the cab, and the cab gave up at 25 s. FIX (main tree, traffic_core.cpp blob 2819f95 = c1af87a's d716924 + 4/-1): the pull-over clamp is skipped while the host's stop point lies ahead in this lane (only the taxi hail sets stopPath with DM_PULLOVER).
  - nt_ai96 (taxi + calmActor fixes, test app) BUILT 01:13. nt_ai97 (set 17 WIP + calmActor + knock, NO traffic_core fix) building (chain PID 15559, AW/build96chain.sh). nt_ai98 = nt_ai96 + traffic_core 2819f95 (AW/graft98.txt) chained after it (PID 17952).
  - Queue: nt_ai98 taxi (top), nt_ai96 trafficstop + knock, nt_ai95 hitped/nearmiss/nightwalk, nt_ai97 knock/trafficstop/crashscene/events.
- 10-13 01:10 TAXI FIX (PR #46, coordinator priority 1): traffic.cpp blob ef10180 = c1af87a's 2112f63 + the DM_PULLOVER task 1 hunk (the hailer counts while PS_ENTERING with boardVeh == the cab; never given up mid-clip). Audit sent to main earlier.
  - Test exe nt_ai94c = c1af87a + traffic ef10180 + app 309ea19 (AW/taxifix/app.cpp: taxi mode v3). Taxi run on it started 00:58; waiter b3h0jh40u (round 2 or the end). Then: sign off to main, delete nt_ai94c.
  - NEW BUG found on nt_ai95's trafficstop (also on HEAD): calmActor (events.cpp) wants PS_ONFOOT, but removePedFromVehicle(.., true) leaves the ped PS_EXITING for the door clip -> the traffic stop is called off at ST_B as the officer gets out; a real fender bender (aiFenderBender, flag 3) skips to ST_C as the drivers get out. Fix: calmActor also accepts PS_EXITING. HEAD blob events.cpp ff4a3f8 (c1af87a's 1e506ec + that only; AW/calmfix.py). WIP has it too.
  - New test mode "knock" (AW/knocktest.py, after evdoortest.py): two cars nose to tail in a kerb lane + aiFenderBender, at 4 s and 80 s. Applied to the WIP app.cpp and AW/calmfix/app.cpp (= taxifix app + evdoortest + knocktest).
  - Plan: build nt_ai96 = c1af87a + traffic ef10180 + events ff4a3f8 + app AW/calmfix/app.cpp; run trafficstop + knock on it (HEAD fixes); WIP set 17 exe after that (trafficstop, knock, events, hitped, nearmiss, nightwalk, crashscene).
  - The stale waiters b881i3q7j / b30lfzti5 were stopped; nt_ai95's trafficstop was killed (useless after the abort).
- 22:45 Separation change (loco_sepclean3): crowded, crossing and walkby all PASS. Reported to main (AW/sep3_results.txt). It joins snapshot 45 with sets 14-16; peds.cpp there will be 52a49711 (grabAxis + separation). My bin/nt_ai_sep3.exe symlink is deleted.
  - crashscene on nt_ai92 PASS: both crash drivers go round their car's free end to the driver's door, then the door clip, then away.
  - Set 17 = graft92 (on 61a3b7b). Delta over the 14-16 blobs: ai.cpp +32/-3, ai_game.h +8/-2, app.cpp +26/-8 (hitped and nearmiss tests only), events.cpp +39/-6, pedai.cpp +40/-5. barks.cpp and police.cpp are unchanged.
  - Queue: events (running), hitped, nearmiss, nightwalk on nt_ai92. Results: AW/set17_results.txt. Texts: AW/signoff_set17_body.txt.
  - Sign off once snapshot 45 lands. The base will be snapshot 45, and the blobs are the main tree's five files.
- 20:05 Sets 14-16 VERIFIED by the coordinator. They go in after PR #45 (city batch 11), as their own PR with the MSVC CI and a Codex review.
  - Set 17 fixes found by crashscene:
    - On nt_ai90, the crash walk-back corners were taken off the body origin, not the box centre. Fixed in events.cpp and aiCarEndToWalkRound.
    - On nt_ai91, the "past the end" threshold (by+0.7) was further out than BRAIN_GOTO's 0.6 m arrival, so drivers stood at the first corner. Fixed to by+0.2.
    - Also new: chatT (overheard lines retry in crowds), and the crash status line now shows each driver's place off their car's box.
  - nt_ai92 = 61a3b7b + AW/graft92.txt (the main tree), building (PID in AW/build92.pid). nt_ai90/nt_ai91 were deleted after their partial runs.
  - REQUEST from the coordinator: crowd tests on the animation agent's separation change.
    - The exe is SP/locotour/sepclean3/bin/loco_sepclean3.exe, symlinked as bin/nt_ai_sep3.exe (my symlink; delete it after).
    - Queue: sep3 crowded, crossing, walkby; then the same on nt_ai90a as the baseline; then the nt_ai92 set 17 runs (crashscene, events, hitped, nearmiss, nightwalk).
    - Report a short pass/fail to main: queues at crossings and bus stops, companions abreast, stuck or jitter in crowds. Life and busrun too if cheap.
  - Possible follow-ups (not done): events.cpp flag-3 walk (1586) and the officers' spots (1669) use the same by+0.6/by+0.8 corner pattern with anchor walks. They were not seen failing.
- 18:01 SETS 14-16 SIGN-OFF SENT (AW/signoff_set1416_send.txt), on 61a3b7b. Blobs: ai.cpp ce7c7c3, ai_game.h 036e52e, app.cpp 5ca9815, barks.cpp 5772084, pedai.cpp b79602b, police.cpp 27a7a43, peds.cpp dbe0046.
  - Tested on nt_ai90a (exact graft): showoff and runby, both PASS after the fixes.
  - Tested on nt_ai86: rideoff, couple, copbreak and crosswalk PASS; nightwalk found the set 10 chat gap.
  - Delete nt_ai90a.exe and nt_ai86.exe once the coordinator confirms the merge.
  - Next: set 17 on nt_ai90 (main tree graft AW/graft90.txt: set 17 + the hitped wait + the nearmiss lane + the chat retry). Queue: crashscene, events, hitped, nearmiss, nightwalk. Sign-off text: AW/signoff_set17_body.txt.
- 16:35 MAIN MOVED to 61a3b7b (PRs #43/#44: the MSVC CI; app.cpp openPause/MA_RESUME). New code must compile under MSVC: no GCC builtins (use src/core/bits.h), no VLAs, __attribute__, statement expressions, typeof, case ranges, __int128, or bare M_PI. My added lines were scanned and have none of these.
  - The shared tree's HEAD is now 61a3b7b. My WIP app.cpp was 3-way merged by the coordinator.
  - Sets 14-16 (graft on 61a3b7b; AW/graft90a.txt):
    - ai.cpp ce7c7c3 = 51ccb87 + the show start/end fix (AW/showfix.py).
    - app.cpp 5ca9815 = d8ab90d 3-way merged with main's change (d946fcc, AW/rebase61/), plus the crosswalk radio-off fix (AW/xwalkfix.py) and the runby fixes (AW/runbyfix.py, AW/runbyfix2.py).
    - ai_game.h 036e52e, barks.cpp 5772084, pedai.cpp b79602b, police.cpp 27a7a43, peds.cpp dbe0046.
  - Set 17 + fixes: the main tree (AW/graft90.txt).
  - Builds (one at a time, AW/build90chain.sh, PID in AW/build90.pid): nt_ai90a (sets 14-16 exactly), then nt_ai90 (the main tree).
  - Queue: nightwalk on nt_ai86 (running); showoff and runby on nt_ai90a; crashscene, events, hitped and nearmiss on nt_ai90. nt_ai88 and nt_ai89 were deleted before any run.
  - nt_ai86 results so far: rideoff, couple and copbreak PASS; crosswalk PASS; showoff found the show start/end bug (fixed); runby's test failed both ways (fixed). Details are in AW/set1416_results.txt.
  - Sign-off texts: AW/signoff_set1416_head.txt + body (to update for 61a3b7b and the new blobs), and AW/signoff_set17_body.txt.
- 13:50 Running (all mine; kill only these PIDs):
  - the queue runner (PID in AW/queue_runner.pid; started 13:37; log AW/run_queue.log);
  - the nt_ai88 graft build (PID in AW/build88.pid; log AW/build_nt_ai88.out; it waits on the memory gates).
- Queue (AW/queue.txt):
  - nt_ai86 (sets 14-16): copbreak (running), showoff, crosswalk, runby, nightwalk.
  - nt_ai88 (set 17 + fixes): crashscene, events, hitped, nearmiss.
- nt_ai88 = 894e7fb + AW/graft88.txt, which holds sets 14-17, the hitped fix, the nearmiss lane fix and peds dbe0046:
  - ai.cpp ef87088, ai_game.h 98fbc34, app.cpp 28cdbd4, barks.cpp 5772084, events.cpp fc080f4, pedai.cpp b8821f7, police.cpp 27a7a43, peds.cpp dbe0046.
  - The main tree passed syntax checks: AW/syntax_set17b.out and AW/syntax_set17c.out.
- New since the wind-down (main tree, in graft88):
  - hitped fix:
    - PedAI::waitDrv/waitDrvUid/waitT, set in ai.cpp at the knock-down. Once up, the one hit waits up to 8 s, facing the driver coming over (pedai.cpp, before the wander activity switch).
    - The sorry driver's 3.5 s linger clock runs only within 4 m of them, or after their word.
    - Test: hitped puts the walker a quarter second ahead (2-3.2 m), and the one hit goes back to ACT_WALK.
  - nearmiss test: the lane pick leaves out LF_HIGHWAY, LF_RAMP and LF_BRIDGE, with a second round allowing a 115 m run. The kerb peds stand 60-105 m ahead.
  - events.cpp: a forward declaration of ai_board::beginThen. `in` is renamed `inCar` in EV_TRAFFIC_STOP ST_D.
- Disk cleanup at 13:35: deleted the PNG folders of merged sets in /tmp/ai, the old SP/log_*/wine_* files, AW/evidence, and the traffic_sim host objects (ts_world.o, ts_ai.o, traffic_sim; rebuildable). That freed about 313 MB.

# AI agent: CURRENT STATE (wind-down 2026-10-06 16:25 UTC, stopped early for the user's usage limit)

Nothing of mine is running. The queue runner, the copbreak game and the /tmp/wine_ai wineserver were stopped by PID.

**Merged:** sets 2-13. Sets 10-13 = snapshot 42 (PR #42, main 894e7fb).

**PARKED: sets 14-16.** Not signed off; sets 14-15 are untested.
- The graft goes on 894e7fb: `git cat-file -p <blob> > src/game/<file>`.
  - ai.cpp 51ccb8791da7269c1e664519c0432ae552efb67c
  - ai_game.h 036e52e96d053db3f1b07efcd86c6c96d9ee2268
  - app.cpp d8ab90d326f6ec25676943873511101549ea56c7
  - barks.cpp 57720843ed3141daaaa398df6cc5649680f712e6
  - pedai.cpp b79602b7255beb3e01f97b49d3dd9cd28c53d97d
  - police.cpp 27a7a430ac9cb84bcb1ac5a100602ac72e60a334
  - peds.cpp (the lead's) dbe0046df9d5d184571d212439191928fb836086 = 5c6e2ff plus `in.grabAxis = vec3(0.f, 1.f, 0.f);` after `in.grabWeight = 1.f;` in the couple block.
  - traffic.cpp and traffic_core.cpp are unchanged from 894e7fb.
  - Each AI blob contains sets 10-13 as merged, plus 14-16.
- Tested on bin/nt_ai86.exe (b49bc7f + AW/graft86.txt; kept for the remaining runs):
  - rideoff PASS: the door clip runs about 2.7-3.0 s and the car holds until both are in.
  - couple PASS: hands 0.03-0.07 m apart, holdW 1.0, the clasp fingers-down.
  - Results: AW/set1416_results.txt. Sheets: /tmp/ai/rideoff_nt_ai86, /tmp/ai/couple_nt_ai86.
- Still to run on nt_ai86, already in AW/queue.txt: copbreak (cut off at t=102 with the crew on its break; partial log SP/log_copbreak_nt_ai86_partial.txt), showoff, crosswalk, runby, nightwalk.
  - Start them: `AW/ai_detach.sh /dev/null bash -c "exec AW/ai_queue.sh >> AW/run_queue.log 2>&1"`.
  - Then send AW/signoff_set1416_head.txt + AW/signoff_set1416_body.txt + the results as the sign-off.

**WIP in the main tree (uncommitted):**
- app.cpp, barks.cpp, pedai.cpp and police.cpp equal the 14-16 blobs.
- ai.cpp (0eaeb61) and ai_game.h (baa7e91) are 14-16 plus set 17.
- events.cpp (5a754b2) is set 17.
- peds.cpp is HEAD's; the grabAxis line is only in dbe0046.
- The main tree passed a syntax check at 16:12.

**PARKED: set 17 (written, never built or run).** Event actors get in through the door:
- ai.cpp ai_board::beginThen with PedAI::boardThen (1 drive away, 2 nothing), plus door LOG lines.
- events.cpp: EV_CRASH and EV_BREAKDOWN drivers (beginThen 1), EV_TRAFFIC_STOP ST_D officer (beginThen 2).
- BRAIN_FOLLOW is not converted: a follower would be left behind if the player drove off mid-clip.
- Test with crashscene and events on a fresh graft (AW/mkgraft.sh + AW/ai_buildgraft.sh on the then-current main).

**Other tests and fixes still to do:**
- hitped fix (not written): the victim waits facing the sorry driver for up to ~8 s, and the test puts the walker within stopping distance. Rerun hitped.
- nearmiss test: exclude highway and ramp lanes. Rerun.
- BRAIN_GOTO easing behind slower walkers: only after the animation agent's separation fix. Run crowded, crossing, walkby, life and busrun on their exe first.

Fuller resume notes (scripts, rules, commands) are in AW/resume_top_draft.md. It was drafted before the stop, and its [[...]] slots were never filled.

# History (older running log)

# AI agent: where things stand (wrapped up 2026-10-01 21:13)

Owner of: src/game/ai.cpp, police.cpp, lanes.cpp, traffic_core.cpp, pednav.cpp, barks.cpp, traffic.cpp, pedai.cpp,
events.cpp, ai_core.h, ai_game.h, tools/traffic_sim.cpp; the "AI additions" block of game.h and gameworld.h; the
autoplay modes in app.cpp.

Not mine:
- peds.cpp and carry.cpp belong to the lead. My hand blocks below are for the lead to graft.
- population.cpp belongs to the world-life agent. I only call its pop_detail::greet* helpers.
- src/anim belongs to the animation agent; src/world is not mine.

Nothing of mine is running: no chains, builds, games or monitors.

`SP` = /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad, and `AW` = SP/aiwork.

## 1. Merged / handed over

**Set 2 is with the lead for snapshot 24.** It was signed off on nt_ai59, which is 23069e5 plus 8 blobs. The lead froze them in SP/ai_set2_blobs.txt:

| File | Blob |
|---|---|
| ai.cpp | 4aaaaf83c9c25dd882d7a86e72053be93205c361 |
| ai_game.h | 39cbe3cad0acbfdb37faaa0961b57939879db89b |
| app.cpp | ce58b661969e1f4c0b7983032a8b9ec54ad7f836 |
| barks.cpp | 2f3c495f92e2c922f591d81bd58ef72d5e5581d6 |
| events.cpp | c11dea8a2fb11aea0a4cc0e02a3236783cfd584a |
| pedai.cpp | b857ca56a51882839b5400ddac530f15b1f9576c |
| police.cpp | 58a65e7ea88f7ccd9809e8b0ccffa2e6dd30d7d8 |
| traffic.cpp | a35d366513736518e63a0c258b1b116e365e3b36 |

- Set 2 goes in without the couple block. The lead wants it, together with the reach block, in the set 3+4 sign-off once a life run has checked the hands visually.
- Set-2 evidence:
  - Logs: SP/wine_{arrest,events,takeover,pedstop,panic,life,brawl}_nt_ai59.out and SP/log_*_nt_ai59.txt.
  - Montages: AW/evidence/m59*.png.
  - Sign-off text: AW/signoff_set2.txt.

## 2. Coded but unverified (all in the shared tree's AI files, uncommitted)

The shared tree's AI files hold sets 3, 4 and 5.
- **Current state:** the blobs are recorded in **AW/blobs_wip_set5.txt** (written with `git hash-object -w`).
- **Syntax check (21:12):** `AW/ai_syntax.sh` came back clean, with no errors or warnings in my files.
- **Never built or run:** this exact state.

### Sets 3+4 (frozen as nt_ai62)
- **Exe:** bin/nt_ai62.exe, built 20:38. Its tree had both hand blocks pasted into peds.cpp.
- **Blobs:** AW/blobs_nt_ai62.txt. Its peds.cpp blob, 002cb6e, includes the two blocks.
- **Tests:** none run yet. The chain was stopped before its first game launched.
- **Contents:** see AW/signoff_set34_draft.txt.
  - Parking tickets (police_ticket, PT_SCENE).
  - Crash scenes with a patrol.
  - Beat pairs helping someone down.
  - Arrest fixes: escortWait, the transport pulls over when held up within 60 m, units go on foot only within 45 m.
  - Hand on the head at the car door.
  - Greetings, overheard chats, the crossing button, lost, shoelace, rain, the proposal event, honks at jaywalkers.
  - The reach hook.
  - Test modes: ticket, proposal, crashscene, rain, night.

### Set 5
- **nt_ai63** (bin/nt_ai63.exe, built 21:10:50 with the hand blocks; blobs in AW/blobs_nt_ai63.txt) has:
  - EV_PANHANDLER: he asks the player; standing still gives $2, walking on gets no hard feelings.
  - Running for the bus (traffic.cpp bus_run, pedai ACT_ENTER_VEH busRun).
  - Rubbernecking: ai.cpp ai_sights, events ev_sights, the traffic.cpp cap, and Driver::hostCap/hostCapT in traffic_core/ai_core.h.
  - Near-miss startles.
  - Beat officers calling to jaywalkers.
  - Personal-space reaction (crowdT).
  - Test modes: panhandle, busrun, nearmiss, jaywalk, crowded.
- **Only in the shared tree since nt_ai63** (not in any build):
  - The "followed" reaction, plus the followed test mode.
  - A hand over the head when running in the rain.
  - The cut-off horn and shout (BK_CUT_OFF).
  - A line-of-sight check before someone runs for the bus.
  - Passers-by dropping change in the panhandler's cup (BK_GIVE_CHANGE).
  - Pedestrians asking beat officers the way (askableCop, BK_ASK_COP_WAY/BK_COP_GIVE_WAY).
  - meetBoost 3 in the jaywalk test.
- **Sign-off draft:** AW/signoff_set5_draft.txt.

### Hand blocks for peds.cpp (exact text; the lead grafts them)
Placement: GameWorld::animatePed, just before the `// foot IK: probe the ground where each foot is planted...` block, after the escort hand-hold.
- The couple block goes first, then the reach block.
- The same text is in AW/couple_block.txt and AW/reach_block.txt.
- AW/ai_hands.py pastes both into a frozen tree, and skips any block that is already there.

```cpp
    // a couple walking hand in hand (pedai.cpp: PedAI::handWith on both, renewed every frame by the companion): each
    // one's inner hand on the point between them at hand height (the animator picks the hand on that side)
    if (in.grabWeight <= 0.f) {
        int self = (int)(&p - peds.data());
        const PedAI* q = self >= 0 && self < (int)ai.ped.size() && ai.ped[self].uid == p.uid ? &ai.ped[self] : nullptr;
        int o = q ? q->handWith : -1;
        if (o >= 0 && o < (int)peds.size() && o < (int)ai.ped.size() && peds[o].used && peds[o].uid == q->handUid && ai.ped[o].uid == peds[o].uid &&
            ai.ped[o].handWith == self && time - q->handT < 0.3 && p.state == PS_ONFOOT && peds[o].state == PS_ONFOOT && !p.ragdoll && !peds[o].ragdoll) {
            vec3 j = rel(peds[o].pos, p.pos) * 0.5f;
            in.grabTarget = vec3(dot(vec2(j.x, j.y), rightV), dot(vec2(j.x, j.y), fwd) + 0.05f, 0.84f);
            in.grabWeight = 1.f;
        }
    }
    // an AI hand out to a point (pedai.cpp / police.cpp: PedAI::reachAt while reachT is ahead - a beat officer's parking
    // ticket going under the wiper); the animator picks the hand on that side
    if (in.grabWeight <= 0.f && p.state == PS_ONFOOT && !p.ragdoll) {
        int self = (int)(&p - peds.data());
        const PedAI* q = self >= 0 && self < (int)ai.ped.size() && ai.ped[self].uid == p.uid ? &ai.ped[self] : nullptr;
        if (q && time < q->reachT) {
            vec3 j = q->reachAt - p.pos.toVec3();
            in.grabTarget = vec3(dot(vec2(j.x, j.y), rightV), dot(vec2(j.x, j.y), fwd), j.z);
            in.grabWeight = 1.f;
        }
    }
```

## 3. Scripts, exes, logs (AW = SP/aiwork)

| Script | What it does |
|---|---|
| ai_buildtree.sh OUTEXE | Freezes src, tools, build.sh and build/gen into SP/tree_TAG and records the blobs from that copy into AW/blobs_TAG.txt. Then: memgate 2000, syntax check (exit 2 if the error is mine, 3 if foreign), memgate 3200, `QUICK=1 build.sh`, moves the exe to bin/. With `HANDS=1` it first pastes the hand blocks with ai_hands.py. Writes its PID to AW/ai_buildloop.pid. |
| ai_refreshtree.sh TAG | Copies the current AI files into a frozen tree that is still queued. It refuses once a cc1plus is running under the build's PID. |
| ai_runtests.sh EXE mode:dur:renderevery:autoevery ... | Sequential games through tools/run.sh in the Wine prefix /tmp/wine_ai. Memgate 3000. Shots go to /tmp/ai/MODE/ (its BMPs are cleared at start). Writes SP/wine_MODE_TAG.out and SP/log_MODE_TAG.txt, and prints a "MODE rc=..." line per run. |
| ai_detach.sh LOG CMD... | Runs a command with setsid nohup and prints its PID. |
| ai_memgate.sh N, ai_syntax.sh [TREE] | My memory gate, and the syntax check (needs memfree of at least 2000; gives up after 10 min). |
| chain_set34.sh, chain_set34b.sh, chain_set5.sh | The queued chains, now stopped. **Their `kill -0 PID` waits name old PIDs** (16492, 25214, 25290), which may belong to other processes after a restart. Edit or remove those lines before reusing them. |

- Exes: bin/nt_ai62.exe (sets 3+4) and bin/nt_ai63.exe (set 5 without the later additions). nt_ai59/60/61 are deleted.
- Logs:
  - Build logs: AW/build_nt_ai*.out.
  - Chain logs: AW/run_chain*.log.
  - Older montages: /tmp/ai/MODE/*.png; the set-2 ones are copied to AW/evidence/.

## 4. Next steps (rules: QUICK=1 builds; at most one build and one Wine run of mine at a time; memfree gates of 3200 for builds, 3000 for runs and 2000 for syntax checks)

1. **Sets 3+4 on nt_ai62.** Run:
   `AW/ai_detach.sh AW/run_chain_set34.log AW/ai_runtests.sh bin/nt_ai62.exe ticket:150:12:60 proposal:150:12:60 crashscene:150:12:60 life:200:12:100 rain:90:12:100 hurt:150:12:30 arrest:260:12:60 pedstop:240:12:60 events:240:12:60 night:200:12:100`
   Then run crowd:28.5:12:100 and soak:300:12:100.
   Check each:
   - ticket: the wiper hand; the owner reads the ticket and drives off.
   - proposal: yes, then no; the ring.
   - crashscene: the unit pulls over behind; officer roles.
   - life: couples "hand in hand" more than 0 and **the hands visually**; greetings; "asked okay" after t=140.
   - rain: shelters.
   - hurt: copAid.
   - arrest: **stage 3 now completes in 260 s?** Tell the lead the result.
   - crowd: AI ms. soak: traffic health.
   Then send "AI snapshot-safe, sets 3+4" using AW/signoff_set34_draft.txt, the nt_ai62 blobs, and both hand blocks (§2).
2. **Set 5.** Build nt_ai64 from the shared tree:
   `HANDS=1 AW/ai_detach.sh AW/build_nt_ai64.out env HANDS=1 AW/ai_buildtree.sh bin/nt_ai64.exe`
   Then run:
   `AW/ai_runtests.sh bin/nt_ai64.exe panhandle:110:12:60 busrun:170:12:60 jaywalk:120:12:60 crowded:90:12:60 followed:100:12:60 nearmiss:120:12:30 rain:90:12:100 crashscene:150:12:60 life:200:12:100 soak:300:12:100`
   Check the census totals: "bus runs (made/missed)", "change (passers-by)", "gawks", "near misses", "jaywalk calls", "crowded (left)", "followed (sharp)", "cut offs". Also check the LOG lines "bus:", "events: panhandler", "police: officer N calls to ped", "street meet: ... asks officer".
   Then sign off with AW/signoff_set5_draft.txt. After the sets are signed off, delete nt_ai62/63.
3. Keep developing in the shared tree. Ideas: drivers turning their heads at scenes (would need a peds.cpp hunk from the lead); more ambient events.


## Update 2026-10-05 (after the reboot)

**Test runner:**
- The test queue: AW/ai_queue.sh, started through handoff.sh, logs to AW/run_queue.log.
- The queue file is AW/queue.txt, with lines of the form "EXE MODE:DUR:EVERY[:SHOTS[:WxH]]". Edit it only under its lock with qedit.py, e.g. `flock AW/queue.lock python3 AW/qedit.py top|add|drop|show`.
- The runner is AW/ai_runtests2.sh. It adds an optional WxH field, and its shots go to /tmp/ai/MODE_EXETAG/.
- **Never edit a script while it is running**: bash reads scripts as it goes. Write a new file instead.

**Sets 3+4 on nt_ai62, results:**
- life: ok.
- arrest: stage 3 now finishes at t=165, inside the 260 s window.
- ticket: ok, but no owner came. The owner fix is in the shared tree.
- proposal: yes and no, both ok.
- crashscene: the flow is ok, but the camera was blocked. The camera is fixed in the shared tree.
- Still to run: rain, hurt, pedstop, events, night, crowd, soak. They are in the queue.

**Couple hands:**
- The first block (both hands to the midpoint, at 0.84 or 0.93 m) never joined the hands.
  - The animator closes a fist round a vertical handle a palm short of the point, so the two fists stay apart.
  - Logged hold weights were 1.0, but there was still a visible gap of about 8 cm.
- The new block: the companion's inner hand grips the leader's inner hand bone, the way the escort grips an arm. It is in AW/couple_block.txt; earlier versions are couple_block_084.txt and couple_block_093.txt.
- With it, pedai.cpp brings hand-holding couples to 0.52 m apart, and skips them if a dog leash is in either inner hand.
- The couple mode is now set at 22:00, after dog hours, and logs "hand to hand" and the hold weights. It is to be verified on nt_ai69.

**New in the shared tree (next sign-off):**
- Wildlife::leashHand(ped, uid), in wildlife.h and wildlife.cpp. The lead OKed adding it.
- Turn-rate cap: walking at most 3 rad/s, slower through turns over 60°.
- Follower slot keeping: continuous, smoothed, facing the leader's heading near the slot.
- Anchor arrival hysteresis: walk in only from beyond 0.6 m, settle inside 0.2 m, face anchorYaw for the last 1 m. Counted in the census as "anchor rewalks".
- Ticket owner fix.
- The walk-by test mode (greetings, "are you okay?").
- The crashscene camera fix.

## Update 2026-10-05 20:00

**Queue (AW/queue.txt, runner AW/ai_queue.sh started by AW/chain72.sh at 19:51):**
- nt_ai72 couple (running), then nt_ai72 walkby and hurt, then nt_ai62 pedstop / events / night / crowd / soak (sets 3+4),
  then the set-5 runs on nt_ai72 (panhandle busrun jaywalk crowded followed nearmiss rain crashscene life soak).
- nt_ai64 and nt_ai71 are deleted (set 5 moved to nt_ai72). Exes left: nt_ai62, nt_ai72.

**nt_ai72** (built 19:51, HANDS=1; blobs in AW/blobs_nt_ai72.txt) = the shared tree at 19:35:
- Everything of set 5, plus the turn-rate cap, follower slot keeping, anchor hysteresis, Wildlife::leashHand.
- Leaders no longer shy from their own companions: PedNav::step takes an ignore list (ai_core.h, pednav.cpp); a
  companion registers itself on its leader every frame (PedAI::company[2] / companyT[2]).
- Hand-holding spacing 0.62 m; the slot is built on the leader's velocity.
- couple_block.txt is the symmetric clasp again: each partner's inner hand at the midpoint plus 0.08 m toward the
  other, z 0.93 (the animator's fist closes 3.5 cm short of a point; the old grip-the-leader's-hand version is
  couple_block_grip.txt). To verify: hold weights near 1, "hand to hand" (wrist to wrist) about a palm, visually a clasp.
- nt_ai71's couple log showed the cause of the lag: the leader at 1.8 m/s (its walker wanted 1.27) because pednav's
  social force (about 2.0 at 0.52 m) pushed it off its companion.

**Sets 3+4 results on nt_ai62 so far:** life, arrest (stage 3 done at t=165), ticket, proposal, crashscene, rain, hurt
(victim got up - the app.cpp victim choice is fixed; rerun on nt_ai72). Still to run: pedstop, events, night, crowd, soak.

**New in the shared tree since nt_ai72 (set 6, syntax clean):** car alarms and key fobs.
- ai.cpp car_alarm: a stolen car's alarm now stops after 14-24 s (it wailed forever while driven - a bug); STIM_ALARM
  every second while one goes; the owner (police_ticket::pickOwner) comes for half of them, 4-9 s on for a knock, 1 s for
  a break-in (VehAI alarmOwner / alarmTheft). car_alarm::fob = the chirp (SFX_CAR_ALARM_CHIRP, unused till now) and the
  hazards flashing (VehAI flashT). VehAI lockAt: drivers who park lock it a few steps off (traffic.cpp, 3 in 4).
- pedai.cpp: STIM_ALARM - people stop for a look, grumble (more late at night), "hey, that's not your car!" when they
  see the player breaking in; officers glance over. ACT_ALARM_OWNER steps 0-5 (over, the fob, round to the sidewalk side,
  crouched look, a word or "was that you?" to the player, drive off half the time; the theft: run to the door, shout,
  hand to the head as it goes). ACT_DRIVE_OFF: unlock with the fob 6 m off (3 in 4), the arm out.
- barks: BK_ALARM_GRUMBLE / WITNESS / OWNER / CHECKED / ACCUSE / THEFT / GONE. Census: "alarms (owners looks) fobs".
- Test mode "alarm" (app.cpp, ai.forceAlarmOwner): knock at t=8 with the player by the car, theft at t=70.

## Update 20:25
- nt_ai73 (20:16) = set 6 first cut; used only for the couple check (couple block with in.passWeight = 0). Found after
  it: kerbside parked cars have no VehAI record (spawnVehicle, no vehAI call), so ai.cpp skipped their alarms entirely
  (pre-existing: a knocked kerbside car wailed forever). Fixed: alarm cars get vehAI(i) before the loop.
- nt_ai74 (building, frozen 20:23:52) = the sets 5+6 candidate: + the alarm-record fix, owners only for civilian
  VR_TRAFFIC cars, the flinch exemption from the turn cap, staggered starts at crosswalks (Walker::goT in pednav.cpp),
  app.cpp alarm test fixes. All alarm/set-5 lines in the queue now point at nt_ai74. Sign-off draft:
  AW/signoff_set6_draft.txt (sets 5+6 together). Anything new in the shared tree from here on is set 7.
- The couple gap was the animation agent's pass-by lean (peds.cpp: a companion at the elbow counts as a passer). The
  lead has asked the animation agent for the root fix; the couple block keeps passWeight = 0 while holding anyway.

## Set 7 (shared tree after the nt_ai74 freeze at 20:23:52; syntax clean)
- Ride-along: a companion whose leader drives off in a kerbside car (ACT_DRIVE_OFF, or just in at the wheel) goes round
  to a passenger door (ACT_RIDE_ALONG, PedAI::rideSeat), waits there till the driver is in, then gets in (seat via
  freeSeat, BRAIN_PASSENGER). The car waits: VehAI::escortHold is now general ("somebody on the way to it"), and
  traffic.cpp pullOutStep only pulls out once time >= escortHold. Census "rides". Before: the companion was dropped
  and walked on alone (population.cpp departures can pick a leader with company).
- Test mode "rideoff" (app.cpp): at 6 s and 60 s a walking pair's leader is sent to a kerbside car (as departures do).
- Riders (PedAI::rider): out with the driver when the car parks (traffic.cpp parking: every passenger seat, ACT_LEAVE_CAR
  to the same door); out on their own when the driver is gone (ai.cpp updateBrain: the car stood with nobody at the
  wheel -> on foot; the player at the wheel and under 3 m/s -> flee, BK_CARJACKED). Cleared whenever on foot.
- Alarm owners skip delivery vans on an errand (va.errand != 0).
- The AI look hook: PedAI::lookAtPt / lookAtT + the extended look block (AW/look_block.txt; the sets 5+6 graft is the
  in-car-only AW/look_block_74.txt). Uses: glances along the road while waiting to cross (near traffic's side first,
  again as they step off), down the street for the bus/taxi, the head turned to a car alarm.
- Test modes: "rideoff" and "crossing" (the busiest signalled crosswalk near the downtown corner; logs each step-off's
  delay after the light and how many waiting have their head turned).
- People look up at a police helicopter low over the streets near the player (ai.cpp AIState::heliAt / heliLow every
  frame; pedai.cpp perception: a look up for 2-4.5 s, once in 25 s each, following it as it goes; PedAI::heliLookT).
- 20:52 couple on nt_ai73 done: held 94 s, hold weights 1.00 in 46/47 samples, wrists median 0.15 m (min 0.12): a clasp.
  Couple + reach blocks sent to the lead (saved by the lead as scratchpad/ai_hand_blocks_v093.txt); the lead grafts
  them with the sets 3+4 sign-off. The lead wants in that sign-off: the AI blobs, the leashHand wildlife blobs, and any
  peds.cpp/game.h change as graft text. Drafts: AW/signoff_set34_final.txt, AW/signoff_set56_final.txt.
- nt_ai75 (set 7) frozen 20:57:09 (ride-along, riders, look hook: crossing/bus/alarm/heli glances, crossing + rideoff tests). Queued crossing + rideoff on nt_ai75 after the nt_ai62 runs.
- Set 8 (after the nt_ai75 freeze): more lines in the most-heard bark banks (greetings, bumps, phone chat, honks, filming, dives, guns seen, joggers, tourists, music, drunks, crashes, insults, flee, cower, help, thanks) - text only.
- Set 8: ai.forceGreet (walkby test: every close pass gets its word, important bark) - nt_ai72's walkby showed greetings 0 / asked okay 0 by t=110 (10%/35% by design, and the global bark limit drops unimportant ones).
- Set 8: officers busy at a statement/stop/ticket (BRAIN_GOTO -4..-6) or a crash scene (ACT_EVENT) say "step back, please" when the player stands within 2.4 m for 3.5 s (once per 30 s; ai.cpp updateBrain; BK_COP_STEP_BACK; census "step backs").
- 21:30 walkby on nt_ai72: greetings 0, asked okay 0 in 150 s (health held at 60/200 from t=75). Cause found in the shots: phone talkers near the player said a line at every free moment (BK_PHONE_CHAT checked every frame), taking the street's bark slot (barkGlobal), so greetings were dropped and greetedT used up the pass. Set 8: phone lines once per 5 s per talker (walkers and stood), a greeting drowned out retries within the pass, forceGreet in the walkby test.
- Set 8: riders bailing from a car the player took say a BK_FLEE line (nt_ai75 used BK_CARJACKED: 'That's my car!' - wrong for a passenger).
- Set 8: two waiting at a bus stop side by side (within 2.6 m) with the player within 14 m: now and then a word about the bus and the answer (BK_BUS_WAIT / BK_BUS_WAIT_REPLY via the reply mechanism).
- nt_ai76 (set 8) frozen at 21:31:50: queued walkby, pedstop, busrun at the end.
- 21:40 hurt on nt_ai72: pass (cop aid 1, the EMS radio line, ambulance from 185 m at t=57, medic, up at t=73, in by t=95).
- nt_ai76 built 22:10. Set 9 (shared tree after 21:31:48): the player knocked flat (PS_RAGDOLL) -> AIState::playerDownT; civilians within 14 m in sight gasp (BK_PLAYER_DOWN) once per knock-down (PedAI::plDownSeenT); the nearest steady one (temper != 0, within 11 m) comes over (ACT_WATCH at 1.4 m, PedAI::plHelper, AIState::playerHelper) and asks 'are you okay?' once the player is up (askOkay++); others stop for a look (ACT_INSPECT). No cheering clip for the helper.
- Set 9: drivers who knock the player flat (vehicles.cpp makes the driver the attacker; ai.cpp detects the player in
  PS_RAGDOLL within 0.25 s of the hit by an AI driver at the wheel): seven in ten stop (VehAI::rage=1 + rageSorry, no
  horn), get out (traffic.cpp; PedAI::rageSorry) and come over (pedai.cpp ACT_ROADRAGE's sorry branch: run over,
  crouched beside them while down, BK_DRIVER_SORRY every 4-6 s, back to the car 3.5 s after the player is up).
  Census "sorry drivers". Test mode "hitplayer" (ai.forceSorry; the player put ~0.45 s ahead of a car at speed, twice).
- nt_ai77 (set 9) frozen 22:27:09; queued hitplayer at the end.
- 22:47:10: nt_ai77's tree refreshed again (pedai.cpp: a waiting prisoner walked off a crossing / out of the road to the kerb before sitting) while its build waited on the build lock.
- nt_ai76 dropped: its runs (walkby, pedstop, busrun) moved to nt_ai77 (sets 8+9 sign off together).
- nt_ai75 dropped too: crossing and rideoff moved to nt_ai77 (sets 7-9 sign off together on nt_ai77).

## Sign-off plan (22:58)
- sets 3+4: nt_ai62 blobs (AW/blobs_nt_ai62.txt) + couple/reach blocks (the lead has them: ai_hand_blocks_v093.txt).
  Results so far in AW/set34_results.txt; remaining runs: events (waiting), night, crowd, soak. Final text:
  AW/signoff_set34_final.txt + set34_results.txt. Propose to the lead: sets 5-9 could go in as two grafts (nt_ai74, then
  nt_ai77) or one (nt_ai77) - nt_ai77 contains everything.
- sets 5+6: nt_ai74 blobs (AW/blobs_nt_ai74.txt) + look_block_74.txt; AW/signoff_set56_final.txt + set56_results.txt.
- sets 7-9: nt_ai77 blobs (AW/blobs_nt_ai77.txt, pedai.cpp blob refreshed twice in the frozen tree before it compiled)
  + the extended look block (AW/look_block.txt); AW/signoff_set79_draft.txt.
## Backlog ideas (not started)
- NPC victims hit by AI cars: the driver stops and checks (the sorry flow for NPCs).
- A tourist walking up to the player to ask the way ("Never mind, thanks!") - events.cpp.
- Bystanders reacting to the player's car hitting street furniture.
- Nightlife groups' own stroll lines (laughing, loud).
- 00:35 sets 3+4 sign-off sent to main (AW/signoff_set34_send.txt). nt_ai62 deleted. Next: nt_ai77 crossing/rideoff, then the nt_ai74 set-5/6 runs, then nt_ai77 walkby/pedstop/busrun/hitplayer/life/soak.
- 00:42 the lead: sets 3+4 go in as snapshot 33 (their peds.cpp graft = blob 9d57bb5468f1284ca22742406c6461b339441c4b:
  HEAD's peds.cpp + couple block + reach block before the foot IK line). Sets 5-9: ONE graft from nt_ai77 once its runs
  pass, based on HEAD + sets 3+4 incl. that peds.cpp; my look block goes after the reach block before the foot IK line
  (the animation agent's pass-by fix also lands in peds.cpp - keep my blocks in their own spot). All runs moved to
  nt_ai77; nt_ai74 deleted. Queue order: new features first (rideoff, hitplayer, walkby, pedstop, alarm), then the set
  5/6 tests, then life and soak. Sign-off draft to build: AW/signoff_set56_final.txt + AW/signoff_set79_draft.txt merged.

## 01:00 container restart (~00:57; disk kept, every process killed)
- The nt_ai77 crossing run died at t=84.4; its log is saved as SP/log_crossing_nt_ai77.txt. Result: pass, with
  step-offs 0.40/0.67/0.83/0.90 s after the light and up to 3 of 4 waiting looking along the road. Not rerun. BMPs deleted; m77x.png kept.
- Queue runner relaunched at 01:01 (PID in AW/queue_runner.pid, appending to AW/run_queue.log). Command:
  `AW/ai_detach.sh /dev/null bash -c "exec AW/ai_queue.sh >> AW/run_queue.log 2>&1"`. Queue: rideoff, hitplayer, walkby,
  pedstop, alarm, panhandle, busrun, jaywalk, crowded, followed, nearmiss, rain, crashscene, life, soak (all nt_ai77).
- main = 8fb1e66 (snapshot 32, city batch 5). It touches nothing in src/game, so the nt_ai77 blobs still replace HEAD's
  files cleanly. Snapshot 33 is being redone with sets 3+4, the peds.cpp graft 9d57bb54 and faces batch 4.
- The sets 5-9 sign-off draft is AW/signoff_set59.txt. [RESULTS] is still to be filled in from the nt_ai77 runs.
- Set 10, in progress in the shared tree:
  - tip-offs: civilians point searching officers to the wanted player.
  - the player is asked the way.
  - drivers who knock down a pedestrian stop to check on them.
  - nightlife stroll lines.

## Set 10 (shared tree, after the nt_ai77 freeze; syntax check pending)
- Tip-offs (police.cpp police_tip, pedai.cpp ACT_TIP_OFF):
  - Civilians who see the wanted player record when, where and which way he went (PedAI::sawPlT/sawPlAt/sawPlVel; pedai.cpp perception, within 35 m, in sight).
  - Trigger: the police have lost him for over 4 s, and an officer on foot is within 16 m of such a witness. The witness's sighting must be newer than the police's last one.
  - The witness calls out (a wave from more than 6 m off; BK_TIP_OFF, or BK_TIP_HERE if the player is still in their sight), then points the way (stance 17).
  - give() then sets pinfo.lastSeenPos/Time to the sighting, sets gD.lastSeenVel, takes 0.2 off the wanted cooldown and resets the officer's tactic timer. The officer looks along the point and says BK_COP_TIP.
  - Never the timid, half of the rest; nobody within 5 m of the player. Each witness tips once per 120 s, and tips are 25-45 s apart overall.
  - Test mode "tipoff": 2 rounds. Officers within 150 m are sent 220 m off; the player is made wanted and run 10 m along the sidewalk, then put 600 m away; the beat lead is brought 9 m from the freshest witness.
- Asked the way (pedai.cpp aiStreetMeets, meet kind 2, askPlayerStep):
  - Who: someone walking the player's way while the player stands or strolls (under 1.2 m/s, not wanted, empty-handed, no mission). Tourists 35%, others 4% per half-minute window. Asks are 4-8 min apart.
  - Flow: they walk up and ask (BK_ASK_WAY). The first time, the hint "Stand still to point the way" shows.
  - Player still within 3 m for 1 s: the player does CLIP_POINT, the asker looks that way and says thanks with a wave.
  - Player walks off: BK_ASK_SNUBBED.
  - Test mode "askway": the first ask is answered; on the second, the player is moved 6 m off.
- Drivers who knock down anyone on foot (ai.cpp: the detection now loops over all peds; VehAI::rageFor/rageForUid/knockT; traffic.cpp checks the victim's distance; PedAI::sorryFor/sorryForUid/sorryCalled):
  - Victim gets up: they say BK_HIT_ANGRY at the driver.
  - Victim stays down (ACT_HURT or dead): the driver phones for an ambulance (BK_DRIVER_CALL, stance 8) and stays 80 s (40 s if the victim is dead) or until a medic arrives.
  - Sorry drivers ignore every stimulus except gunfire and explosions.
  - Census "others hit". Test mode "hitped": the first victim is made tough (400 hp); the second is cut to 15% health when it lands.
- Night chats: walking pairs between 21:30 and 04:30 use BK_NIGHT_CHAT/REPLY. Census "night chats".
- Siren looks: within 38 m, 60% of calm civilians turn their heads to a siren passing at over 4 m/s, once per 20 s. They follow it for up to 4 s (PedAI::sirenLookT/sirenVeh). Census "siren looks".
- The sign-off draft's blob list was missing ai_game.h (a filter bug); fixed in AW/signoff_set59.txt. All 13 blobs and 9d57bb5 have been checked as present in the object store.

## 02:10 status
- nt_ai77 runs:
  - rideoff: pass.
  - hitplayer: the sorry drivers and the gasp work. Bug: the helper gave up as the player slid. Fixed in the main tree; the sets 5-9 pedai.cpp blob with the fix is f6363200ec6c4ef56fa3eef6d4afe7a3cd75a9cf (from AW/s59fix/pedai.cpp).
  - walkby: OOM-killed at t=86 (the coordinator confirmed someone else's ASan compile caused it). Requeued at the top. Greetings worked (5 by t=50).
- nt_ai78: built, but replaced before any run. nt_ai79 (sets 3-10 + the helper fix) built at 02:02. Its hitplayer runs first (the fix check), then walkby on nt_ai77, then the rest of the nt_ai77 queue, then the nt_ai79 set 10 tests (tipoff, askway, hitped, night).
- Results so far: AW/set59_results.txt. Sign-off draft: AW/signoff_set59.txt. When sending, swap in the pedai.cpp blob f636320 and mention the fix.
- Main sent a status message at 01:55.

## Set 11 (main tree after the nt_ai79 freeze at 01:51:31)
- Officers asking around (police.cpp police_tip::asking, hooked into FT_SEARCH, and into FT_ARREST when the player is not seen):
  - Trigger: the suspect lost for 8 s or more and the gap (copAskGap, 30-55 s) over. The officer picks a calm, upright civilian within 7 m and in front of them, walks up, and asks (BK_COP_ASK_SEEN).
  - The civilian (ACT_TIP_OFF with tipAsked) listens, then answers once the question is said:
    - saw the player since the police last did, within 60 s: points the way (give()).
    - otherwise: BK_NOT_SEEN and a talk gesture (step 3).
  - PedAI::copAsk/copAskUid/copAskT/copAskSaid; census "cop asks".
  - Test mode "copask": the player made wanted (2 stars held) runs 10 m in sight of people, then lies low 300 m off. The officers search where he was.
- Syntax check pending (AW/syntax_set11.out).

## 02:37
- Sets 3+4 are merged: PR #33, main = d3c2c3f. Its AI files are exactly my sets 3+4 blobs, and its peds.cpp is 9d57bb5. Base the sets 5-9 sign-off on d3c2c3f (the draft says so now).
  - The wildlife blobs are d3c2c3f plus 12 additive lines.
- nt_ai79 hitplayer verified the helper fix:
  - Stage 2: the helper hurried over and asked "are you okay?".
  - Stage 1: the helper stayed, but a parked car between them blocked the way.
- Set 11, part 2 (main tree): ai.cpp ai_cardetour::detour. aiWalkRound now goes round parked, stopped or crawling cars, unless the goal is at that car. It is used by:
  - the helper and ACT_AID walk-ins
  - the sorry driver's approach
  - the asking officer's approach
  Syntax clean.
- nt_ai80 frozen at 02:35:59 (sets 3-11). The queue has the nt_ai80 lines after the nt_ai77 ones: tipoff, askway, hitped, night, copask, hitplayer.
- nt_ai79.exe can be deleted once its hitplayer run has ended.
- Set 12 (main tree after the nt_ai80 freeze at 02:35:59):
  - aiWalkRound now also used by road-rage drivers (to the player's window, and back to the car), delivery drivers (to the door and back) and EMS crews (back to the ambulance).
  - The car detour also treats the target car as an obstacle when the goal is a door on its far side.
  - Syntax check pending.
- Set 12 also: parking tickets on the player's car.
  - What gets ticketed: a car the player left in the street (playerUsed) more than 60 s ago, with the player over 25 m off and no mission running. police_ticket::ticketable allows it; nobody comes hurrying for it.
  - VehAI::plLeftT is set when the player gets out (ai.cpp tracks AIState::plVehPrev).
  - When the ticket goes under the wiper, VehAI::plTicket is set. Getting back in costs $45 and shows notify("Parking ticket", "-$45"). Census "player tickets".
  - Test mode "plticket": forcePlTicket; the player is put back in the car 4 s after the ticket is on.

## 03:46
- nt_ai77 results so far (AW/set59_results.txt):
  - pass: crossing, rideoff, hitplayer (fix checked on nt_ai79), walkby (rerun after the OOM kill).
  - pedstop: stage 2's walk-up got stuck. This is pre-existing; to recheck with the car detour. Its test's step-back crowding is fixed in the main tree's app.cpp.
  - alarm: running.
- Main tree fixes since the nt_ai80 freeze:
  - The car detour no longer loops round a car whose middle is the goal (walking up to get in). This was never built: nt_ai80 has the plain skip.
  - The pedstop test crowds the officer only once they are with the stopped ped.
- Plan: freeze nt_ai81 (sets 3-12) when the nt_ai77 queue is nearly done. Retarget the nt_ai80 lines to nt_ai81 and add plticket, pedstop and alarm (the car detour). Then delete nt_ai80.exe.
- 04:20 Set 13 (main tree after the nt_ai81 freeze at 03:45:38): police_stop::pickSubject skips anyone across the street, sampling the line between officer and subject at 1/4, 1/2 and 3/4 for a lane. The nt_ai77 pedstop stage 2 subject was likely across the road; the test camera framed the middle of the road, 13 m between them.
- 04:55 busrun on nt_ai77, the first ever run of the bus-run test: bus 1 drove past both stops on the lane at 13 m/s, most likely because it had moved out of the kerb lane for a turn on its route. Bus 2 stopped (at the earlier stop, 28 m before the test's) and a runner went for it.
  - Set 13 fix (traffic_core.cpp laneChangeLogic): a bus with an unserved stop ahead in its kerb lane re-routes from that lane instead of making a mandatory lane change.

## 05:07 exact-graft build
- jaywalk on nt_ai77: nobody ever crossed. The mid-block crossing check counted every car parked along the street within 45 m as "coming", so jaywalking (and set 5's officers calling to jaywalkers) never happened on streets with parked cars. Fix in pedai.cpp (main tree and the graft): parked cars count only where they stand across the way over.
- The sets 5-9 pedai.cpp blob with both fixes (helper + jaywalk) is c456b4736d21e6917a94cb973e43bc52345232c0.
- AW/ai_buildgraft.sh COMMIT BLOBLIST OUTEXE builds exactly a graft: git archive of the commit, plus the blobs from the list, plus the missing hand blocks. AW/graft59.txt is the sets 5-9 list.
- nt_ai77f = d3c2c3f + graft59 + the look block. Its peds.cpp blob is 0b5009d3b7aeec9d84a4ba468e765d90ce468cb5, which is what the lead's peds.cpp becomes after the look block.
- The remaining sets 5-9 runs moved to nt_ai77f: jaywalk, hitplayer, nearmiss, rain, crashscene, life, soak. followed stays on nt_ai77 to keep the runner busy while 77f builds.
- 05:27 main = cda29a1 (PR #34). It changed peds.cpp to 7aaad25 (the pass-by fix) and anim/face.cpp; my files are untouched.
  - nt_ai77f was restarted as a graft on cda29a1 (the d3c2c3f build was stopped before it compiled).
  - cda29a1's peds.cpp with the look block is b24369838a3b7ae9500e4ec18e21a5372d9146c5: +34 lines, inserted after line 919, the end of the reach block, before the foot IK line.
  - The sign-off draft is updated for the new base.
- 07:20 nearmiss on nt_ai77f: the flee-mode car only reached 11.5 m/s, under the player's 15 m/s threshold, so there were no near misses and the feature was not exercised.
  - Set 13 test change (app.cpp, nearmiss mode): the kerb lane on a 150 m run, and four people stood right at the kerb 70-115 m ahead, out from the lane's middle by the car's half width + 1.2 m (inside the start band, outside the dive band).
- nt_ai82 (sets 3-13, frozen 05:42:41, built 06:33) does NOT have this test change, nor the nearmiss change. A later build (nt_ai83) will.

## 08:45
- nt_ai77f life: pass (results in AW/set59_results.txt). soak on nt_ai77f started 08:36. Once it passes: fill [RESULTS] in AW/signoff_set59.txt and send it.
- main is now b907098 (PRs #35-37). Nothing in src/game changed since cda29a1, and peds.cpp is still 7aaad25, so the sets 5-9 graft and b243698 apply unchanged. Say so in the sign-off.
- The untracked repo-root file "--help" is a 900x900 PPM from 03:40. It is not mine; leave it.
- Set 14 (main tree, after the nt_ai83 freeze): the street watches the player's car.
  - Screech looks: ai.cpp fills AIState::screech every frame with cars within 80 m of the player whose tyres have screeched for at least 0.25 s (VehAI::screechT; slip > 0.55 at over 4 m/s). pedai.cpp turns heads to them and follows them through sirenVeh, with screechLookT and an occasional BK_SCREECH. Census "screech looks".
  - Car show: ai.cpp AIState::showT/showOn/showId/showAt/showVeh. The show starts after 2 s of the player's car spinning (yaw rate > 1.1 at over 2 m/s) or smoking (rear slip > 0.6 under 9 m/s) on the ground, with no wanted level and no mission. It ends when the car leaves 40 m or stops for 4 s.
    - pedai.cpp: civilians within 36 m with line of sight decide once per show (PedAI::showSeen). The steady or business sort tut (BK_SHOW_TUT) and hurry on. Up to 75% of the rest become ACT_WATCH with plShow (a step back if closer than 9 m); the bold film (stance 8).
    - Watchers turn to follow the car, step out of its path, cheer or point every 2.5-6.5 s, and give one shout at a time (BK_SHOW_CHEER). When the show ends, they leave 1-4.5 s later, one in three with BK_SHOW_END.
    - Officers on foot within 45 m call out (BK_COP_SHOW) and go over for a look (ACT_INSPECT). No wanted level.
  - Census "car shows (watchers filming tuts cop calls)".
  - Test mode "showoff": a muscle car in the nearest signalled 4-way street crossing does 18 s of donuts (donutControls on the player's controls), brakes and waits 24 s, then drives off with the traffic driver and does a handbrake slide once over 10 m/s.
  - Syntax check pending (AW/syntax_set14.out).
- 08:50 SETS 5-9 SIGN-OFF SENT to main (AW/signoff_set59_send.txt; results AW/set59_results.txt). soak passed. All nt_ai77f runs are done. nt_ai77f.exe can be deleted once the lead confirms the graft.
- Set 14 syntax is clean (AW/syntax_set14.out).
- Next: the nt_ai83 runs (sets 10-13), now running: tipoff, askway, hitped, night, copask, hitplayer, plticket, pedstop, alarm, busrun, jaywalk, nearmiss. Results go to AW/set10_results.txt; the sign-off draft is AW/signoff_set10_draft.txt.
- 08:52 The coordinator accepted sets 5-9: all 13 blobs are in git, and snapshot 38 (tree e552036e = b907098 + them) is building. Base sets 10-13 on b907098 + the sets 5-9 blobs; the peds.cpp base becomes b2436983 once 38 merges. nt_ai77f.exe is deleted.
- Door-entry hooks (the coordinator asked): none of my sets adds one. AI exits already use removePedFromVehicle(id, true), which goes through peds.cpp startCarDoorClip. AI entries don't: ACT_DRIVE_OFF, ACT_CUFFED and ACT_RIDE_ALONG queue a plain CLIP_ENTER_CAR_L/R and warp in after 1.05 s; the other entries warp in with no clip. I proposed a public `float GameWorld::startCarEntry(int ped, int veh, int seat)` (a wrapper over the static startCarDoorClip, -1 if no door) and offered to switch every entry site once it lands. Waiting for its name.
- The sets 10-13 sign-off must use AW/blobs_nt_ai83.txt, NOT the main tree: the main tree now also has set 14.
- 08:53 The coordinator asked the animation agent for `float GameWorld::startCarEntry(int ped, int veh, int seat)` (peds.cpp, declared in gameworld.h); it comes with their next sign-off. Do NOT switch the AI entry sites until the coordinator says it is merged.
- 08:54 Set 14 part 2 (main tree; syntax clean, AW/syntax_set14b.out): the player's car standing over a crosswalk or up on a sidewalk.
  - pedai.cpp perception: a civilian crossing on that crosswalk (WS_CROSSING on a WL_CROSSWALK or WL_ZEBRA whose stripes run under the car), or walking a sidewalk while ai.plCarOnSidewalk is set, comes within 1.4 m of the stopped car's outline.
  - They glare at the player (lookPed) and say a word, by temper: 90% bold, 50% mid, 20% timid. The words are BK_WALKING_HERE / BK_SIDEWALK_CAR.
  - The bold with free hands slap the hood: a reachAt at hood height for 0.45 s, with SFX_IMPACT_METAL at 0.35 played 0.3 s later via PedAI::slapT. Gated per ped by PedAI::hoodT, once per 30 s.
  - ai.cpp: AIState::plCarOnSidewalk is set when the stopped car's middle is inside a WL_SIDEWALK band (nearestWalk |lat| < halfWidth).
  - Census: "crosswalk words (hood slaps) sidewalk car words".
  - Test mode "crosswalk": a sedan over the nearest signalled crosswalk from t=6, moved up onto the sidewalk by that corner at t=60.
- nt_ai84 (sets 3-14 + hands) frozen 08:54:29; blobs in AW/blobs_nt_ai84.txt; build log AW/build_nt_ai84.out. Its showoff and crosswalk tests are queued after the nt_ai83 lines.
- 09:10 Set 14 part 3 (main tree, after the nt_ai84 freeze; NOT yet syntax-checked, because my nt_ai84 build is going and only one compile of mine runs at a time; check it after nt_ai84 is built):
  - The show build-up must stay within 15 m of where it began (AIState::showBuildAt), so a couple of hard corners never make a show.
  - Run past: the player on foot, not wanted, at over 4.5 m/s within 2.3 m. The person looks after them; civilians say BK_RUN_PAST, 45% of the bold and 25% of the rest. Gated by PedAI::runPastT, 20 s. Census "run pasts".
  - Night avoidance: between 22:00 and 05:00, a lone civilian walker (no companions) on a quiet street (nobody else on foot within 25 m) meets the player head-on, 7-20 m off. If they are wary (the timid, 45% of women, 12% of the rest) and overTheStreet allows it, they cross to the other side (ACT_CROSS) with a look back. Gated by PedAI::avoidT, 90 s. AIState::forceAvoid is for tests. Census "night avoids".
  - pedai_detail::overTheStreet() is the jaywalk crossing check, factored out (same logic). The jaywalk site uses it.
  - Test mode "runby": sprinting the downtown sidewalks for 60 s, then put on a Northside sidewalk near (2470, 3880) at 23:30, walking with forceAvoid.
- 09:22 Plan for when startCarEntry lands (do NOT apply before the coordinator says it is merged):
  - Add one helper in ai.cpp, aiBoardCar(g, id, veh, seat). On the first call at the door: float len = g.startCarEntry(id, veh, seat); if len <= 0, fall back to the plain CLIP_ENTER_CAR_L/R + SFX_CAR_DOOR_OPEN with len 1.05. Store PedAI::boardT = time + len. Return true (warpPedIntoVehicle done) once time >= boardT.
  - Convert the entry sites to it:
    - pedai.cpp: ACT_DRIVE_OFF, ACT_RIDE_ALONG, ACT_CUFFED (rear door), the ACT_COP_BREAK return, taxi fares (walk to the rear door), EMS crews back in, road-rage and sorry drivers back in, errand drivers.
    - police.cpp: officers back to their cars (~3371/3399/3451).
    - events.cpp: actors walking back to their cars (~1771/2494/2581/2788).
    - ai.cpp: ~1202.
    - Spawn-time warps (partners put into new cars) stay as they are.
- 09:22 The machine is very loaded (3 other games plus compiles): tipoff on nt_ai83 is running at 40-50 s wall per game second. The 12 nt_ai83 tests will take many hours unless the load drops.
- 09:35 tipoff on nt_ai83 failed because of the TEST: it moved the player 600 m off and the officers 220 m off, and the population recycles peds more than 150 m from the player, so every witness and the officer vanished. copask (300 m) has the same flaw.
  - Fixed in app.cpp: ai_tests::hiddenSpot puts the player 85-105 m off and out of sight; the kept officer is persistent for the round; the camera stays on the run spot meanwhile.
  - The sets 10-13 app.cpp with the fix is blob e2d4a5c (AW/s1013fix/app.cpp). The graft list is AW/graft1013.txt (= blobs_nt_ai83 with that app.cpp).
  - nt_ai83f (b907098 + graft1013) started building at 09:34 (AW/build_nt_ai83f.out). Its tipoff and copask are at the top of the queue; copask was dropped from the nt_ai83 lines.
  - The sets 10-13 sign-off will cite graft1013 (nt_ai83f). Results so far: AW/set1013_results.txt.
- nt_ai84 was built at 09:30 (sets 3-14 part 2, without the tipoff/copask test fixes and without part 3).
- 09:45 Set 14 part 4 (main tree; not yet syntax-checked): car radio reactions. The player's car is stopped close by (under 1.5 m/s, within 9 m) with vehicles[pv].radio >= 0. People standing still react, gated by PedAI::musicT (45 s):
  - Lively (beach, tourist, bold, or 20:00-03:00): 60% dance 4-8 s (PedAI::vibeT; stance 9 while still, set at the end of aiCivilianBrain), sometimes saying BK_GOOD_SONG. 25% of the rest dance.
  - Business and timid: look over; 35% say BK_LOUD_MUSIC.
  - Census "radio dancers / radio complaints".
  - The crosswalk test now has three stages: short of the stripes with the radio on (6-32 s), over the stripes (32-64 s), up on the sidewalk (64-110 s).
- 10:05 SETS 5-9 MERGED: snapshot 38 ea94690, PR #38; main = e4efe94 (also the main tree's HEAD now). HEAD's peds.cpp is b2436983. Base sets 10-13 on e4efe94; peds.cpp hunks go against b2436983.
  - e4efe94 = b907098 + exactly the sets 5-9 files, so nt_ai83f (grafted on b907098 + graft1013) is content-identical to e4efe94 + the 8 sets 10-13 blobs: ai.cpp 73912f4, ai_game.h 9562392, app.cpp e2d4a5c, barks.cpp 5060b5d, pedai.cpp 57ce67e, police.cpp 77d62c2, traffic.cpp 2112f63, traffic_core.cpp d716924.
  - Since 09:49 the animation agent has uncommitted WIP in the shared tree: src/anim/animator.cpp, character.h, src/game/peds.cpp, and gameworld.h (the startCarEntry declaration). So ai_buildtree.sh would freeze their WIP. From now on, build my test exes with ai_buildgraft.sh on e4efe94 plus my AI blobs only. nt_ai84 (frozen 08:54) predates their WIP and is clean.
  - Entry sites stay as they are until the coordinator gives the merged startCarEntry name.
- 10:15 Snapshot 39 (the animation sign-off) is queued. It changes peds.cpp from b2436983 to 5c6e2ff79d7d1b37c488189789bf4a0a73b90282 (grabAxis, ground scan, foot-probe clamps, and GameWorld::startCarEntry defined before warpPedIntoVehicle). Do not use the new API until the coordinator confirms the merge.
  - The API: startCarEntry(ped, veh, seat) puts the ped at the door spot and queues the door clip; it returns the length or -1. Hold the ped there (PS_ENTERING, which ai.cpp already freezes), then call warpPedIntoVehicle when the time is up; that closes the door and clears doorVehicle.
  - I sent the couple-block hunk: `in.grabAxis = vec3(0.f, 1.f, 0.f);` after `in.grabWeight = 1.f;` in the hand-in-hand block. It goes in with or after 39. Sets 10-13 have no peds.cpp hunks.
- 10:21 The coordinator: keep the grabAxis line OUT of sets 10-13. Once 39 is confirmed merged, put it in the NEXT set together with the startCarEntry switch, built on the merged main, and check held hands and door entries in game. Sets 10-13 sign off on e4efe94 (the coordinator grafts onto 39 if it merges first). Sets 10-13 don't change gameworld.h; say so in the sign-off.
- 10:22 hitped on nt_ai83 failed because of the TEST: the victim dived clear (the dive code), so nobody was hit. Fix: qa.diveCooldown = 3 s in the hitped test (main tree + AW/s1013fix/app.cpp). The sets 10-13 app.cpp blob is now 9b43215 (graft1013.txt). The nt_ai83f build was stopped (it was still waiting in build.sh) and relaunched as a graft on e4efe94 at 10:22. tipoff, copask and hitped are queued on nt_ai83f.
- 10:50 night on nt_ai83: clean, but no night chats (downtown empties at 23:00, so no pair passed within 8 m). New test mode "nightwalk" in the main tree: the walkby walk on Ocean Promenade (5352, 860) at 22:30. walkby's branch is shared, and the hurt part is walkby only. Run it on the next build (nt_ai85) as the night-chat check.
- Next build, nt_ai85: ai_buildgraft.sh on e4efe94 with the main tree's AI files (sets 10-15 + test fixes); NOT ai_buildtree, because of others' WIP. Its tests: showoff, crosswalk (3 stages), runby, nightwalk.
- 11:20 nt_ai83 results so far (AW/set1013_results.txt): askway pass; hitplayer pass (the car detour works, asked okay 2); night clean but no chats (nightwalk will check); tipoff and hitped were test failures, fixed and requeued on nt_ai83f. plticket is running now. The nt_ai83f build is still waiting for memory (memfree around 2000; the build gate is 3200).
- 11:25 nt_ai83f built at 11:24. Its tipoff, copask and hitped are at the top of the queue.
- nt_ai85 graft launched 11:24: e4efe94 + AW/graft85.txt, which mkgraft.sh writes from the main tree's AI files that differ from e4efe94 (sets 10-15 + the test fixes + nightwalk). Its tests are queued at the end: showoff, crosswalk, runby, nightwalk. nt_ai84 was deleted unused (superseded).
- 11:55 SNAPSHOT 39 MERGED: main = b49bc7f, peds.cpp 5c6e2ff; startCarEntry and AnimInput::grabAxis are in. SET 16 is written in the main tree:
  - ai.cpp namespace ai_board (begin / boarded / step):
    - begin: startCarEntry, then PS_ENTERING, p.targetVehicle/targetSeat set, PedAI::boardVeh/boardVehUid/boardSeat/boardT, and pa.clipTimer = len (so police.cpp escortStep stands over a prisoner).
    - step: called from updateBrain's PS_ENTERING branch. It keeps the car's escortHold up; when the time is up it calls warpPedIntoVehicle, then boarded(). If the car is gone, moving or the seat is taken, the ped goes back on foot with clipTimer = -1.
    - boarded: each activity's post-warp follow-up, moved there from the sites: DRIVE_OFF, RIDE_ALONG, CUFFED, ERRAND, ROADRAGE, EVENT (EMS crew), COP_BREAK (the everybody-in check now counts PS_ENTERING too), HAIL_TAXI, and police returning (BRAIN_GOTO -2).
  - Sites converted, each falling back to the plain clip or warp on -1:
    - pedai.cpp: DRIVE_OFF, CUFFED, RIDE_ALONG; ERRAND; ROADRAGE (now walks to the driver's door); EMS crew (walks to the free seat's door); COP_BREAK; taxi fare (kerb-side back seat, walks to its door).
    - police.cpp: FT_RETURN (walks to the seat's door; PedAI::doorWalkT, falling back after 8 s).
  - Left instant: spawn warps; police.cpp 3371 (bringing the car round to an escort) and 3451 (a chase); events.cpp's actor warps; ai.cpp BRAIN_FOLLOW.
  - Followers now see a leader at its door: the rc check and drvIn accept PS_ENTERING with targetVehicle.
  - Census: "door entries (plain)".
  - peds.cpp test blob dbe0046 = 5c6e2ff + the couple-block `in.grabAxis = vec3(0.f, 1.f, 0.f);` line, as a hunk for the lead.
- nt_ai85 (sets 10-15) was built 11:46. nt_ai86 = b49bc7f + graft86 (sets 10-16 + peds dbe0046) was grafted 11:51. Queued on nt_ai86: rideoff, couple, copbreak.
- 12:05 The coordinator: set 16 looks right. Next, set 17 = the events.cpp actors' entries (keep the chase jump-in and the bring-the-car-round instant; BRAIN_FOLLOW only if cheap).
  - Sign-off order: sets 10-13 FIRST (snapshot 40 is waiting on them, with city batch 10), then set 16 (rideoff + couple on nt_ai86, then copbreak).
  - Set 16 sheets need a held-hands frame or two and a door-entry frame; report the hand clip and door timings seen.
  - jaywalk was dropped from the nt_ai83 queue (sets 10-13 don't touch jaywalking; it passed on nt_ai77f).
  - Queue order now: nt_ai83f copask, hitped; nt_ai83 pedstop, busrun, nearmiss, alarm; nt_ai86 rideoff, couple, copbreak; nt_ai85 showoff, crosswalk, runby, nightwalk.
  - Sets 14-15 have not been mentioned by the coordinator yet; propose signing them off together with 16 (nt_ai86 holds 10-16).
- 12:15 SET 17 written (main tree, after the nt_ai86 graft; not yet syntax-checked): event actors through the door.
  - ai_board::beginThen(g, id, veh, seat, then), where PedAI::boardThen means: 0 the activity's follow-up; 1 away at the wheel (DRIVER, unparked, engine on, attachTraffic); 2 nothing (events.cpp carries on when it sees them in). begin() = beginThen(..., 0). boarded() reads and resets boardThen; the cancel path also resets it.
  - events.cpp: EV_CRASH (both drivers back to their cars; skip while PS_ENTERING; beginThen 1 at the door), EV_BREAKDOWN (the driver; role VR_TRAFFIC set once in), EV_TRAFFIC_STOP ST_D (the officer; beginThen 2; the event's post-warp code runs once it sees the officer in).
  - Kept instant: EV_TAKEOVER runners (fleeing the police), the arrested crook put in the cruiser (2214 - a 25 m put-in; needs the escort system, later), spawn warps.
  - BRAIN_FOLLOW NOT converted: a mission follower would be left behind if the player drove off during the clip. Tell the coordinator.
  - Test it on the next build (nt_ai87, after nt_ai86) with crashscene; an events or breakdown run if possible.
- 12:16 FUTURE (from the coordinator; not now). The ped-ped separation in peds.cpp updatePeds pushes positions apart but leaves p.vel alone, so a walker held up behind someone keeps its speed. Their feet drag, and aiWalkRound's stuck test (ai.cpp ~161, length(p.vel) < 0.15) never fires.
  1. The animation agent fixes the physics side first: the separation removes the closing velocity. When they sign off, the coordinator will ask me to run crowded, crossing, walkby, life and busrun on their exe; watch queues, crossings and companions walking abreast.
  2. Then mine (after set 16 and the events.cpp actors): BRAIN_GOTO walkers ease off behind a slower walker ahead, as pednav's social force does on sidewalks, instead of pressing into them.
- 12:45 tipoff rerun on nt_ai83f: PASS in round 2 (call, point, police last-seen moved; tip-offs 1). nt_ai86 built 12:40. nt_ai87 (set 17: b49bc7f + graft87 + peds dbe0046) grafted 12:42; its crashscene and events runs are queued after nt_ai86's copbreak.
- 12:48 Plan: sign off SETS 14-16 together on nt_ai86 (graft86 = b49bc7f + sets 10-16 AI blobs + peds dbe0046). Its runs: rideoff, couple, copbreak (set 16), then showoff, crosswalk, runby, nightwalk (sets 14-15; moved from nt_ai85, which is deleted). Set 17 signs off after that on nt_ai87 (crashscene, events).
- 15:35 SETS 10-13 SIGN-OFF SENT (AW/signoff_set1013_send.txt; results AW/set1013_results.txt). Blobs: ai.cpp 73912f4, ai_game.h 9562392, app.cpp 9b43215, barks.cpp 5060b5d, pedai.cpp 57ce67e, police.cpp 77d62c2, traffic.cpp 2112f63, traffic_core.cpp d716924 (on e4efe94; they apply to a2cd96b unchanged). Known gaps reported: hitped victim/driver talk, nearmiss test lane choice. nt_ai83 and nt_ai83f can be deleted once the coordinator confirms the merge (or at wind-down).
- 15:40 The coordinator verified sets 10-13; they are going in as snapshot 42 (tree 2d7111e7), with the hitped gap and night coverage noted. The sets 14-16 sign-off must list the files it shares with sets 10-13 (its blobs hold 10-13 too). graft86: ai.cpp 51ccb87, ai_game.h 036e52e, app.cpp d8ab90d, barks.cpp 5772084, pedai.cpp b79602b, police.cpp 27a7a43; traffic.cpp 2112f63 and traffic_core.cpp d716924 are the same as the 10-13 blobs; peds.cpp dbe0046.

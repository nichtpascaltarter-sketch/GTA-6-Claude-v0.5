# Save point of the pause on 2026-10-29 (about 15:20 UTC)

Not for merging. main at the pause: 9b36279 (snapshot 105). No pull request was open and nothing was running.

What is here (a curated set; the earlier full save point is this commit's parent):
- `_save/scratchpad/`: the lead's scripts (snapshot flow, showcase renders, PC queue), its check-in notes
  (`lead/checkin_state.md`) and every agent's resume notes (`resume/*.md`). The city agent's notes start with its
  "PAUSED 2026-10-29 15:21 UTC" section.
- `_save/tmp_city/`: the city agent's scripts, messages and plans (`msg/next_district_plan.txt` is the Calle Luna Sur
  plan), its N0b status files and its profiling patch.
- `_save/b43_changed/`: N0b's work in progress (Calle Luna Sur's roads, parcels and props), as the files of the city
  agent's private tree that differ from main 9b36279; `_save/b43_changed_LIST.txt` names them and their main blobs
  ("new" for a new file). Not yet signed off: the turning-circle prop fix in it is unchecked.

To resume N0b: export main 9b36279, copy `_save/b43_changed/` over it, then follow the PAUSED section of
`_save/scratchpad/resume/city.md`.

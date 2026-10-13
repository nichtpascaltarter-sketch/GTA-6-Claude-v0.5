#!/bin/sh
# Prints how much memory (MB) is still free for a new heavy process (a compile, a game under Wine): the lower of the
# machine's MemAvailable and the room left under this process tree's memory cgroup cap. Test containers can cap the
# tree below the machine's RAM, and when the cap is hit the kernel kills the largest process, usually a compile.
a=$(awk '/MemAvailable/ {print int($2 / 1024)}' /proc/meminfo 2>/dev/null)
c=
cg=$(awk -F: '$2 == "memory" {print $3; exit}' /proc/self/cgroup 2>/dev/null)
if [ -n "$cg" ] && [ -r "/sys/fs/cgroup/memory$cg/memory.stat" ]; then
  # cgroup v1: the cap is the lower of this group's limit and the inherited one; page cache is reclaimable
  d=/sys/fs/cgroup/memory$cg
  c=$(awk -v lim="$(cat $d/memory.limit_in_bytes 2>/dev/null)" '
    $1 == "hierarchical_memory_limit" && ($2 < lim || lim == "") {lim = $2}
    $1 == "total_rss" || $1 == "total_shmem" {u += $2}
    END {if (lim != "") print int((lim - u) / 1048576)}' $d/memory.stat)
else
  cg=$(awk -F: '$1 == "0" {print $3; exit}' /proc/self/cgroup 2>/dev/null)
  d=/sys/fs/cgroup$cg
  if [ -n "$cg" ] && [ -r "$d/memory.max" ] && [ "$(cat $d/memory.max)" != max ] && [ -r "$d/memory.stat" ]; then
    c=$(awk -v lim="$(cat $d/memory.max)" '$1 == "anon" || $1 == "shmem" {u += $2} END {print int((lim - u) / 1048576)}' $d/memory.stat)
  fi
fi
if [ -n "$c" ] && { [ -z "$a" ] || [ "$c" -lt "$a" ]; }; then a=$c; fi
echo "${a:-100000}"

#!/usr/bin/env bash
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RESULTS="$ROOT/experiments/results"; CASES="$ROOT/experiments/generated_cases"
NORMAL="$ROOT/build/pacman_normal"; TSAN="$ROOT/build/pacman_tsan"
RUNS="${REPEAT_RUNS:-1000}"; TSAN_RUNS="${TSAN_RUNS:-20}"; LIMIT="${TIMEOUT_SECONDS:-5}"
mkdir -p "$RESULTS" "$CASES"; cd "$ROOT"
status(){ printf '%s\n' "$2" >"$RESULTS/$1.status"; }
timed(){ local n=$1; shift; /usr/bin/time -v -o "$RESULTS/$n.time" "$@" >"$RESULTS/$n.out" 2>"$RESULTS/$n.err"; local s=$?; status "$n" "$s"; return 0; }
generate(){
 local s="$CASES/stress_100k" c="$CASES/collision_early" p="$CASES/priority_stress"; mkdir -p "$s" "$c" "$p"
 printf '%s\n' XXXXXXXXXX XPOOOOOOOX XOOOOOOOOX XOOOOOOOOX XOOAOOBOOX XOOOOOOOOX XOOCOODOOX XOOOOOOOOX XOOOOOOOOX XXXXXXXXXX >"$s/map.txt"
 awk 'BEGIN{for(i=1;i<=100000;i++)print(i%2?"RIGHT":"LEFT")}' >"$s/pacman_moves.txt"
 for g in 1 2 3 4; do awk 'BEGIN{for(i=1;i<=100000;i++)print(i%2?"RIGHT":"LEFT")}' >"$s/ghost_${g}_moves.txt"; done
 printf '%s\n' XXXXXXXXXX XPAOOOOOOX XOOOOOOOOX XOOBOCOODX XOOOOOOOOX XOOOOOOOOX XOOOOOOOOX XOOOOOOOOX XOOOOOOOOX XXXXXXXXXX >"$c/map.txt"
 awk 'BEGIN{for(i=1;i<=20;i++)print"LEFT"}' >"$c/pacman_moves.txt"
 awk 'BEGIN{for(i=1;i<=20;i++)print(i%2?"LEFT":"RIGHT")}' >"$c/ghost_1_moves.txt"
 for g in 2 3 4; do awk 'BEGIN{for(i=1;i<=20;i++)print"UP"}' >"$c/ghost_${g}_moves.txt"; done
 cp "$s/map.txt" "$p/map.txt"; awk 'BEGIN{for(i=1;i<=10000;i++)print"SET_PRIORITY "(i%2?31:30)}' >"$p/pacman_moves.txt"
 for g in 1 2 3 4; do awk 'BEGIN{for(i=1;i<=10000;i++)print"SET_PRIORITY "(i%2?30:31)}' >"$p/ghost_${g}_moves.txt"; done
}
repeat_normal(){ local n=$1 c=$2 r=$3 ticks=$4; NAME=$n CASE=$c RUNS_N=$r TICKS=$ticks BIN=$NORMAL RES=$RESULTS LIMIT_N=$LIMIT timed "$n" bash -c 'f=0;t=0; : >"$RES/$NAME.runs.err"; for i in $(seq 1 "$RUNS_N"); do timeout "$LIMIT_N" "$BIN" "$CASE" --max-ticks "$TICKS" >/dev/null 2>>"$RES/$NAME.runs.err"; s=$?; if [ $s -ne 0 ]; then echo "FAIL_RUN_${i}:exit=$s"; f=$((f+1)); [ $s -eq 124 ] && t=$((t+1)); fi; done; echo "TOTAL_RUNS=$RUNS_N FAILURES=$f TIMEOUTS=$t"; [ $f -eq 0 ]'; }
repeat_tsan(){ local n=$1; : >"$RESULTS/$n.tsan.log"; NAME=$n RUNS_N=$TSAN_RUNS BIN=$TSAN RES=$RESULTS CASE="$ROOT/Caso1" LIMIT_N=$LIMIT timed "$n" bash -c 'f=0; for i in $(seq 1 "$RUNS_N"); do echo "===== RUN $i =====" >>"$RES/$NAME.tsan.log"; TSAN_OPTIONS="halt_on_error=0 exitcode=66" timeout "$LIMIT_N" "$BIN" "$CASE" --max-ticks 100 >/dev/null 2>>"$RES/$NAME.tsan.log"; s=$?; [ $s -ne 0 ] && { echo "FAIL_RUN_${i}:exit=$s"; f=$((f+1)); }; done; echo "TOTAL_RUNS=$RUNS_N FAILURES=$f"; [ $f -eq 0 ]'; }
generate
make normal tsan >"$RESULTS/build.out" 2>"$RESULTS/build.err"; b=$?; if [ $b -ne 0 ]; then echo "Compilación fallida: $b" >"$RESULTS/run_errors.log"; python3 experiments/parse_time_metrics.py; exit $b; fi
timed T01_baseline_normal "$NORMAL" "$ROOT/Caso1"
repeat_normal T02_shutdown_stress_normal "$ROOT/Caso1" "$RUNS" 10
repeat_tsan T03_p1_terminar_tsan
repeat_tsan T04_p2_terminar_tsan
timed T05_queue_p1_100k timeout 120 "$NORMAL" "$CASES/stress_100k" --max-ticks 200000
timed T06_ghosts_100k timeout 120 "$NORMAL" "$CASES/stress_100k" --max-ticks 200000
repeat_normal T07_collision_stress "$CASES/collision_early" "$RUNS" 20
repeat_normal T08_game_over_shutdown_stress "$CASES/collision_early" "$RUNS" 20
timed T09_priority_mailbox_stress timeout 60 "$NORMAL" "$CASES/priority_stress" --max-ticks 10000
timed T10_100k_full_run timeout 120 "$NORMAL" "$CASES/stress_100k" --max-ticks 100000
python3 experiments/parse_time_metrics.py

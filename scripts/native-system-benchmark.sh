#!/usr/bin/env bash

# define DBG='rr record --' to launch all components under rr
# runs for DURATION seconds (defaults to 30)

# if DURATION is set to 0, sleep infinity
case "$DURATION" in
    inf|infinity|0) DURATION=infinity;;
    '') DURATION=30;;
esac

# locate and move to build-dir
CWD=$(pwd)
TL=$(git rev-parse --show-toplevel)
RT="${TL:-$CWD}"
BLD="$RT"/build
TESTDIR="$BLD"/test-$(date +"%s")
mkdir -p "$TESTDIR"
printf 'Running test from %s\n' "$TESTDIR"
cd "$TESTDIR" || exit

ORIG_CFG=
if test "$#" -lt 1 -o -z "$1"; then
    ORIG_CFG="$RT/2pc-compose.cfg"
else
    case "$1" in
        /*) ORIG_CFG="$1";;
        *)  ORIG_CFG="$CWD/$1";;
    esac
fi

# normalizes ports for local execution
IFS='' read -r -d '' normalize <<'EOF'
BEGIN {
    i = 5000
}

/".*:...."/ {
    gsub(/".*:...."/, "\"""0.0.0.0:" i "\"");
    ++i
}

{ print }
EOF

CFG="$TESTDIR"/config
awk "$normalize" "$ORIG_CFG" > "$CFG"

twophase=$(grep -q '2pc=1' "$CFG" && printf '1\n' || printf '0\n')

on_int() {
    printf 'Interrupting all components\n'
    trap '' SIGINT # avoid interrupting ourself
    for i in $PIDS; do # intentionally unquoted
        kill -SIGINT -- "-$i"
    done
    sleep 5

    for i in "$TESTDIR"/tx_samples_*.txt; do
        if ! test -s "$i"; then
            printf 'Could not generate plots: %s is not a non-empty, regular file\n' "$i"
            exit 1
        fi
    done

    printf 'Generating plots\n'
    python "$RT"/scripts/plot.py "$TESTDIR"

    exit
}

trap on_int SIGINT

getcount() {
    count=$(grep -E "$1_count" "$CFG")
    if test "$count"; then
        printf '%s\n' "$count" | cut -d'=' -f2
    else
        printf '0\n'
    fi
}

getpath() {
    case "$1" in
        # uniquely-named
        archiver)     printf '%s/src/uhs/atomizer/archiver/archiverd\n' "$BLD";;
        atomizer)     printf '%s/src/uhs/atomizer/atomizer/atomizer-raftd\n' "$BLD";;
        watchtower)   printf '%s/src/uhs/atomizer/watchtower/watchtowerd\n' "$BLD";;
        coordinator)  printf '%s/src/uhs/twophase/coordinator/coordinatord\n' "$BLD";;

        # special-case
        seeder)       printf '%s/tools/shard-seeder/shard-seeder\n' "$BLD";;

        # architecture-dependent
        loadgen)
            if test "$twophase" -eq 1; then
                printf '%s/tools/bench/twophase-gen\n' "$BLD"
            else
                printf '%s/tools/bench/atomizer-cli-watchtower\n' "$BLD"
            fi;;
        shard)
            if test "$twophase" -eq 1; then
                printf '%s/src/uhs/twophase/locking_shard/locking-shardd\n' "$BLD"
            else
                printf '%s/src/uhs/atomizer/shard/shardd\n' "$BLD"
            fi;;
        sentinel)
            if test "$twophase" -eq 1; then
                printf '%s/src/uhs/twophase/sentinel_2pc/sentineld-2pc\n' "$BLD"
            else
                printf '%s/src/uhs/atomizer/sentinel/sentineld\n' "$BLD"
            fi;;
        *) printf 'Unrecognized component: %s\n' "$1";;
    esac
}

seed() {
    seed_from=$(grep -E 'seed_from=.*' "$CFG" | cut -d'=' -f2)
    seed_from="${seed_from:-0}"
    seed_to=$(grep -E 'seed_to=.*' "$CFG" | cut -d'=' -f2)
    seed_to="${seed_to:-0}"
    if test "$seed_from" -ne "$seed_to"; then
        printf 'Seeding from %d to %d\n' "$seed_from" "$seed_to"
        ${DBG} "$(getpath seeder)" "$CFG" &> seeder.log
    else
        printf 'Running without seeding\n'
    fi
}

getpgid() {
    ps -o pgid= "$1"
}

PIDS=
launch() {
    last=$(getcount "$1")
    if test "$last" -le 0; then
        if test "$1" = 'loadgen'; then
            printf 'Running without a loadgen\n'
        else
            printf 'Invalid count for %s\n' "$1"
            exit 1
        fi
    else
        for id in $(seq 0 $(( "$last" - 1 )) ); do
            raft=$(getcount "$1$id")
            if test "$raft" -gt 0; then
                for node in $(seq 0 $(( "$raft" - 1 )) ); do
                    ${DBG} "$(getpath "$1")" "$CFG" "$id" "$node" &> "$1_${id}_$node.log" &
                    PID="$!"
                    printf 'Launched logical %s %d, replica %d [PID: %d]\n' "$1" "$id" "$node" "$PID"
                    PIDS="$PIDS $(getpgid $PID)"
                done
            else
                ${DBG} "$(getpath "$1")" "$CFG" "$id" &> "$1_$id.log" &
                PID="$!"
                printf 'Launched %s %d [PID: %d]\n' "$1" "$id" "$PID"
                PIDS="$PIDS $(getpgid $PID)"
            fi
        done
    fi
}

seed

if test "$twophase" -eq 0; then # atomizer
    for comp in watchtower atomizer archiver shard sentinel loadgen; do
        sleep 5
        launch "$comp"
    done
else # twophase
    for comp in shard coordinator sentinel loadgen; do
        sleep 10
        launch "$comp"
    done
fi

printf 'Awaiting manual termination or timeout (%ds)\n' "$DURATION"
sleep "$DURATION"

on_int

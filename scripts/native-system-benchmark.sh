#!/usr/bin/env bash
set -x  # BBW
DBG='rr record'
#DBG="${DBG:-gdb -ex run --args}"

# runs for DURATION seconds (defaults to 30)
# if DURATION is set to 0, sleep infinity
case "$DURATION" in
    inf|infinity|0) DURATION=infinity;;
    '') DURATION=30;;
esac

# locate and move to build-dir
CWD=$(pwd)
COMMIT=$(git rev-parse --short HEAD)
TL=$(git rev-parse --show-toplevel)
RT="${TL:-$CWD}"
BLD="$RT"/build
SEEDDIR="$BLD"/preseeds
TESTDIR="$BLD"/test-$(date +"%s")

mkdir -p "$TESTDIR"
printf 'Running test from %s\n' "$TESTDIR"
cd "$TESTDIR" || exit
####mkdir $TESTDIR/shard0_raft_log_0/
####cp /home/Bebo/cbdc/o-tx/build/test-1704829212/shard0_raft_log_0/000003* $TESTDIR/shard0_raft_log_0/

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
arch=
if test "$twophase" -eq 0; then
    arch='atomizer'
else
    arch='2pc'
fi

on_int() {
    printf 'Interrupting all components\n'
    trap '' SIGINT # avoid interrupting ourself
    for i in $PIDS; do # intentionally unquoted
        kill -SIGINT -- "-$i"
    done
    sleep 5

    for i in "$TESTDIR"/tx_samples_*.txt; do
        if  ! test -s "$i"; then
            printf 'Could not generate plots: %s is not a non-empty, regular file\n' "$i"
            exit 1
        fi
    done

    if test -n "$(find "$TESTDIR" -maxdepth 1 -name '*.perf' -print -quit)"; then
        for i in "$TESTDIR"/*.perf; do
            perf script -i "$i" | stackcollapse-perf.pl > "${i/.perf/.folded}"
            flamegraph.pl "${i/.perf/.folded}" > "${i/.perf/.svg}"
            rm -- "${i/.perf/.folded}"
        done
    fi

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

run() {
    PROC_LOG="$TESTDIR"/"$PNAME.log"
    PERF_LOG="$TESTDIR"/"$PNAME-perf.log"
    COMP=
    case "$RECORD" in
        perf)
            $@ &> "$PROC_LOG" &
            COMP="$!"
            perf record -F 99 -a -g -o "$PNAME".perf -p "$COMP" &> "$PERF_LOG" & ;;
        debug)
            ${DBG} -- "$@" &> "$PROC_LOG" &
            COMP="$!";;
        *)
            $@ &> "$PROC_LOG" &
            COMP="$!";;
    esac

    if test -n "$BLOCK"; then
        wait "$COMP"
    fi

    echo "$COMP"
}

seed() {
    seed_from=$(grep -E 'seed_from=.*' "$CFG" | cut -d'=' -f2)
    seed_from="${seed_from:-0}"
    seed_to=$(grep -E 'seed_to=.*' "$CFG" | cut -d'=' -f2)
    seed_to="${seed_to:-0}"
    seed_count=$(( "$seed_to" - "$seed_from" ))
    if test "$seed_from" -eq "$seed_to"; then
        printf 'Running without seeding\n'
        return
    fi

    preseed_id="$arch"_"$COMMIT"_"$seed_count"
    if test ! -e "$SEEDDIR"/"$preseed_id"; then
        printf 'Creating %s\n' "$preseed_id"
        mkdir -p -- "$SEEDDIR"/"$preseed_id"
        pushd "$SEEDDIR"/"$preseed_id" &> /dev/null
        PID=$(PNAME=seeder BLOCK=1 run "$(getpath seeder)" "$CFG")
        popd &> /dev/null
    fi

    printf 'Using %s as seed\n' "$preseed_id"
    for i in "$SEEDDIR"/"$preseed_id"/*; do
        ln -sf -- "$i" "$TESTDIR"/"$(basename "$i")"
    done
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
                    PID=$(PNAME="$1_${id}_$node" run "$(getpath "$1")" "$CFG" "$id" "$node")
                    printf 'Launched logical %s %d, replica %d [PID: %d]\n' "$1" "$id" "$node" "$PID"
                    PIDS="$PIDS $(getpgid $PID)"
                done
            else
                PID=$(PNAME="$1_$id" run "$(getpath "$1")" "$CFG" "$id")
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

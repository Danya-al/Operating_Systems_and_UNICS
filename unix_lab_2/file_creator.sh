#!/bin/sh
set -eu

stopping=0
owned_file=
trap 'stopping=1' INT TERM

cd "${DATA_DIR:-/data}"
exec 9>>.lock

container_id=$(od -An -N16 -tx1 /dev/urandom | tr -d '[:space:]')
if [ "${#container_id}" -ne 32 ]; then
    printf '%s\n' 'Could not generate a container ID' >&2
    exit 1
fi

reserve_file() {
    flock -x 9

    if [ "$stopping" -eq 0 ]; then
        index=1
        while :; do
            case $index in
                [0-9]) candidate=./00$index ;;
                [0-9][0-9]) candidate=./0$index ;;
                *) candidate=./$index ;;
            esac

            if [ ! -e "$candidate" ] && [ ! -L "$candidate" ]; then
                set -C
                true > "$candidate"
                set +C
                owned_file=$candidate
                break
            fi

            index=$((index + 1))
        done
    fi

    flock -u 9
}

remove_file() {
    [ -n "$owned_file" ] || return 0

    flock -x 9
    rm -f "$owned_file"
    owned_file=
    flock -u 9
}

trap 'remove_file' 0

count=0
while [ "$stopping" -eq 0 ]; do
    reserve_file
    [ "$stopping" -eq 0 ] || break

    count=$((count + 1))
    printf '%s %s\n' "$container_id" "$count" > "$owned_file"
    printf 'created %s %s %s\n' "${owned_file#./}" "$container_id" "$count"

    sleep 1 || [ "$stopping" -ne 0 ]
    remove_file

    [ "$stopping" -eq 0 ] || break
    sleep 1 || [ "$stopping" -ne 0 ]
done
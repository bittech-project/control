#!/bin/bash

TRACK_ORIGINS="${1:-yes}"

printf "Settings:
	\t--track-origins=%s\n" "$TRACK_ORIGINS"

valgrind --leak-check=full --show-leak-kinds=all --track-origins="$TRACK_ORIGINS" --log-file=valgrind-out.txt ./control/src/control &&
cat valgrind-out.txt

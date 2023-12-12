#!/bin/bash

rootdir=$(readlink -f "$(dirname "$0")")/..

SERVER_SRC_PATH=$rootdir/server/
CONTROL_SRC_PATH=$rootdir/control/src/

function run_iwyu {
	make clean
	make all -k CC=include-what-you-use CCFLAGS="-Xiwyu --error_always" 2> /tmp/iwyu.out
	iwyu-fix-includes < /tmp/iwyu.out
}

cd "$SERVER_SRC_PATH" && run_iwyu
cd "$CONTROL_SRC_PATH" && run_iwyu

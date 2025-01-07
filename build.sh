#!/usr/bin/env bash

set -euo pipefail

PROJECT_DIR="$(git rev-parse --show-toplevel)"
PROJECT_NAME="termview"

: ${OUT_DIR:=build}
: ${DEBUG:=1}

CC=clang
OUT_DIR="${PROJECT_DIR}/${OUT_DIR}"
SRC_DIR="${PROJECT_DIR}/src"
CMD_DIR="${PROJECT_DIR}/cmd"

CFLAGS="${CFLAGS:-} -I${PROJECT_DIR}/include -I${PROJECT_DIR}/src -isystem=$(clang -print-resource-dir)/include"
CFLAGS="$CFLAGS $(pkg-config --cflags --libs libuv basu) -pthread"
CFLAGS="$CFLAGS -Wall -Wextra -Werror"
CFLAGS="$CFLAGS -DLOG_ENABLE_DBG=1"

if [ "$DEBUG" = "1" ]; then
	CFLAGS="$CFLAGS -g -DMODDIR=\"$OUT_DIR/modules\""
fi

clean() {
	rm -rf "$OUT_DIR"
}

build_mod() {
	local f=$1
	local modname="$(basename $f)"
	modname="${modname%.*}"

	mkdir -p "$OUT_DIR/modules"
	$CC -I"$PROJECT_DIR/include" \
		-shared \
		$CFLAGS $1 -o "$OUT_DIR/modules/$modname.so"
	echo "$OUT_DIR/modules/$modname.so"
}

build_modules() {
	for f in $PROJECT_DIR/src/modules/*.c; do
		build_mod $f
	done
}

build_dhub() {
	mkdir -p "$OUT_DIR"
	# -rdynamic so modules can access D-Hub symbols (log functions, libuv, etc).
	$CC -rdynamic \
		-I"$PROJECT_DIR/cmd/dhub" \
		"$CMD_DIR/dhub/main.c" "$CMD_DIR/dhub/start/main.c" \
		"$SRC_DIR/log.c" "$SRC_DIR/xsnprintf.c" "$SRC_DIR/debug.c" \
		$CFLAGS -o "$OUT_DIR/dhub"
	echo "$OUT_DIR/dhub"
}

build() {
	build_modules
	build_dhub
}

run() {
	$(build)
}

compile_flags() {
	echo $CFLAGS | tr ' ' '\n' > compile_flags.txt
}

fmt() {
	find . -type f \( -name "*.c" -or -name "*.h" \) | xargs indent --linux-style
	if [ -z "${FMT_BACKUP:-}" ]; then
		find . -type f -name "*~" | xargs rm
	fi
}

pushd $PROJECT_DIR &> /dev/null
if [ "$#" = "0" ]; then
	build
else
	while [ "$#" -gt "0" ]; do
		$1
		shift
	done
fi
popd &> /dev/null


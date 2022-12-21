#!/bin/sh -e
CFLAGS="\
-pedantic -Wall -Wextra \
-Wno-implicit-fallthrough \
-Wno-missing-field-initializers \
-Wfatal-errors -std=c99 \
-D_POSIX_C_SOURCE=200809L $CFLAGS"

: ${CC:=$(command -v cc)}
: ${PREFIX:=/usr/local}

run() {
	printf '%s\n' "$*"
	"$@"
}

install() {
	[ -x v4l2fb ] || build
	run mkdir -p "$DESTDIR$PREFIX/bin/"
	run cp -f v4l2fb "$DESTDIR$PREFIX/bin/v4l2fb"
}

build() {
	run "$CC" "v4l2fb.c" $CFLAGS -o v4l2fb
}

if [ "$#" -gt 0 ]; then
	"$@"
else
	build
fi

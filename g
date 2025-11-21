#!/bin/sh
if [ "$(uname)" = "Darwin" ]; then
    CFG="./ginkgo_mac"
else
    CFG="./ginkgo_linux"
fi
ASAN_OPTIONS=detect_odr_violation=0 $CFG "$@"
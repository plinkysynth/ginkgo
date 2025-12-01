#!/bin/sh
if [ "$(uname)" = "Darwin" ]; then
    # open build/ginkgo.app
    #ASAN_OPTIONS=detect_odr_violation=0 build/ginkgo.app/Contents/MacOS/ginkgo "$@"
    ASAN_OPTIONS=detect_odr_violation=0 ./ginkgo_mac "$@"
else
    ASAN_OPTIONS=detect_odr_violation=0 ./ginkgo_linux "$@"
fi

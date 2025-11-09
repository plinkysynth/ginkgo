#!/bin/sh
ASAN_OPTIONS=detect_odr_violation=0 ./ginkgo "$@"
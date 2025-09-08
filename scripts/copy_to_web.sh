#!/bin/bash

rsync -av --include='*/' --include='*.html' --include='*.js' --include='*.wasm' --include='*.png' --exclude='*' ./ alex@serval.mythic-beasts.com:public_html/plinky12/


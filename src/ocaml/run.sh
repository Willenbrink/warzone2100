#!/bin/sh
dune build ./main.exe && _build/default/main.exe -datadir "/opt/warzone2100/data" -autogame

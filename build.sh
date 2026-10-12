#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Vorthas
# Release build in build/. Extra arguments go to cmake, e.g. a personal build
# with every font in fonts/ embedded (remembered for later builds):
#   ./build.sh -DPF2MONSTER_EMBED_ALL_FONTS=ON
cmake -B build -DCMAKE_BUILD_TYPE=Release "$@" && cmake --build build -j6

#!/bin/bash
# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

# Settings shared by the driver scripts. These were run by an agent on
# Linux, reaching the Windows build machine with the ssh skill's
# claude-ssh wrapper; adapt them to your setup.
HOST=${HOST:-steve-win-sandbox}                        # ssh host of the Windows machine
REPO=${REPO:-'C:\Users\sandbox\dev\tightvnc-2.8.90-gpl'} # checkout there (PRE in the .py files)
WORK=${WORK:-$PWD/work}                               # local working directory
TOOLS=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
TOOLS_REMOTE='tools\pointer-parameters'               # the same scripts in the checkout
remote() { claude-ssh call "$HOST" "cd $REPO; $*"; }

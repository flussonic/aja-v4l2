#!/bin/sh
# Copy the tree to the bench machine and build it there.
#   tools/sync.sh [user@host] [remote dir]
set -e
HOST=${1:?usage: tools/sync.sh <user@host> [remote dir]}
DIR=${2:-/root/aja-v4l2}
cd "$(dirname "$0")/.."
rsync -a --delete --exclude .git --exclude build --exclude '*.o' --exclude '*.cmd' \
      --exclude '*.ko' --exclude '*.mod' --exclude '*.mod.c' --exclude tools/ajav \
      ./ "$HOST:$DIR/"
ssh "$HOST" "make -C $DIR -j8 2>&1 | tail -20"

#!/bin/bash
# Copy a binary onto the board over SSH in 8 KB base64 chunks (the board has no
# scp/tar, and dropbear refuses very long commands): board_put_bin.sh <file> <board path>
set -e
src=$1; dst=$2; tmp=$(mktemp -d)
base64 -w0 "$src" | split -b 8192 - $tmp/c.
~/m2/bssh.sh "rm -f $dst.b64"
for c in $tmp/c.*; do ~/m2/bssh.sh "echo -n $(cat $c) >> $dst.b64"; done
~/m2/bssh.sh "base64 -d $dst.b64 > $dst && rm $dst.b64 && chmod +x $dst && md5sum $dst"
md5sum "$src"
rm -rf $tmp

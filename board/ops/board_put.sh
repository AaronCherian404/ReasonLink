#!/bin/bash
# Copy a small text file onto the board over SSH (no scp/tar there): board_put.sh <torino file> <board path>
content=$(cat "$1")
exec ~/m2/bssh.sh "cat > $2 <<'RL_EOF'
$content
RL_EOF
chmod +x $2; wc -c $2"

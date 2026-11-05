#!/bin/sh
# ssh to the board root shell through the lab gateway; password from $RL_PW (never stored)
DISPLAY=none SSH_ASKPASS=$HOME/m2/askpass.sh setsid -w ssh -p 5508 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o PreferredAuthentications=password -o NumberOfPasswordPrompts=1 root@espgate "$@" < /dev/null

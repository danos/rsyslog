#!/bin/bash
# This file is part of the rsyslog project, released under ASL 2.0

# This test checks that omprog does NOT send a TERM signal to the
# external program when signalOnClose=off, closes the pipe, and kills
# the unresponsive child if killUnresponsive=on.

. $srcdir/diag.sh init
. $srcdir/diag.sh startup omprog-close-unresponsive-noterm.conf
. $srcdir/diag.sh wait-startup
. $srcdir/diag.sh injectmsg 0 10
. $srcdir/diag.sh wait-queueempty
. $srcdir/diag.sh shutdown-when-empty
. $srcdir/diag.sh wait-shutdown
. $srcdir/diag.sh ensure-no-process-exists omprog-close-unresponsive-bin.sh

expected_output="Starting
Received msgnum:00000000:
Received msgnum:00000001:
Received msgnum:00000002:
Received msgnum:00000003:
Received msgnum:00000004:
Received msgnum:00000005:
Received msgnum:00000006:
Received msgnum:00000007:
Received msgnum:00000008:
Received msgnum:00000009:
Terminating unresponsively"

written_output=$(<rsyslog.out.log)
if [[ "$expected_output" != "$written_output" ]]; then
    echo unexpected omprog script output:
    echo "$written_output"
    . $srcdir/diag.sh error-exit 1
fi

. $srcdir/diag.sh exit

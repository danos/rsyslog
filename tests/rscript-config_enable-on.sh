#!/bin/bash
# tests 'config.enabled="on"' -- default value is implicitely check
# in all testbench tests and does not need its individual test
# (actually it is here tested via template() and action() as well...
# added 2018-01-22 by Rainer Gerhards; Released under ASL 2.0
. $srcdir/diag.sh init
export DO_STOP=on
. $srcdir/diag.sh generate-conf
. $srcdir/diag.sh add-conf '
template(name="outfmt" type="string" string="%msg:F,58:2%\n")

if $msg contains "msgnum:" then {
	if $msg contains "msgnum:00000000" then {
		include(text="stop" config.enabled=`echo $DO_STOP`)
	}
	action(type="omfile" template="outfmt" file="rsyslog.out.log")
}
'
. $srcdir/diag.sh startup
. $srcdir/diag.sh injectmsg 0 10
. $srcdir/diag.sh shutdown-when-empty
. $srcdir/diag.sh wait-shutdown
. $srcdir/diag.sh seq-check 1 9
. $srcdir/diag.sh exit

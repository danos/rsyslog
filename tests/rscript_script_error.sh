#!/bin/bash
# Added 2017-12-09 by Rainer Gerhards, released under ASL 2.0
. $srcdir/diag.sh init
. $srcdir/diag.sh generate-conf
. $srcdir/diag.sh add-conf '
module(load="../plugins/imtcp/.libs/imtcp")
input(type="imtcp" port="13514")
template(name="outfmt" type="string" string="%$!%\n")

local4.* {
	set $!valid!serial   = parse_time("2017-10-05T01:10:11Z");
	set $!valid!error    = script_error();
	set $!invalid!serial = parse_time("not a date/time");
	set $!invalid!error  = script_error();
	set $!valid2!serial   = parse_time("2017-10-05T01:10:11Z");
	set $!valid2!error    = script_error();
	action(type="omfile" file="rsyslog.out.log" template="outfmt")
}
'

. $srcdir/diag.sh startup
. $srcdir/diag.sh tcpflood -m1
. $srcdir/diag.sh shutdown-when-empty
. $srcdir/diag.sh wait-shutdown

# Our fixed and calculated expected results
EXPECTED='{ "valid": { "serial": 1507165811, "error": 0 }, "invalid": { "serial": 0, "error": 1 }, "valid2": { "serial": 1507165811, "error": 0 } }'
echo $EXPECTED | cmp - rsyslog.out.log
if [[ $? -ne 0 ]]; then
  printf "Invalid function output detected!\n"
  printf "expected:\n$EXPECTED\n"
  printf "rsyslog.out is:\n"
  cat rsyslog.out.log
  . $srcdir/diag.sh error-exit 1
fi;

. $srcdir/diag.sh exit

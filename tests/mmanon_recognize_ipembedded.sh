#!/bin/bash
# add 2016-11-22 by Jan Gerhards, released under ASL 2.0

. $srcdir/diag.sh init
. $srcdir/diag.sh generate-conf
. $srcdir/diag.sh add-conf '
template(name="outfmt" type="string" string="%msg%\n")

module(load="../plugins/mmanon/.libs/mmanon")
module(load="../plugins/imtcp/.libs/imtcp")
input(type="imtcp" port="13514" ruleset="testing")

ruleset(name="testing") {
	action(type="mmanon" ipv4.enable="off" ipv6.enable="off" embeddedipv4.bits="128" embeddedipv4.anonmode="zero")
	action(type="omfile" file="rsyslog.out.log" template="outfmt")
}'

. $srcdir/diag.sh startup
. $srcdir/diag.sh tcpflood -m1 -M "\"<129>Mar 10 01:00:00 172.20.245.8 tag: asdfghjk
<129>Mar 10 01:00:00 172.20.245.8 tag: FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:255.255.255.255
<129>Mar 10 01:00:00 172.20.245.8 tag: 61:34:ad::7:F aa:ff43::756:172.2.3.4
<129>Mar 10 01:00:00 172.20.245.8 tag: ::
<129>Mar 10 01:00:00 172.20.245.8 tag: 0::
<129>Mar 10 01:00:00 172.20.245.8 tag: ::0.0.0.0
<129>Mar 10 01:00:00 172.20.245.8 tag: 13:abd:45:1:1:1:0.0.0.0
<129>Mar 10 01:00:00 172.20.245.8 tag: 13:abd:45::1:1:0.0.0.0
<129>Mar 10 01:00:00 172.20.245.8 tag: 13:abd:45:1:1:1:1:0.0.0.0
<129>Mar 10 01:00:00 172.20.245.8 tag: 13:abd:45:1:1:1::1:0.0.0.0
<129>Mar 10 01:00:00 172.20.245.8 tag: 13:abd:45:0.0.0.0
<129>Mar 10 01:00:00 172.20.245.8 tag: 13:abd:45::. test
<129>Mar 10 01:00:00 172.20.245.8 tag: 13:abd:45::1.2.3.4 test
<129>Mar 10 01:00:00 172.20.245.8 tag: *13:abd:45::ac.2.3.5* test
<129>Mar 10 01:00:00 172.20.245.8 tag: ewirnwem aa:ff43:756:99:ff:445:cc.1.2.3.4
<129>Mar 10 01:00:00 172.20.245.8 tag: aa::ff:bb:122:0:44.1.23.4.0
<129>Mar 10 01:00:00 172.20.245.8 tag: 12:12345::a.3.4.12.7
<129>Mar 10 01:00:00 172.20.245.8 tag: textnoblank72:8374:adc7:47FF::43:172.1.1.0
<129>Mar 10 01:00:00 172.20.245.8 tag: 72:8374:adc7:47FF::43:172.1.1.0stillnoblank
<129>Mar 10 01:00:00 172.20.245.8 tag: textnoblank72:8374:adc7:47FF::43:172.1.1.0stillnoblank\""

. $srcdir/diag.sh shutdown-when-empty
. $srcdir/diag.sh wait-shutdown
echo ' asdfghjk
 0:0:0:0:0:0:0.0.0.0
 61:34:ad::7:F 0:0:0:0:0:0:0.0.0.0
 ::
 0::
 0:0:0:0:0:0:0.0.0.0
 0:0:0:0:0:0:0.0.0.0
 0:0:0:0:0:0:0.0.0.0
 13:0:0:0:0:0:0:0.0.0.0
 13:abd:0:0:0:0:0:0:0.0.0.0
 13:abd:45:0.0.0.0
 13:abd:45::. test
 0:0:0:0:0:0:0.0.0.0 test
 *13:abd:45::ac.2.3.5* test
 ewirnwem aa:ff43:756:99:ff:445:cc.1.2.3.4
 0:0:0:0:0:0:0.0.0.0.0
 12:12345::a.3.4.12.7
 textnoblank0:0:0:0:0:0:0.0.0.0
 0:0:0:0:0:0:0.0.0.0stillnoblank
 textnoblank0:0:0:0:0:0:0.0.0.0stillnoblank' | cmp - rsyslog.out.log
if [ ! $? -eq 0 ]; then
  echo "invalid response generated, rsyslog.out.log is:"
  cat rsyslog.out.log
  . $srcdir/diag.sh error-exit  1
fi;

. $srcdir/diag.sh exit

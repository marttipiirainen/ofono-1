#!/bin/sh

STARTTIME=$(date +%s)

echo START OFONO TESTS

if ! start ofono ; then
	echo CANNOT START OFONO
	exit 1
fi

if ! ./rilmodem/test-modem-offline ; then
	echo OFONO NOT OFFLINE
	exit 1
fi

if ! ./online-modem ; then
	echo CANNOT SET OFONO ONLINE
	exit 1
fi

if ! ./rilmodem/test-sim-online ; then
	echo SIM NOT READY
	exit 1
fi

if ! ./rilmodem/test-hangup ; then
	echo HANGUP FAILED
	exit 1
fi

if ! ./rilmodem/test-roaming ; then
	echo ROAMING FAILED
	exit 1
fi

if ! ./offline-modem ; then
	echo CANNOT SET OFONO OFFLINE
	exit 1
fi

if ! stop ofono ; then
	echo CANNOT STOP OFONO
	exit 1
fi

ENDTIME=$(date +%s)
DIFFTIME=$(( $ENDTIME - $STARTTIME ))
echo TESTS PASSED, DURATION $DIFFTIME SECONDS


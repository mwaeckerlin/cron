#!/bin/sh

if test "$1" != "-d"; then
    OPTION=-f
fi

crond ${OPTION} -l ${CRON_DEBUG:-8} -L /proc/1/fd/1

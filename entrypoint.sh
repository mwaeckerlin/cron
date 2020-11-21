#!/bin/sh
/usr/sbin/crond -f -L /proc/1/fd/1 -l ${CRON_DEBUG:-0} -M /bin/logger

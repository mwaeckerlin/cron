# Run Cron Jobs in Docker

Just inherit and add your scripts to one of the following directories:
 - `/etc/periodic/15min`
 - `/etc/periodic/hourly`
 - `/etc/periodic/weekly`
 - `/etc/periodic/daily`
 - `/etc/periodic/monthly`

As long as `/start.sh` is executed (the default) cron starts in foreground.

Run `/start.sh -d` to start cron as daemon.

You may define environment variable `CRON_DEBUG` to a value less than
the default (`8`) to increase debug level.

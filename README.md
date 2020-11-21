# Run Cron Jobs in Docker

Very lightweight cron image of only 1.59MB, based on alpine but stripped to the minimum and runs as non-root user.

Just add your scripts to one of the following directories:

- `/etc/periodic/min`
- `/etc/periodic/5min`
- `/etc/periodic/10min`
- `/etc/periodic/15min`
- `/etc/periodic/30min`
- `/etc/periodic/hourly`
- `/etc/periodic/daily`
- `/etc/periodic/weekly`
- `/etc/periodic/monthly`
- `/etc/periodic/yearly`

You may define environment variable `CRON_DEBUG` to a value higher than the default `0` to increase debug level up to `8`.

Everything is logged to docker, including the output of the cron jobs.

To test it with teh included `demo` sample script, just run `docker-compose up`.

# Changelog

- 2026-09-26 **1.4.1**
    - The image is headless now: no shell, no busybox, only the cron program, below 1MB. A derived image brings exactly the programs its jobs need, and nothing an attacker could use besides
        - jobs run their program directly; a script needs its interpreter in the derived image, and shell syntax in a crontab line is refused with a message
        - scripts that relied on the shell of earlier versions no longer run and have to be replaced by a program or brought along with their interpreter
    - Any schedule is possible: a derived image adds a crontab file to `/etc/cron.d`, in the standard syntax including `@reboot`, `@daily` and the other shortcuts, with environment variables per file
    - The ten periodic directories keep their names and times
    - A crontab with an error stops the start and names file, line and cause; `cron --check` shows every job's next run
    - Every job's output reaches the container log below a header naming the job; a failing job is always logged, `CRON_DEBUG=1` logs every start and end
    - Schedules follow the time zone in `TZ`, UTC without it
    - A crontab line can take its schedule and its arguments from the environment of the container with `${NAME}`, so a derived image sets defaults that a deployment overrides
    - The container stops at once and cleanly, also while a job is running
    - The demo runs without mounting host files and shows how to derive an image
    - The images are published for amd64 and arm64, each only after the complete test suite passed on it
    - Feature and test registers with an end to end suite and an automatic guard: every feature has a test

- 2023-04-21 **1.4.0**
    - Job output reaches the container log with a separator line and the job's name above it
    - The image is assembled in one more build stage, so fewer layers remain

- 2021-04-17 **1.3.0**
    - The debug level for the cron log follows `CRON_DEBUG` (default `0`)

- 2020-11-29 **1.2.0**
    - Jobs can write to `/var/tmp`

- 2020-11-22 **1.1.0**
    - Very small image of about 1.6MB, based on Alpine and reduced to the minimum, running as an unprivileged user
    - New intervals: every minute, every 5, 10 and 30 minutes, and yearly
    - Output of the jobs goes to the container log
    - Demo with `docker-compose up`

- 2019-08-07 **1.0.0**
    - Cron in a container: scripts in `/etc/periodic/15min`, `hourly`, `daily`, `weekly` and `monthly` run at their interval
    - `CRON_DEBUG` sets the log level

# Run Cron Jobs in Docker

[mwaeckerlin/cron](https://hub.docker.com/r/mwaeckerlin/cron) is a cron base image below 1MB without a shell: derive from it, copy in the programs your jobs call, add a schedule, done.

A cron container usually carries a complete shell and a toolbox so that any job line can run. This image carries only the cron program. Your derived image adds exactly the tools its jobs need — `rsync`, `pg_dump`, `curl`, your own binary — and nothing else, so a compromised job finds no shell to continue with.

- no shell, no busybox, no interpreter: jobs run their program directly
- runs, and runs its jobs, as the unprivileged user `somebody`
- standard crontab syntax, plus ten periodic directories
- every job's output in the container log, failures always logged
- a broken crontab stops the start with file, line and cause

All features are listed in [FEATURES.md](FEATURES.md), all tests in [TESTS.md](TESTS.md).

The image is a runtime base image: it is the `FROM` of the final stage of your image. It is built via the build-only [mwaeckerlin/very-base](https://github.com/mwaeckerlin/very-base) and ships on the runtime base [mwaeckerlin/scratch](https://github.com/mwaeckerlin/scratch).

## Deriving an Image

Install the programs in a build stage from [mwaeckerlin/very-base](https://github.com/mwaeckerlin/very-base), collect them with their libraries in `/root/`, and copy that tree onto the cron image:

```Dockerfile
FROM mwaeckerlin/very-base AS build
RUN $PKG_INSTALL rsync
RUN tar cph $(which rsync) \
    $(ldd $(which rsync) | sed -n 's,.* => \([^ ]*\) .*,\1,p') \
    | tar xpC /root/

FROM mwaeckerlin/cron
COPY --from=build /root/ /
COPY backup /etc/cron.d/backup
```

With the crontab file `backup`:

```crontab
# min hour day month weekday command
30    2    *   *     *       rsync -a /data/ /backup/
```

The directory [demo/](demo/) holds a complete example, which `npm start` builds and runs.

## Schedules

### Crontab Files

Every file in `/etc/cron.d` is a crontab. A file name may only contain letters, digits, `_` and `-`; any other file is ignored, and the log says so.

A line has five time fields and the command:

| Field | Values |
| --- | --- |
| minute | `0`–`59` |
| hour | `0`–`23` |
| day of month | `1`–`31` |
| month | `1`–`12` or `jan`–`dec` |
| day of week | `0`–`7` or `sun`–`sat`, `0` and `7` are Sunday |

A field takes `*`, a value, a range `1-5`, a list `1,15`, a step `*/10` or `5-30/5`. When both day fields are restricted, a job runs when either one matches, as in every standard cron.

Instead of the five fields a line may start with `@reboot` (once at container start), `@yearly` or `@annually`, `@monthly`, `@weekly`, `@daily` or `@midnight`, or `@hourly`.

A line `NAME=value` sets an environment variable for the jobs below it in the same file. Every job also gets the environment of the container.

### Variables in Crontab Lines

Before a line is read, every `${NAME}` in it is replaced by that variable of the container environment, in the time fields as in the command. A derived image sets defaults with `ENV`, and a deployment overrides them:

```Dockerfile
ENV RSYNC_SCHEDULE="0 * * * *"
ENV RSYNC_OPTIONS="-avP --delete"
```

```crontab
${RSYNC_SCHEDULE} rsync ${RSYNC_OPTIONS} /source/ /target/
```

The value is split into arguments like the rest of the line; `-avP --delete` becomes two arguments, `"${RSYNC_OPTIONS}"` in quotes one. It reaches no shell. A variable that is not set stops the start with file, line and name, and `--check` shows the lines with the values filled in.

### Commands Without a Shell

The command is split into arguments and executed directly; the program is found by its path or in `PATH`. `"double"` and `'single'` quotes and backslash escapes work as in a shell. Everything else a shell would do is refused at start with a message: pipes, redirections, `;`, `&`, `$` other than the `${NAME}` above, command substitution. Quoted, these characters are passed on as text.

A job that needs a pipeline or a script belongs in a program of its own; a script runs when the derived image brings its interpreter along.

### Periodic Directories

Every executable file in one of these directories runs at its interval, one after the other in name order:

| Directory | Runs |
| --- | --- |
| `/etc/periodic/min` | every minute |
| `/etc/periodic/5min` | every 5 minutes |
| `/etc/periodic/10min` | every 10 minutes |
| `/etc/periodic/15min` | every 15 minutes |
| `/etc/periodic/30min` | every 30 minutes |
| `/etc/periodic/hourly` | at minute 0 of every hour |
| `/etc/periodic/daily` | at 02:00 |
| `/etc/periodic/weekly` | on Saturday at 03:00 |
| `/etc/periodic/monthly` | on the 1st at 05:00 |
| `/etc/periodic/yearly` | on 1 January at 01:00 |

The schedule for these directories is the crontab `/etc/cron.d/periodic`; a derived image may replace it.

## Configuration

| Variable | Default | Effect |
| --- | --- | --- |
| `CRON_DEBUG` | `0` | `0` logs failed jobs; `1` or higher also logs every start and end |
| `TZ` | UTC | time zone of the schedules, as a POSIX string, for Central Europe `CET-1CEST,M3.5.0,M10.5.0/3` |

## Log

Everything goes to the container log. The output and error output of a job stand below a header with its file, line and command, here from the demo:

```text
====================================================================================================
/etc/cron.d/demo:2 /bin/echo "RUNNING CRONJOB every minute"
----------------------------------------------------------------------------------------------------
RUNNING CRONJOB every minute
```

A job that fails is logged with its exit status and duration:

```text
**** cron: FAILED /etc/cron.d/e2e:8: exit status 1 after 0.0s: false
```

On stop the container ends at once with status 0; a job that is still running ends with it.

## Checking a Crontab

At start every crontab is checked. A wrong field, shell syntax, an unclosed quote, a variable that is not set, a missing or not executable program, or a date that never occurs (`0 0 30 2 *`) stops the start, and every error is named with file and line.

The same check runs without starting, and prints each job's next run:

```sh
$ docker run --rm my-cron-image --check
$ docker run --rm my-cron-image --check --at 2026-12-31T23:59
```

`--at` takes a local time, or UTC with a trailing `Z`. A file name after `--check` checks that file instead of `/etc/cron.d`.

## Development

```sh
$ npm run build   # builds mwaeckerlin/cron and the demo image
$ npm start       # runs both in the foreground, stop with Ctrl+C
$ npm test        # docs contract, image contract, end to end suite
$ npm run deploy  # pushes both images to Docker Hub
```

`npm test` checks that every feature in [FEATURES.md](FEATURES.md) has a test in [TESTS.md](TESTS.md), that both images contain no shell, no busybox and no perl, and runs the end to end suite in `tests/e2e/`: an image derived as shown above runs real jobs every minute, so the suite takes about two minutes.

The cron program is [cron.cpp](cron.cpp), compiled statically in the build stage of the [Dockerfile](Dockerfile).

On every push and once a week, [.github/workflows/docker.yml](.github/workflows/docker.yml) builds both images for amd64 and arm64, runs `npm test` on each architecture and publishes them on Docker Hub; a red test stops the publication.

## Internals

The image had used Alpine's `crond` until version 1.4.0. `crond` hands every job line to `/bin/sh`, so the image had to carry busybox with a shell and all its tools. `cron.cpp` replaces it: it reads the same crontab syntax, splits the command line itself and starts the program with `execv`, as the other headless images of this family start their service from a small C++ init.

`cron` is PID 1 of the container. It collects every ended process, also the ones a job left behind. On `SIGTERM` or `SIGINT` it exits with status 0, and the kernel then ends every process left in the container.

A clock that jumps forward by up to five minutes runs the jobs of the minutes in between; a larger jump runs only the current minute, and a jump backwards runs nothing twice.

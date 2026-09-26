# Tests

Register of all tests, grouped by kind and sorted by the [FEATURES.md](FEATURES.md) number each test covers. `npm test` runs everything; the guard `tests/docs-contract.sh` fails when a feature has no test entry here. Tests are never skipped.

The e2e stack (`tests/e2e/`) runs an image derived from `mwaeckerlin/cron` the way the README describes it, with real programs from Alpine's coreutils, the project's demo image, and an image with a broken crontab.

## E2E — running containers (`tests/run-e2e.sh`)

- **F1** `tests/run-e2e.sh` › demo_image_runs_its_jobs — the demo image, derived as the README shows, runs its crontab job and its periodic program.
- **F1** `tests/run-e2e.sh` › demo_without_failures — the demo image starts and logs no failed job.
- **F1** `tests/run-e2e.sh` › jobs_run_unprivileged — the programs copied into the derived image run (`id -u` prints a user id that is not 0).
- **F2** `tests/run-e2e.sh` › start_message — the start names the number of jobs from `/etc/cron.d` and the time zone.
- **F2** `tests/run-e2e.sh` › reboot_job_runs_at_start — an `@reboot` job runs at start.
- **F2** `tests/run-e2e.sh` › invalid_file_name_reported — a file named `ignored.file` is reported as ignored.
- **F2** `tests/run-e2e.sh` › ignored_file_not_run — its job never runs.
- **F2** `tests/run-e2e.sh` › check_step — `*/15` from 23:59 is due at 00:00.
- **F2** `tests/run-e2e.sh` › check_list — `30 4 1,15 * *` is due on the next 1st.
- **F2** `tests/run-e2e.sh` › check_weekday_range — `mon-fri` from a Friday night is due on Monday.
- **F2** `tests/run-e2e.sh` › check_leap_day — `0 0 29 2 *` is due on the next 29 February, two years ahead.
- **F2** `tests/run-e2e.sh` › check_dom_or_dow — `0 0 13 * 5` is due on the next Friday before the 13th: day of month OR day of week.
- **F2** `tests/run-e2e.sh` › check_macro — `@monthly` is due at midnight of the 1st.
- **F2** `tests/run-e2e.sh` › check_sunday_is_7 — weekday `7` is Sunday.
- **F2** `tests/run-e2e.sh` › check_range_step — `5-10/2 1 * jan,jul *` is due on 1 July 01:05.
- **F2** `tests/run-e2e.sh` › check_reboot — `@reboot` is listed as due at start.
- **F3** `tests/run-e2e.sh` › periodic_directory_runs — a program in `/etc/periodic/min` runs every minute (`whoami` prints `somebody`).
- **F4** `tests/run-e2e.sh` › quoted_arguments — `echo "two words" 'single $quoted' plain\ escaped` prints `two words single $quoted plain escaped`, programs found in `PATH`.
- **F4** `tests/run-e2e.sh` › invalid_pipe — an unquoted `|` is refused as shell syntax.
- **F4** `tests/run-e2e.sh` › invalid_variable — an unquoted `$` is refused as shell syntax.
- **F4** `tests/run-e2e.sh` › invalid_quote — an unclosed quote is refused.
- **F5** `tests/run-e2e.sh` › env_from_crontab — a `NAME=value` line of the crontab reaches the job.
- **F5** `tests/run-e2e.sh` › env_from_container — a variable of the container reaches the job.
- **F6** `tests/run-e2e.sh` › header_top — the header starts with a line of `=`.
- **F6** `tests/run-e2e.sh` › header_bottom — the header line with file, line and command is followed by a line of `-` and then the output.
- **F7** `tests/run-e2e.sh` › failed_job_logged — `false` is logged as `FAILED` with exit status 1 and duration.
- **F7** `tests/run-e2e.sh` › debug_logs_start_and_end — with `CRON_DEBUG=1`, start and end with exit status 0 are logged.
- **F7** `tests/run-e2e.sh` › invalid_cron_debug_refuses_start — `CRON_DEBUG=verbose` stops the start with a message.
- **F8** `tests/run-e2e.sh` › invalid_exit_status — `cron --check` over a crontab with errors ends with status 1.
- **F8** `tests/run-e2e.sh` › invalid_range — minute `61` is reported with file and line.
- **F8** `tests/run-e2e.sh` › invalid_field_count — a line with too few fields is reported.
- **F8** `tests/run-e2e.sh` › invalid_missing — a program that does not exist is reported.
- **F8** `tests/run-e2e.sh` › invalid_never — `0 0 30 2 *` is reported as matching no date.
- **F8** `tests/run-e2e.sh` › invalid_macro — an unknown `@` schedule is reported with the known ones.
- **F8** `tests/run-e2e.sh` › invalid_not_exec — a file without execute permission is reported.
- **F8** `tests/run-e2e.sh` › invalid_name — an unknown weekday name is reported.
- **F8** `tests/run-e2e.sh` › invalid_valid_line_listed — a valid line between the errors is still listed with its next run.
- **F8** `tests/run-e2e.sh` › broken_crontab_refuses_start — a container whose crontab names a missing program does not start and names file, line and cause.
- **F9** `tests/run-e2e.sh` › check_utc_default — without `TZ` the times are UTC.
- **F9** `tests/run-e2e.sh` › tz_local_midnight — with `TZ=CET-1`, 23:59 UTC is 00:59 local time and the next midnight is a day later.
- **F9** `tests/run-e2e.sh` › tz_local_step — with `TZ=CET-1`, `*/15` after 23:59 UTC is due at 01:00 local time.
- **F10** `tests/run-e2e.sh` › stop_is_immediate_and_clean — `docker compose stop` while `sleep 600` runs ends the container within 10 seconds with exit code 0.
- **F10** `tests/run-e2e.sh` › stop_is_logged — the stop is logged with the number of running jobs.
- **F12** `tests/run-e2e.sh` › variable_in_time_fields_and_command — `${CRON_E2E_SCHEDULE} /bin/echo ${CRON_E2E_WORDS} end` with `*/15 * * * *` and `two words` is due at 00:00 and listed with the values filled in.
- **F12** `tests/run-e2e.sh` › variable_in_quotes — `"${CRON_E2E_WORDS}"` keeps its quotes and becomes one argument.
- **F12** `tests/run-e2e.sh` › variable_errors_reported — an unset variable, `${1BAD}` and an unclosed `${` are reported with file and line, status 1.
- **F12** `tests/run-e2e.sh` › variable_job_runs — a job whose schedule and arguments come from the container environment runs every minute and prints them.
- **F11** `tests/run-e2e.sh` › tmp_and_var_tmp_writable — a job writes to `/tmp` and `/var/tmp`.

## Image contract (`tests/image-contract.sh`)

- **F11** `tests/image-contract.sh` › no sh, no bash, no busybox, no perl, runs unprivileged — for `mwaeckerlin/cron` and the derived `mwaeckerlin/cron-demo`.

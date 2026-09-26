#!/usr/bin/env bash
# End to end tests for mwaeckerlin/cron, against the stack in tests/e2e/.
# The images mwaeckerlin/cron and mwaeckerlin/cron-demo must be built
# first (npm run test:e2e does that).
#
# The jobs run every minute, so the suite waits for the first full minute
# after the start: about one to two minutes.
#
# Usage: bash tests/run-e2e.sh
set -euo pipefail

cd "$(dirname "$0")/e2e"
IMAGE="mwaeckerlin/cron-e2e"

cleanup() {
    docker compose --profile broken down -v --remove-orphans > /dev/null 2>&1 || true
}
trap cleanup EXIT

PASS=0
FAIL=0
declare -a FAILED_NAMES
_pass() { PASS=$((PASS + 1)); echo "  PASS  $1"; }
_fail() { FAIL=$((FAIL + 1)); FAILED_NAMES+=("$1"); echo "  FAIL  $1: $2"; }

_logs() { docker compose logs --no-log-prefix "$1" 2> /dev/null; }

# waits until the log of service $1 contains the fixed string $2
_wait_log() {
    local service="$1" text="$2" tries=90
    while (( tries-- > 0 )); do
        if _logs "${service}" | grep -qF -- "${text}"; then
            return 0
        fi
        sleep 1
    done
    return 1
}

# the output line a job wrote below its header: $1 is the header text
_output_of() {
    _logs cron | grep -A2 -F -- "$1" | sed -n 3p || true
}

_check_line() {
    local name="$1" output="$2" expected="$3"
    if grep -qxF -- "${expected}" <<< "${output}"; then
        _pass "${name}"
    else
        _fail "${name}" "expected line '${expected}' in: ${output}"
    fi
}

echo "==> Building the derived test images"
docker compose --profile broken build

echo "==> Configuration check with a fixed time (--check --at)"
CHECK=$(docker run --rm --pull=never "${IMAGE}" --check /etc/cron-e2e/schedule --at 2026-02-27T23:59)
_check_line check_step           "${CHECK}" "/etc/cron-e2e/schedule:1 next 2026-02-28 00:00 /bin/echo a"
_check_line check_list           "${CHECK}" "/etc/cron-e2e/schedule:2 next 2026-03-01 04:30 /bin/echo b"
_check_line check_weekday_range  "${CHECK}" "/etc/cron-e2e/schedule:3 next 2026-03-02 12:00 /bin/echo c"
_check_line check_leap_day       "${CHECK}" "/etc/cron-e2e/schedule:4 next 2028-02-29 00:00 /bin/echo d"
_check_line check_dom_or_dow     "${CHECK}" "/etc/cron-e2e/schedule:5 next 2026-03-06 00:00 /bin/echo e"
_check_line check_macro          "${CHECK}" "/etc/cron-e2e/schedule:6 next 2026-03-01 00:00 /bin/echo f"
_check_line check_sunday_is_7    "${CHECK}" "/etc/cron-e2e/schedule:7 next 2026-03-01 22:00 /bin/echo g"
_check_line check_range_step     "${CHECK}" "/etc/cron-e2e/schedule:8 next 2026-07-01 01:05 /bin/echo h"
_check_line check_utc_default    "${CHECK}" "/etc/cron-e2e/schedule:9 next 2026-02-28 00:00 /bin/echo i"
_check_line check_reboot         "${CHECK}" "/etc/cron-e2e/schedule:10 next at start /bin/echo j"

echo "==> Time zone (TZ)"
CHECK=$(docker run --rm --pull=never -e TZ=CET-1 "${IMAGE}" --check /etc/cron-e2e/schedule --at 2026-02-27T23:59Z)
_check_line tz_local_midnight "${CHECK}" "/etc/cron-e2e/schedule:9 next 2026-03-01 00:00 /bin/echo i"
_check_line tz_local_step     "${CHECK}" "/etc/cron-e2e/schedule:1 next 2026-02-28 01:00 /bin/echo a"

echo "==> Errors in a crontab are reported with file and line"
set +e
INVALID=$(docker run --rm --pull=never "${IMAGE}" --check /etc/cron-e2e/invalid 2>&1)
RC=$?
set -e
if [[ ${RC} -eq 1 ]]; then _pass invalid_exit_status; else _fail invalid_exit_status "exit status ${RC}, expected 1"; fi
_expect_error() {
    local name="$1" text="$2"
    if grep -qF -- "${text}" <<< "${INVALID}"; then
        _pass "${name}"
    else
        _fail "${name}" "missing '${text}' in: ${INVALID}"
    fi
}
_expect_error invalid_range         "error: /etc/cron-e2e/invalid:1: minute field: '61' is outside 0-59"
_expect_error invalid_field_count   "error: /etc/cron-e2e/invalid:2: a job line needs five time fields and a command"
_expect_error invalid_pipe          "error: /etc/cron-e2e/invalid:3: '|' is shell syntax, but this image has no shell"
_expect_error invalid_variable      "error: /etc/cron-e2e/invalid:4: '\$' is shell syntax, but this image has no shell"
_expect_error invalid_missing       "error: /etc/cron-e2e/invalid:5: program '/usr/bin/does-not-exist' not found"
_expect_error invalid_never         "error: /etc/cron-e2e/invalid:6: the schedule '0 0 30 2 *' matches no date at all"
_expect_error invalid_macro         "error: /etc/cron-e2e/invalid:7: unknown schedule '@sometimes'"
_expect_error invalid_quote         "error: /etc/cron-e2e/invalid:8: the quote \" is not closed"
_expect_error invalid_not_exec      "error: /etc/cron-e2e/invalid:9: '/etc/cron-e2e/schedule' is not executable"
_expect_error invalid_name          "error: /etc/cron-e2e/invalid:10: day of week field: 'xyz' is neither a number nor a name"
if grep -qE '^/etc/cron-e2e/invalid:11 next [0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2} /bin/echo "valid line between the errors"$' <<< "${INVALID}"; then
    _pass invalid_valid_line_listed
else
    _fail invalid_valid_line_listed "the valid line 11 is not listed: ${INVALID}"
fi

echo "==> Environment variables in crontab lines"
CHECK=$(docker run --rm --pull=never -e "CRON_E2E_SCHEDULE=*/15 * * * *" -e "CRON_E2E_WORDS=two words" \
    "${IMAGE}" --check /etc/cron-e2e/variables --at 2026-02-27T23:59)
_check_line variable_in_time_fields_and_command "${CHECK}" "/etc/cron-e2e/variables:1 next 2026-02-28 00:00 /bin/echo two words end"
_check_line variable_in_quotes "${CHECK}" "/etc/cron-e2e/variables:2 next at start /bin/echo \"two words\""
set +e
UNSET=$(docker run --rm --pull=never "${IMAGE}" --check /etc/cron-e2e/unset 2>&1)
RC=$?
set -e
if [[ ${RC} -eq 1 ]] \
    && grep -qF "error: /etc/cron-e2e/unset:1: the environment variable CRON_E2E_NOT_SET is not set" <<< "${UNSET}" \
    && grep -qF "error: /etc/cron-e2e/unset:2: '\${1BAD}' is no variable name" <<< "${UNSET}" \
    && grep -qF "error: /etc/cron-e2e/unset:3: '\${' is not closed by '}'" <<< "${UNSET}"; then
    _pass variable_errors_reported
else
    _fail variable_errors_reported "exit status ${RC}: ${UNSET}"
fi

echo "==> A crontab with an error stops the start"
set +e
BROKEN=$(docker compose --profile broken run --rm broken 2>&1)
RC=$?
set -e
if [[ ${RC} -eq 1 ]] && grep -qF "/etc/cron.d/broken:2: program '/usr/bin/missing-program' not found" <<< "${BROKEN}" \
    && grep -qF "not started, 1 errors in /etc/cron.d" <<< "${BROKEN}"; then
    _pass broken_crontab_refuses_start
else
    _fail broken_crontab_refuses_start "exit status ${RC}: ${BROKEN}"
fi

set +e
BADDEBUG=$(docker run --rm --pull=never -e CRON_DEBUG=verbose "${IMAGE}" 2>&1)
RC=$?
set -e
if [[ ${RC} -eq 1 ]] && grep -qF "CRON_DEBUG must be a number" <<< "${BADDEBUG}"; then
    _pass invalid_cron_debug_refuses_start
else
    _fail invalid_cron_debug_refuses_start "exit status ${RC}: ${BADDEBUG}"
fi

echo "==> Running jobs (waits for the next full minute)"
docker compose up -d cron demo

if _wait_log cron "reboot-marker"; then _pass reboot_job_runs_at_start; else _fail reboot_job_runs_at_start "no reboot-marker in the log"; fi
if _logs cron | grep -qF "**** cron: started with 18 jobs from /etc/cron.d, times in UTC"; then
    _pass start_message
else
    _fail start_message "$(_logs cron | head -5)"
fi
if _logs cron | grep -qF "**** cron: ignoring /etc/cron.d/ignored.file: a file name may only contain letters, digits, '_' and '-'"; then
    _pass invalid_file_name_reported
else
    _fail invalid_file_name_reported "no warning for ignored.file"
fi

_wait_log cron "**** cron: end /etc/cron.d/e2e:9: exit status 0" || true
_wait_log cron "--run-parts /etc/periodic/min" || true
_wait_log demo "RUNNING CRONJOB every minute" || true
sleep 3
LOG=$(_logs cron)

_check_line header_top      "$(_logs cron | grep -B1 -F '/etc/cron.d/e2e:5 echo' | head -1)" "$(printf '=%.0s' {1..100})"
_check_line header_bottom   "$(_logs cron | grep -A1 -F '/etc/cron.d/e2e:5 echo' | sed -n 2p)" "$(printf -- '-%.0s' {1..100})"
_check_line quoted_arguments "$(_output_of '/etc/cron.d/e2e:5 echo')" 'two words single $quoted plain escaped'
_check_line env_from_crontab "$(_logs cron | grep -A3 -F '/etc/cron.d/e2e:6 printenv' | sed -n 3p)" "value from the crontab"
_check_line env_from_container "$(_logs cron | grep -A3 -F '/etc/cron.d/e2e:6 printenv' | sed -n 4p)" "from the container"

UID_LINE=$(_output_of '/etc/cron.d/e2e:7 id -u')
if [[ "${UID_LINE}" =~ ^[0-9]+$ && "${UID_LINE}" != "0" ]]; then
    _pass jobs_run_unprivileged
else
    _fail jobs_run_unprivileged "id -u printed '${UID_LINE}'"
fi

if grep -qE '^\*\*\*\* cron: FAILED /etc/cron.d/e2e:8: exit status 1 after [0-9.]+s: false$' <<< "${LOG}"; then
    _pass failed_job_logged
else
    _fail failed_job_logged "no FAILED line for false"
fi
if grep -qE '^\*\*\*\* cron: start /etc/cron.d/e2e:9: touch /tmp/written /var/tmp/written$' <<< "${LOG}" \
    && grep -qE '^\*\*\*\* cron: end /etc/cron.d/e2e:9: exit status 0 after [0-9.]+s: touch' <<< "${LOG}"; then
    _pass debug_logs_start_and_end
    _pass tmp_and_var_tmp_writable
else
    _fail debug_logs_start_and_end "no start/end lines for the touch job"
    _fail tmp_and_var_tmp_writable "touch /tmp/written /var/tmp/written did not end with status 0"
fi
_check_line variable_job_runs "$(_output_of '/etc/cron.d/e2e:10 echo words from the environment')" "words from the environment"
_check_line periodic_directory_runs "$(_output_of '/etc/cron.d/periodic:6 /usr/bin/cron --run-parts /etc/periodic/min')" "somebody"
if grep -qF "MUST-NOT-RUN" <<< "${LOG}"; then
    _fail ignored_file_not_run "a job of ignored.file ran"
else
    _pass ignored_file_not_run
fi

DEMO=$(_logs demo)
if grep -qF "RUNNING CRONJOB every minute" <<< "${DEMO}" && grep -qE '^[A-Z][a-z]{2} [A-Z][a-z]{2} ' <<< "${DEMO}"; then
    _pass demo_image_runs_its_jobs
else
    _fail demo_image_runs_its_jobs "${DEMO}"
fi
if grep -qF "**** cron:" <<< "${DEMO}" && ! grep -qF "FAILED" <<< "${DEMO}"; then
    _pass demo_without_failures
else
    _fail demo_without_failures "${DEMO}"
fi

echo "==> Stop while a job is running"
START=$(date +%s)
docker compose stop -t 20 cron > /dev/null 2>&1
TOOK=$(( $(date +%s) - START ))
EXIT=$(docker inspect -f '{{.State.ExitCode}}' "$(docker compose ps -a -q cron)")
if [[ "${EXIT}" == "0" && ${TOOK} -lt 10 ]]; then
    _pass stop_is_immediate_and_clean
else
    _fail stop_is_immediate_and_clean "exit code ${EXIT} after ${TOOK}s"
fi
# at least the @reboot job /bin/sleep 600 is still running
if _logs cron | grep -qE '^\*\*\*\* cron: stopped, [1-9][0-9]* running jobs end with the container$'; then
    _pass stop_is_logged
else
    _fail stop_is_logged "$(_logs cron | tail -3)"
fi

echo ""
echo "==> E2E results: ${PASS} passed, ${FAIL} failed"
if [[ ${FAIL} -gt 0 ]]; then
    echo "==> Failed tests: ${FAILED_NAMES[*]}"
    exit 1
fi

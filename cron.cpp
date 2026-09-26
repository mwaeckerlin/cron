/**

Cron: shell-free cron daemon, the entrypoint of mwaeckerlin/cron.

The image has no shell, so a job is never handed to /bin/sh: this program
splits the command line into arguments itself and execv()s the program
directly. A derived image brings the programs its jobs call, plus a
crontab file in /etc/cron.d.

Modes:

  cron                            run the daemon (the image's entrypoint)
  cron --check [--at T] [FILE…]   validate crontabs, print every job's next
                                  run after T (default: now); without FILE
                                  all files in /etc/cron.d
  cron --run-parts DIR            run every executable in DIR in name order
                                  (the /etc/periodic/* directories)

Crontab syntax is the standard five time fields (lists, ranges, steps,
month and weekday names, weekday 0 and 7 for Sunday, day-of-month OR
day-of-week when both are restricted) or one of @reboot, @yearly,
@annually, @monthly, @weekly, @daily, @midnight, @hourly, followed by the
command. A line NAME=value sets an environment variable for the jobs
below it in the same file. Before a line is parsed, every ${NAME} in it
is replaced by that variable of the container's environment, in the time
fields as in the command; a variable that is not set is an error. Command
lines know "double" and 'single'
quotes and backslash escapes; unquoted shell syntax (| & ; < > $ ` ( ))
is refused with a message, because nothing would interpret it.

Every job's output and error output goes to the container log, below a
header naming the job. A failing job is always logged; CRON_DEBUG>=1
also logs every start and end.

On SIGTERM or SIGINT the daemon exits at once with status 0. It is PID 1
of the container, so the kernel ends every running job with it.

*/

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr const char *CRON_DIR = "/etc/cron.d";
constexpr const char *SELF = "/usr/bin/cron";
constexpr const char *DEFAULT_PATH = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
// a line without newline is passed on once it reaches this size
constexpr std::size_t LINE_CHUNK = 64 * 1024;
// a clock jump forward by more minutes than this runs only the current
// minute, so a suspended host or a clock correction does not fire a storm
constexpr long CATCH_UP_MINUTES = 5;
// a schedule that matches no minute within this span never runs
constexpr long SEARCH_MINUTES = 8L * 366 * 24 * 60;
const std::string SEPARATOR_TOP(100, '=');
const std::string SEPARATOR_BOTTOM(100, '-');

struct Schedule {
  bool reboot = false;
  std::vector<bool> minute, hour, dom, month, dow;
  bool dom_star = true, dow_star = true;
};

struct Job {
  std::string file;
  int line = 0;
  std::string command;  // as written, for the log
  Schedule schedule;
  std::vector<std::string> argv;
  std::vector<std::pair<std::string, std::string>> env;
};

std::string where(const Job &job) {
  return job.file + ":" + std::to_string(job.line);
}

std::string trim(const std::string &s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return {};
  return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

bool all_digits(const std::string &s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

bool valid_name(const std::string &name) {
  return !name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '-';
  });
}

const std::vector<std::string> MONTHS = {"jan", "feb", "mar", "apr", "may", "jun",
                                         "jul", "aug", "sep", "oct", "nov", "dec"};
const std::vector<std::string> DAYS = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};

int value_of(const std::string &text, const char *field, const std::vector<std::string> *names, int first) {
  if (names) {
    const auto it = std::find(names->begin(), names->end(), lower(text));
    if (it != names->end()) return first + static_cast<int>(it - names->begin());
  }
  if (!all_digits(text) || text.size() > 4)
    throw std::runtime_error(std::string(field) + " field: '" + text + "' is neither a number" +
                             (names ? " nor a name" : ""));
  return std::stoi(text);
}

// one of the five time fields; returns which values lo..hi match
std::vector<bool> parse_field(const std::string &spec, const char *field, int lo, int hi,
                              const std::vector<std::string> *names = nullptr, int first = 0) {
  std::vector<bool> bits(hi + 1, false);
  std::size_t pos = 0;
  while (pos <= spec.size()) {
    const auto comma = spec.find(',', pos);
    const std::string item = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
    pos = comma == std::string::npos ? spec.size() + 1 : comma + 1;
    if (item.empty()) throw std::runtime_error(std::string(field) + " field: empty entry in '" + spec + "'");
    const auto slash = item.find('/');
    const std::string range = item.substr(0, slash);
    int step = 1;
    if (slash != std::string::npos) {
      step = value_of(item.substr(slash + 1), field, nullptr, 0);
      if (step < 1) throw std::runtime_error(std::string(field) + " field: step in '" + item + "' must be at least 1");
    }
    int from = lo, to = hi;
    if (range != "*") {
      const auto dash = range.find('-');
      from = value_of(range.substr(0, dash), field, names, first);
      to = dash == std::string::npos ? (slash == std::string::npos ? from : hi)
                                     : value_of(range.substr(dash + 1), field, names, first);
    }
    if (from < lo || to > hi || from > to)
      throw std::runtime_error(std::string(field) + " field: '" + item + "' is outside " + std::to_string(lo) +
                               "-" + std::to_string(hi));
    for (int v = from; v <= to; v += step) bits[v] = true;
  }
  return bits;
}

Schedule parse_schedule(const std::vector<std::string> &f) {
  Schedule s;
  s.minute = parse_field(f[0], "minute", 0, 59);
  s.hour = parse_field(f[1], "hour", 0, 23);
  s.dom = parse_field(f[2], "day of month", 1, 31);
  s.month = parse_field(f[3], "month", 1, 12, &MONTHS, 1);
  s.dow = parse_field(f[4], "day of week", 0, 7, &DAYS, 0);
  if (s.dow[7]) s.dow[0] = true;  // 7 is Sunday as well
  s.dom_star = f[2][0] == '*';
  s.dow_star = f[4][0] == '*';
  return s;
}

const std::map<std::string, std::string> MACROS = {
    {"@yearly", "0 0 1 1 *"}, {"@annually", "0 0 1 1 *"}, {"@monthly", "0 0 1 * *"},
    {"@weekly", "0 0 * * 0"}, {"@daily", "0 0 * * *"},    {"@midnight", "0 0 * * *"},
    {"@hourly", "0 * * * *"}};

// splits a command line into arguments the way a shell does for plain
// words and quotes, and refuses everything a shell would interpret
std::vector<std::string> split_command(const std::string &text) {
  std::vector<std::string> args;
  std::string current;
  bool in_word = false;
  char quote = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (quote) {
      if (c == quote) {
        quote = 0;
      } else if (quote == '"' && c == '\\' && i + 1 < text.size() &&
                 std::string("\"\\$`").find(text[i + 1]) != std::string::npos) {
        current += text[++i];
      } else {
        current += c;
      }
    } else if (c == ' ' || c == '\t') {
      if (in_word) args.push_back(current);
      current.clear();
      in_word = false;
    } else if (c == '\'' || c == '"') {
      quote = c;
      in_word = true;
    } else if (c == '\\') {
      if (i + 1 >= text.size()) throw std::runtime_error("the command ends with a backslash");
      current += text[++i];
      in_word = true;
    } else if (std::string("|&;<>$`()").find(c) != std::string::npos) {
      throw std::runtime_error(std::string("'") + c +
                               "' is shell syntax, but this image has no shell: the command runs directly. "
                               "Quote it to pass it on as text, or move the logic into a program of its own");
    } else {
      current += c;
      in_word = true;
    }
  }
  if (quote) throw std::runtime_error(std::string("the quote ") + quote + " is not closed");
  if (in_word) args.push_back(current);
  if (args.empty()) throw std::runtime_error("the command is missing");
  return args;
}

// replaces every ${NAME} in a crontab line by that variable of the
// container's environment, before the line is parsed; the value is split
// into arguments like the rest of the line and never reaches a shell
std::string expand(const std::string &line) {
  std::string out;
  for (std::size_t pos = 0; pos < line.size();) {
    const auto start = line.find("${", pos);
    if (start == std::string::npos) {
      out += line.substr(pos);
      break;
    }
    out += line.substr(pos, start - pos);
    const auto end = line.find('}', start + 2);
    if (end == std::string::npos) throw std::runtime_error("'${' is not closed by '}'");
    const std::string name = line.substr(start + 2, end - start - 2);
    if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0])) ||
        !std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; }))
      throw std::runtime_error("'${" + name + "}' is no variable name: letters, digits and '_', not starting "
                               "with a digit");
    const char *value = std::getenv(name.c_str());
    if (!value) throw std::runtime_error("the environment variable " + name + " is not set");
    out += value;
    pos = end + 1;
  }
  return out;
}

// the program a job will execute, found the way execvp() finds it
std::string resolve(const std::string &program) {
  std::vector<std::string> candidates;
  if (program.find('/') != std::string::npos) {
    candidates.push_back(program);
  } else {
    const char *p = std::getenv("PATH");
    const std::string path = (p && *p) ? p : DEFAULT_PATH;
    for (std::size_t pos = 0; pos <= path.size();) {
      auto colon = path.find(':', pos);
      if (colon == std::string::npos) colon = path.size();
      candidates.push_back((colon == pos ? std::string(".") : path.substr(pos, colon - pos)) + "/" + program);
      pos = colon + 1;
    }
  }
  for (const auto &c : candidates) {
    struct stat st{};
    if (stat(c.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
      if (access(c.c_str(), X_OK) == 0) return c;
      throw std::runtime_error("'" + c + "' is not executable");
    }
  }
  throw std::runtime_error("program '" + program + "' not found" +
                           (program.find('/') == std::string::npos ? " in PATH" : "") +
                           ", the derived image has to bring it along");
}

// the first `count` whitespace separated words of s; `rest` is the remainder
std::vector<std::string> words(const std::string &s, std::size_t count, std::string &rest) {
  std::vector<std::string> out;
  std::size_t pos = 0;
  while (out.size() < count) {
    const auto b = s.find_first_not_of(" \t", pos);
    if (b == std::string::npos) break;
    const auto e = s.find_first_of(" \t", b);
    out.push_back(s.substr(b, e == std::string::npos ? std::string::npos : e - b));
    pos = e == std::string::npos ? s.size() : e;
  }
  rest = trim(s.substr(std::min(pos, s.size())));
  return out;
}

bool matches(const Schedule &s, const std::tm &t) {
  if (!s.minute[t.tm_min] || !s.hour[t.tm_hour] || !s.month[t.tm_mon + 1]) return false;
  const bool dom = s.dom[t.tm_mday], dow = s.dow[t.tm_wday];
  return (s.dom_star || s.dow_star) ? (dom && dow) : (dom || dow);
}

// first minute strictly after the minute of `after` that matches, or -1
time_t next_run(const Schedule &s, time_t after) {
  time_t t = (after / 60 + 1) * 60;
  for (long i = 0; i < SEARCH_MINUTES; ++i, t += 60) {
    std::tm tm{};
    localtime_r(&t, &tm);
    if (matches(s, tm)) return t;
  }
  return -1;
}

std::string format_time(time_t t) {
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
  return buf;
}

// reads one crontab file; every error is collected with its file and line
void load_file(const std::string &path, std::vector<Job> &jobs, std::vector<std::string> &errors) {
  std::ifstream in(path);
  if (!in) {
    errors.push_back(path + ": cannot read the file: " + std::strerror(errno));
    return;
  }
  std::vector<std::pair<std::string, std::string>> env;
  std::string raw;
  for (int n = 1; std::getline(in, raw); ++n) {
    std::string line = trim(raw);
    if (line.empty() || line[0] == '#') continue;
    Job job;
    job.file = path;
    job.line = n;
    try {
      line = trim(expand(line));
      if (line.empty()) throw std::runtime_error("the line is empty once its variables are filled in");
      const auto eq = line.find('=');
      const std::string name = eq == std::string::npos ? std::string() : trim(line.substr(0, eq));
      if (!name.empty() && !std::isdigit(static_cast<unsigned char>(name[0])) &&
          std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; })) {
        std::string value = trim(line.substr(eq + 1));
        if (value.size() >= 2 && (value[0] == '"' || value[0] == '\'') && value.back() == value[0])
          value = value.substr(1, value.size() - 2);
        env.emplace_back(name, value);
        continue;
      }
      if (line[0] == '@') {
        const auto fields = words(line, 1, job.command);
        const std::string timing = lower(fields[0]);
        if (timing == "@reboot") {
          job.schedule.reboot = true;
        } else {
          const auto macro = MACROS.find(timing);
          if (macro == MACROS.end())
            throw std::runtime_error("unknown schedule '" + fields[0] +
                                     "', known are @reboot, @yearly, @annually, @monthly, @weekly, @daily, "
                                     "@midnight, @hourly");
          std::string none;
          job.schedule = parse_schedule(words(macro->second, 5, none));
        }
      } else {
        const auto fields = words(line, 5, job.command);
        if (fields.size() < 5 || job.command.empty())
          throw std::runtime_error("a job line needs five time fields and a command: minute hour day-of-month "
                                   "month day-of-week command");
        job.schedule = parse_schedule(fields);
        if (next_run(job.schedule, std::time(nullptr)) < 0)
          throw std::runtime_error("the schedule '" + fields[0] + " " + fields[1] + " " + fields[2] + " " +
                                   fields[3] + " " + fields[4] + "' matches no date at all");
      }
      job.argv = split_command(job.command);
      job.argv[0] = resolve(job.argv[0]);
      job.env = env;
      jobs.push_back(std::move(job));
    } catch (const std::exception &e) {
      errors.push_back(path + ":" + std::to_string(n) + ": " + e.what() + "\n    " + line);
    }
  }
}

// the names a crontab or periodic directory takes, sorted; every other
// name is reported, never dropped without a word
std::vector<std::string> directory_entries(const std::string &dir, std::vector<std::string> &warnings) {
  DIR *d = opendir(dir.c_str());
  if (!d) throw std::runtime_error("cannot open directory " + dir + ": " + std::strerror(errno));
  std::vector<std::string> names;
  while (const dirent *e = readdir(d)) {
    const std::string name = e->d_name;
    if (name == "." || name == "..") continue;
    if (!valid_name(name)) {
      warnings.push_back("ignoring " + dir + "/" + name +
                         ": a file name may only contain letters, digits, '_' and '-'");
      continue;
    }
    names.push_back(name);
  }
  closedir(d);
  std::sort(names.begin(), names.end());
  return names;
}

void load_directory(const std::string &dir, std::vector<Job> &jobs, std::vector<std::string> &errors,
                    std::vector<std::string> &warnings) {
  for (const auto &name : directory_entries(dir, warnings)) {
    const std::string path = dir + "/" + name;
    struct stat st{};
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
      warnings.push_back("ignoring " + path + ": not a regular file");
      continue;
    }
    load_file(path, jobs, errors);
  }
}

int debug_level() {
  const char *v = std::getenv("CRON_DEBUG");
  if (!v || !*v) return 0;
  if (!all_digits(v) || std::strlen(v) > 4)
    throw std::runtime_error(std::string("CRON_DEBUG must be a number (0: only failures, 1 or more: every "
                                         "start and end), but is '") + v + "'");
  return std::atoi(v);
}

std::string timezone_name() {
  const char *tz = std::getenv("TZ");
  return (tz && *tz) ? std::string(tz) : std::string("UTC");
}

//////////////////////////////////////////////////////////////////// --check

time_t parse_at(const std::string &text) {
  std::tm tm{};
  char zone = 0;
  const int n = std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d%c", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                            &tm.tm_hour, &tm.tm_min, &zone);
  if (n < 5 || (n == 6 && zone != 'Z'))
    throw std::runtime_error("--at expects YYYY-MM-DDTHH:MM (local time) or YYYY-MM-DDTHH:MMZ (UTC), got '" +
                             text + "'");
  tm.tm_year -= 1900;
  tm.tm_mon -= 1;
  tm.tm_isdst = -1;
  return zone == 'Z' ? timegm(&tm) : std::mktime(&tm);
}

int check(const std::vector<std::string> &args) {
  time_t at = std::time(nullptr);
  std::vector<std::string> files;
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--at") {
      if (i + 1 >= args.size()) throw std::runtime_error("--at needs a time");
      at = parse_at(args[++i]);
    } else {
      files.push_back(args[i]);
    }
  }
  std::vector<Job> jobs;
  std::vector<std::string> errors, warnings;
  if (files.empty()) load_directory(CRON_DIR, jobs, errors, warnings);
  for (const auto &f : files) load_file(f, jobs, errors);
  for (const auto &w : warnings) std::cerr << "warning: " << w << std::endl;
  for (const auto &job : jobs)
    std::cout << where(job) << " next "
              << (job.schedule.reboot ? std::string("at start") : format_time(next_run(job.schedule, at))) << " "
              << job.command << std::endl;
  for (const auto &e : errors) std::cerr << "error: " << e << std::endl;
  return errors.empty() ? 0 : 1;
}

//////////////////////////////////////////////////////////////// --run-parts

std::string describe(int status) {
  return WIFEXITED(status) ? "exit status " + std::to_string(WEXITSTATUS(status))
                           : "ended by signal " + std::to_string(WTERMSIG(status));
}

int run_parts(const std::string &dir) {
  std::vector<std::string> warnings;
  const auto names = directory_entries(dir, warnings);
  for (const auto &w : warnings) std::cerr << "**** cron: " << w << std::endl;
  int result = 0;
  for (const auto &name : names) {
    const std::string path = dir + "/" + name;
    struct stat st{};
    if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || access(path.c_str(), X_OK) != 0) continue;
    const pid_t pid = fork();
    if (pid < 0) throw std::runtime_error(std::string("fork: ") + std::strerror(errno));
    if (pid == 0) {
      const char *argv[] = {path.c_str(), nullptr};
      execv(path.c_str(), const_cast<char *const *>(argv));
      std::cerr << "**** cron: cannot execute " << path << ": " << std::strerror(errno) << std::endl;
      _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      std::cerr << "**** cron: " << path << " " << describe(status) << std::endl;
      result = 1;
    }
  }
  return result;
}

/////////////////////////////////////////////////////////////////// daemon

int signal_pipe[2] = {-1, -1};

void on_signal(int sig) {
  const int saved = errno;
  const unsigned char c = static_cast<unsigned char>(sig);
  (void)!write(signal_pipe[1], &c, 1);
  errno = saved;
}

void write_all(int fd, const std::string &text) {
  for (std::size_t done = 0; done < text.size();) {
    const ssize_t n = write(fd, text.data() + done, text.size() - done);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return;
    done += static_cast<std::size_t>(n);
  }
}

// the output pipe of one job run; it may outlive the job when the job
// left a process behind that still writes to it
struct Output {
  int fd;
  long run;
  const Job *job;
  std::string partial;
};

struct Running {
  long run;
  const Job *job;
  timespec start;
};

class Daemon {
public:
  Daemon(std::vector<Job> jobs, int debug) : jobs_(std::move(jobs)), debug_(debug) {}

  int run() {
    if (pipe2(signal_pipe, O_CLOEXEC | O_NONBLOCK) != 0)
      throw std::runtime_error(std::string("pipe: ") + std::strerror(errno));
    struct sigaction sa{};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    for (int sig : {SIGTERM, SIGINT, SIGCHLD}) sigaction(sig, &sa, nullptr);

    log("started with " + std::to_string(jobs_.size()) + " jobs from " + CRON_DIR + ", times in " +
        timezone_name());
    for (const auto &job : jobs_)
      if (job.schedule.reboot) spawn(job);
    long last = std::time(nullptr) / 60;

    while (!stopping_) {
      timespec now{};
      clock_gettime(CLOCK_REALTIME, &now);
      const long wait_ms = ((last + 1) * 60 - now.tv_sec) * 1000 - now.tv_nsec / 1000000;
      std::vector<pollfd> fds{{signal_pipe[0], POLLIN, 0}};
      for (const auto &o : outputs_) fds.push_back({o.fd, POLLIN, 0});
      if (poll(fds.data(), fds.size(), static_cast<int>(std::clamp(wait_ms, 0L, 60000L))) < 0 && errno != EINTR)
        throw std::runtime_error(std::string("poll: ") + std::strerror(errno));
      for (std::size_t i = 1; i < fds.size(); ++i)
        if (fds[i].revents) read_output(fds[i].fd);
      reap();
      handle_signals();
      if (stopping_) break;
      const long minute = std::time(nullptr) / 60;
      if (minute <= last) {
        last = std::min(last, minute);  // the clock went back: nothing runs twice
        continue;
      }
      for (long m = (minute - last > CATCH_UP_MINUTES ? minute : last + 1); m <= minute; ++m) {
        const time_t t = m * 60;
        std::tm tm{};
        localtime_r(&t, &tm);
        for (const auto &job : jobs_)
          if (!job.schedule.reboot && matches(job.schedule, tm)) spawn(job);
      }
      last = minute;
    }
    log("stopped, " + std::to_string(running_.size()) + " running jobs end with the container");
    return 0;
  }

private:
  void log(const std::string &text) {
    write_all(1, "**** cron: " + text + "\n");
    last_writer_ = 0;
  }

  // output of a job, below a header whenever another writer came between
  void emit(Output &o, const std::string &text) {
    if (last_writer_ != o.run)
      write_all(1, SEPARATOR_TOP + "\n" + where(*o.job) + " " + o.job->command + "\n" + SEPARATOR_BOTTOM + "\n");
    last_writer_ = o.run;
    write_all(1, text);
  }

  void spawn(const Job &job) {
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
      log("FAILED " + where(job) + ": cannot start, pipe: " + std::strerror(errno));
      return;
    }
    const pid_t pid = fork();
    if (pid < 0) {
      log("FAILED " + where(job) + ": cannot start, fork: " + std::strerror(errno));
      close(fds[0]);
      close(fds[1]);
      return;
    }
    if (pid == 0) {
      struct sigaction sa{};
      sa.sa_handler = SIG_DFL;
      for (int sig : {SIGTERM, SIGINT, SIGCHLD}) sigaction(sig, &sa, nullptr);
      dup2(fds[1], 1);
      dup2(fds[1], 2);
      for (const auto &[name, value] : job.env) setenv(name.c_str(), value.c_str(), 1);
      const char *home = std::getenv("HOME");
      if (!home || chdir(home) != 0) (void)!chdir("/");
      std::vector<char *> argv;
      for (const auto &a : job.argv) argv.push_back(const_cast<char *>(a.c_str()));
      argv.push_back(nullptr);
      execv(argv[0], argv.data());
      std::cerr << "cannot execute " << job.argv[0] << ": " << std::strerror(errno) << std::endl;
      _exit(127);
    }
    close(fds[1]);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    const long id = ++runs_;
    timespec start{};
    clock_gettime(CLOCK_MONOTONIC, &start);
    running_[pid] = {id, &job, start};
    outputs_.push_back({fds[0], id, &job, {}});
    if (debug_ >= 1) log("start " + where(job) + ": " + job.command);
  }

  void read_output(int fd) {
    const auto it = std::find_if(outputs_.begin(), outputs_.end(), [fd](const Output &o) { return o.fd == fd; });
    if (it == outputs_.end()) return;
    char buf[8192];
    for (;;) {
      const ssize_t n = read(fd, buf, sizeof buf);
      if (n > 0) {
        it->partial.append(buf, static_cast<std::size_t>(n));
        const auto nl = it->partial.rfind('\n');
        if (nl != std::string::npos) {
          emit(*it, it->partial.substr(0, nl + 1));
          it->partial.erase(0, nl + 1);
        }
        if (it->partial.size() >= LINE_CHUNK) {
          emit(*it, it->partial);
          it->partial.clear();
        }
        continue;
      }
      if (n < 0 && errno == EINTR) continue;
      if (n < 0 && errno == EAGAIN) return;
      if (!it->partial.empty()) emit(*it, it->partial + "\n");  // end of file or a read error
      close(it->fd);
      outputs_.erase(it);
      return;
    }
  }

  void reap() {
    int status = 0;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
      const auto it = running_.find(pid);
      if (it == running_.end()) continue;  // an orphan adopted by PID 1
      // output written before the exit belongs above the status line
      for (std::size_t i = 0; i < outputs_.size(); ++i)
        if (outputs_[i].run == it->second.run) {
          read_output(outputs_[i].fd);
          break;
        }
      timespec end{};
      clock_gettime(CLOCK_MONOTONIC, &end);
      char took[32];
      std::snprintf(took, sizeof took, "%.1fs",
                    (end.tv_sec - it->second.start.tv_sec) + (end.tv_nsec - it->second.start.tv_nsec) / 1e9);
      const Job &job = *it->second.job;
      if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        log("FAILED " + where(job) + ": " + describe(status) + " after " + took + ": " + job.command);
      else if (debug_ >= 1)
        log("end " + where(job) + ": exit status 0 after " + took + ": " + job.command);
      running_.erase(it);
    }
  }

  void handle_signals() {
    unsigned char sig;
    while (read(signal_pipe[0], &sig, 1) == 1)
      if (sig == SIGTERM || sig == SIGINT) stopping_ = true;
  }

  std::vector<Job> jobs_;
  int debug_;
  std::map<pid_t, Running> running_;
  std::vector<Output> outputs_;
  long runs_ = 0;
  long last_writer_ = 0;
  bool stopping_ = false;
};

int daemon_main() {
  const int debug = debug_level();
  std::vector<Job> jobs;
  std::vector<std::string> errors, warnings;
  load_directory(CRON_DIR, jobs, errors, warnings);
  for (const auto &w : warnings) std::cout << "**** cron: " << w << std::endl;
  if (!errors.empty()) {
    for (const auto &e : errors) std::cerr << "**** cron: ERROR " << e << std::endl;
    std::cerr << "**** cron: not started, " << errors.size() << " errors in " << CRON_DIR << std::endl;
    return 1;
  }
  std::cout.flush();
  return Daemon(std::move(jobs), debug).run();
}

void usage(std::ostream &out) {
  out << "usage: " << SELF << "                         run the cron daemon\n"
      << "       " << SELF << " --check [--at T] [FILE…] validate crontabs, show each job's next run\n"
      << "       " << SELF << " --run-parts DIR          run every executable in DIR in name order\n";
}

} // namespace

int main(int argc, char *argv[]) try {
  const std::vector<std::string> args(argv + 1, argv + argc);
  if (args.empty()) return daemon_main();
  if (args[0] == "--check") return check({args.begin() + 1, args.end()});
  if (args[0] == "--run-parts" && args.size() == 2) return run_parts(args[1]);
  if (args[0] == "--help") {
    usage(std::cout);
    return 0;
  }
  std::cerr << "**** cron: unknown arguments" << std::endl;
  usage(std::cerr);
  return 2;
} catch (const std::exception &e) {
  std::cerr << "**** cron: ERROR " << e.what() << std::endl;
  return 1;
} catch (...) {
  std::cerr << "**** cron: UNKNOWN ERROR" << std::endl;
  return 1;
}

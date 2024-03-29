/* Copyright (c) 2020-2023, Michael Santos <michael.santos@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "restrict_process.h"
#ifdef RESTRICT_PROCESS_capsicum
#include <sys/capsicum.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <dirent.h>
#include <stdlib.h>

#include <errno.h>

static int fdlimit(int lowfd, cap_rights_t *policy);
static int isnum(const char *s);
static int fdlimit_range(int lowfd, cap_rights_t *policy);

int restrict_process_init(void) { return 0; }

int restrict_process_signal_on_supervisor_exit(void) { return 0; }

int restrict_process(void) {
  cap_rights_t policy_read;
  cap_rights_t policy_write;
  cap_rights_t policy_rw;
  struct rlimit rl = {0};
  struct stat sb = {0};

  if (fstat(STDOUT_FILENO, &sb) < 0)
    return -1;

  if (!S_ISREG(sb.st_mode)) {
    if (setrlimit(RLIMIT_FSIZE, &rl) < 0)
      return -1;
  }

  (void)cap_rights_init(&policy_read, CAP_READ, CAP_EVENT);
  (void)cap_rights_init(&policy_write, CAP_WRITE, CAP_EVENT);
  (void)cap_rights_init(&policy_rw, CAP_READ, CAP_WRITE, CAP_EVENT, CAP_PDKILL);

  if (cap_rights_limit(STDIN_FILENO, &policy_read) < 0)
    return -1;

  if (cap_rights_limit(STDOUT_FILENO, &policy_write) < 0)
    return -1;

  if (cap_rights_limit(STDERR_FILENO, &policy_write) < 0)
    return -1;

  if (fdlimit(STDERR_FILENO + 1, &policy_rw) < 0)
    return -1;

  return cap_enter();
}

static int fdlimit(int lowfd, cap_rights_t *policy) {
  DIR *dp;
  int dfd;
  struct dirent *de;
  int fd;

  dp = opendir("/dev/fd");
  if (dp == NULL) {
    return fdlimit_range(lowfd, policy);
  }

  dfd = dirfd(dp);
  if (dfd == -1) {
    (void)closedir(dp);
    return fdlimit_range(lowfd, policy);
  }

  while ((de = readdir(dp)) != NULL) {
    if (!isnum(de->d_name))
      continue;

    fd = atoi(de->d_name);

    if (fd < lowfd || fd == dfd)
      continue;

    if (cap_rights_limit(fd, policy) < 0) {
      int errnum = errno;
      (void)closedir(dp);
      errno = errnum;
      return -1;
    }
  }

  return closedir(dp);
}

static int isnum(const char *s) {
  const char *p;

  for (p = s; *p != '\0'; p++) {
    if (*p < '0' || *p > '9')
      return 0;
  }

  return 1;
}

static int fdlimit_range(int lowfd, cap_rights_t *policy) {
  struct rlimit rl = {0};
  int fd;

  if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
    return -1;

  for (fd = rl.rlim_cur; fd >= lowfd; fd--) {
    if (fcntl(fd, F_GETFD, 0) == -1)
      continue;

    if (cap_rights_limit(fd, policy) < 0)
      return -1;
  }

  return 0;
}
#endif

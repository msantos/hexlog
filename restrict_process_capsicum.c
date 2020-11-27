/* Copyright (c) 2020, Michael Santos <michael.santos@gmail.com>
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
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <errno.h>

int restrict_process_init(void) { return 0; }

int restrict_process_signal_on_supervisor_exit(void) { return 0; }

int restrict_process(void) {
  int fd;
  struct rlimit rl = {0};
  cap_rights_t policy_read;
  cap_rights_t policy_write;
  cap_rights_t policy_rw;

  (void)cap_rights_init(&policy_read, CAP_READ);
  (void)cap_rights_init(&policy_write, CAP_WRITE);
  (void)cap_rights_init(&policy_rw, CAP_READ, CAP_WRITE, CAP_FSTAT, CAP_FCNTL,
                        CAP_EVENT);

  if (cap_rights_limit(STDIN_FILENO, &policy_read) < 0)
    return -1;

  if (cap_rights_limit(STDOUT_FILENO, &policy_write) < 0)
    return -1;

  if (cap_rights_limit(STDERR_FILENO, &policy_write) < 0)
    return -1;

  if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
    return -1;

  for (fd = STDERR_FILENO + 1; fd < rl.rlim_cur; fd++) {
    if (fcntl(fd, F_GETFD, 0) < 0)
      continue;

    if (cap_rights_limit(fd, &policy_rw) < 0)
      return -1;
  }

  return cap_enter();
}
#endif
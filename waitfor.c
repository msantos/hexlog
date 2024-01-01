/* Copyright (c) 2019-2024, Michael Santos <michael.santos@gmail.com>
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
#include <errno.h>
#include <signal.h>

#ifdef RESTRICT_PROCESS_capsicum
#include <stdlib.h>
#include <sys/event.h>
#include <unistd.h>
#endif

#include "waitfor.h"

int waitfor(int fdp, int *status) {
#ifdef RESTRICT_PROCESS_capsicum
  struct kevent changelist;
  struct kevent eventlist;
  int kq;
  int rv;

  kq = kqueue();
  if (kq == -1)
    return -1;

  EV_SET(&changelist, fdp, EVFILT_PROCDESC, EV_ADD | EV_CLEAR, NOTE_EXIT, 0,
         NULL);

  rv = kevent(kq, &changelist, 1, NULL, 0, NULL);
  if (rv == -1 || (changelist.flags & EV_ERROR)) {
    (void)close(kq);
    return -1;
  }

  for (;;) {
    rv = kevent(kq, NULL, 0, &eventlist, 1, NULL);
    if (rv < 1) {
      if (errno == EINTR)
        continue;
      (void)close(kq);
      return -1;
    }

    *status = (int)eventlist.data;
    return close(kq);
  }
#else
  (void)fdp;
  for (;;) {
    errno = 0;
    if (wait(status) < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    return 0;
  }
#endif
}

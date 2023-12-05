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
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <unistd.h>

#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <poll.h>

#ifdef RESTRICT_PROCESS_capsicum
#include <sys/procdesc.h>
#endif

#include "restrict_process.h"
#include "waitfor.h"

#define HEXLOG_VERSION "0.5.2"

#define COUNT(_array) (sizeof(_array) / sizeof(_array[0]))

enum {
  NONE = 0,
  IN = 1,
  OUT = 2,
};

typedef struct {
  int fdin;
  int fdout;
  FILE *fdhex;
  char *label;
  char buf[8192]; /* XXX */
  size_t off;
} hexlog_t;

typedef struct {
  pid_t pid;
  int fdp;
  int fdsig;
  int dir_initial;
  int dir_cur;
  int raw;
  unsigned int timeout;
} state_t;

extern const char *__progname;

static int sigfd;

static int direction(state_t *s, char *name);
static int relay(state_t *s, hexlog_t *h);
static int event_loop(state_t *s, hexlog_t h[2]);
static ssize_t hexdump(FILE *stream, const char *label, const void *data,
                       size_t size, int raw);
static int hexlog_write(int fd, void *buf, size_t size);
static int hexlog_flush(state_t *s, hexlog_t h[2]);

static int signal_init(void (*handler)(int));
void sighandler(int sig);
static int sigread(state_t *s);

static noreturn void usage(void);

void sighandler(int sig) {
  if (write(sigfd, &sig, sizeof(sig)) < 0)
    (void)close(sigfd);
}

int main(int argc, char *argv[]) {
  pid_t pid;
  int fdin[2];
  int fdout[2];
  int fdsig[2];
  int oerrno;
  int rv;
  int status = 0;
  int fdp = -1; /* capsicum: process descriptor */
  char *stream;
  char *timeout;

  state_t s = {0};
  hexlog_t h[2] = {0};

  if (restrict_process_init() < 0)
    err(111, "process restriction failed");

  if (setvbuf(stdout, NULL, _IOLBF, 0) < 0)
    err(111, "setvbuf");

  if (argc < 3)
    usage();

  if (direction(&s, argv[1]) < 0)
    usage();

  timeout = getenv("HEXLOG_TIMEOUT");
  if (timeout != NULL) {
    s.timeout = (unsigned)atoi(timeout);
  }

  h[0].fdhex = stderr;
  stream = getenv("HEXLOG_FD_STDIN");
  if (stream != NULL) {
    h[0].fdhex = fdopen(atoi(stream), "w");
    if (h[0].fdhex == NULL)
      err(111, "fdopen: stdin: %s", stream);
  }

  h[1].fdhex = stderr;
  stream = getenv("HEXLOG_FD_STDOUT");
  if (stream != NULL) {
    h[1].fdhex = fdopen(atoi(stream), "w");
    if (h[1].fdhex == NULL)
      err(111, "fdopen: stdout: %s", stream);
  }

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fdsig) < 0)
    err(111, "socketpair");

  sigfd = fdsig[0];

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fdin) < 0)
    err(111, "socketpair");

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fdout) < 0)
    err(111, "socketpair");

  if (signal_init(sighandler) < 0)
    err(111, "signal_init");

#ifdef RESTRICT_PROCESS_capsicum
  pid = pdfork(&fdp, PD_CLOEXEC);
#else
  pid = fork();
#endif

  switch (pid) {
  case -1:
    err(111, "fork");

  case 0:
    if (setsid() < 0)
      err(111, "setsid");

    if (restrict_process_signal_on_supervisor_exit() < 0)
      err(111, "restrict_process_signal_on_supervisor_exit");

    if ((close(fdin[1]) < 0) || (close(fdout[1]) < 0) ||
        (close(fdsig[0]) < 0) || (close(fdsig[1]) < 0))
      exit(111);

    if (dup2(fdin[0], STDIN_FILENO) < 0)
      exit(111);

    if (close(fdin[0]) < 0)
      exit(111);

    if (dup2(fdout[0], STDOUT_FILENO) < 0)
      exit(111);

    if (close(fdout[0]) < 0)
      exit(111);

    (void)execvp(argv[2], argv + 2);

    err(127, "%s", argv[2]);

  default:
    break;
  }

  if (restrict_process() < 0)
    err(111, "process restriction failed");

  if (close(fdin[0]) < 0)
    exit(111);

  if (close(fdout[0]) < 0)
    exit(111);

  s.pid = pid;
  s.fdp = fdp;
  s.fdsig = fdsig[1];

  h[0].fdin = STDIN_FILENO;
  h[0].fdout = fdin[1];
  h[0].label = getenv("HEXLOG_LABEL_STDIN");
  if (h[0].label == NULL)
    h[0].label = " (0)";

  h[1].fdin = fdout[1];
  h[1].fdout = STDOUT_FILENO;
  h[1].label = getenv("HEXLOG_LABEL_STDOUT");
  if (h[1].label == NULL)
    h[1].label = " (1)";

  rv = event_loop(&s, h);
  oerrno = errno;

  (void)hexlog_flush(&s, h);

  if (rv < 0) {
    errno = oerrno;
    err(111, "event_loop");
  }

  if (waitfor(fdp, &status) < 0)
    err(111, "waitfor");

  if (WIFEXITED(status))
    exit(WEXITSTATUS(status));

  if (WIFSIGNALED(status))
    exit(128 + WTERMSIG(status));

  exit(0);
}

static int signal_init(void (*handler)(int)) {
  struct sigaction act = {0};

  act.sa_handler = handler;
  (void)sigfillset(&act.sa_mask);

  if (sigaction(SIGCHLD, &act, NULL) < 0)
    return -1;

  if (sigaction(SIGHUP, &act, NULL) < 0)
    return -1;

  if (sigaction(SIGUSR1, &act, NULL) < 0)
    return -1;

  if (sigaction(SIGUSR2, &act, NULL) < 0)
    return -1;

  if (sigaction(SIGINT, &act, NULL) < 0)
    return -1;

  if (sigaction(SIGTERM, &act, NULL) < 0)
    return -1;

  if (sigaction(SIGALRM, &act, NULL) < 0)
    return -1;

  return 0;
}

static int event_loop(state_t *s, hexlog_t h[2]) {
  struct pollfd rfd[5] = {0};

  rfd[0].fd = h[0].fdin; /* read: parent: STDIN_FILENO */
  rfd[1].fd = h[1].fdin; /* read: child: STDOUT_FILENO */
  rfd[2].fd = s->fdsig;  /* read: parent: signal fd */

  rfd[3].fd = h[0].fdout; /* write: child STDIN_FILENO */

  rfd[4].fd = s->fdp; /* POLLHUP: parent: indicate child exit */

  rfd[0].events = POLLIN; /* read: parent: STDIN_FILENO */
  rfd[1].events = POLLIN; /* read: child: STDOUT_FILENO */
  rfd[2].events = POLLIN; /* read: signal fd */

  for (;;) {
    if (s->timeout > 0)
      alarm(s->timeout);

    if (poll(rfd, COUNT(rfd), -1) < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    if (rfd[3].revents & POLLHUP) {
      // subprocess closed stdin, ignore stdin
      if (close(h[0].fdout) < 0)
        return -1;
      if (close(h[0].fdin) < 0)
        return -1;
      rfd[0].fd = -1;
      rfd[3].fd = -1;
      continue;
    }
    if (rfd[0].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) {
      switch (relay(s, &h[0])) {
      case 0:
        if (close(h[0].fdout) < 0)
          return -1;
        if (close(h[0].fdin) < 0)
          return -1;
        rfd[0].fd = -1;
        rfd[3].fd = -1;
        break;
      case -1:
        return -1;
      default:
        break;
      }
    }

    if (rfd[1].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) {
      switch (relay(s, &h[1])) {
      case 0:
        if (close(h[1].fdout) < 0)
          return -1;
        if (close(h[1].fdin) < 0)
          return -1;
        rfd[1].fd = -1;
        break;
      case -1:
        return -1;
      default:
        break;
      }
    }

    if (rfd[2].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) {
      switch (sigread(s)) {
      case 0:
        return 0;
      case -1:
        return -1;
      case 2:
        (void)hexlog_flush(s, h);
        break;
      default:
        break;
      }
    }

    if (rfd[4].revents & POLLHUP) {
      return 0;
    }
  }
}

static void setdir(state_t *s, int dir) {
  if (s->dir_initial & dir)
    s->dir_cur &= ~dir;
  else
    s->dir_cur |= dir;
}

static int sigread(state_t *s) {
  ssize_t n;
  int sig;

  n = read(s->fdsig, &sig, sizeof(sig));
  if (n != sizeof(sig))
    return -1;

  switch (sig) {
  case SIGHUP:
    s->dir_cur = s->dir_initial;
    break;
  case SIGUSR1:
    setdir(s, IN);
    break;
  case SIGUSR2:
    setdir(s, OUT);
    break;
  case SIGALRM:
    return 2;
  case SIGCHLD:
    return 0;
  default:
#ifdef RESTRICT_PROCESS_capsicum
    (void)pdkill(s->fdp, sig);
#else
    (void)kill(-s->pid, sig);
#endif
    return 0;
  }

  return 1;
}

static int hexlog_flush(state_t *s, hexlog_t h[2]) {
  if (h[0].off > 0) {
    (void)hexdump(h[0].fdhex, h[0].label, h[0].buf, h[0].off, s->raw);
    h[0].off = 0;
  }

  if (h[1].off > 0) {
    (void)hexdump(h[1].fdhex, h[1].label, h[1].buf, h[1].off, s->raw);
    h[1].off = 0;
  }

  return 0;
}

static int relay(state_t *s, hexlog_t *h) {
  ssize_t n;
  char buf[4096] = {0};

  while ((n = read(h->fdin, buf, sizeof(buf))) == -1 && errno == EINTR)
    ;

  if (n < 1)
    return n;

  if (hexlog_write(h->fdout, buf, n) == -1)
    return -1;

  switch (h->fdin) {
  case STDIN_FILENO:
    if (!(s->dir_cur & IN)) {
      h->off = 0;
      return 1;
    }
    break;
  default:
    if (!(s->dir_cur & OUT)) {
      h->off = 0;
      return 1;
    }
    break;
  }

  if (h->off + n > 15) {
    size_t len = ((h->off + n) / 16) * 16;
    size_t rem = (h->off + n) % 16;
    (void)memcpy(h->buf + h->off, buf, len);
    (void)hexdump(h->fdhex, h->label, h->buf, len, s->raw);
    if (rem > 0)
      (void)memcpy(h->buf, buf + (len - h->off), rem);
    h->off = rem;
  } else {
    (void)memcpy(h->buf + h->off, buf, n);
    h->off += n;
  }

  return 1;
}

static int hexlog_write(int fd, void *buf, size_t size) {
  ssize_t n;
  size_t off = 0;

  do {
    n = write(fd, buf, size - off);
    if (n < 0) {
      if (errno == EINTR)
        continue;

      return -1;
    }
    off += n;
  } while (off < size);

  return 0;
}

static ssize_t hexdump(FILE *stream, const char *label, const void *data,
                       size_t size, int raw) {
  unsigned char ascii[17];
  size_t i, j;

  if (raw) {
    /* TODO: handle errors, short writes: return value is currently ignored */
    return fwrite(data, 1, size, stream);
  }

  ascii[16] = '\0';
  for (i = 0; i < size; ++i) {
    (void)fprintf(stream, "%02X ", ((const unsigned char *)data)[i]);
    if (((const unsigned char *)data)[i] >= ' ' &&
        ((const unsigned char *)data)[i] <= '~') {
      ascii[i % 16] = ((const unsigned char *)data)[i];
    } else {
      ascii[i % 16] = '.';
    }
    if ((i + 1) % 8 == 0 || i + 1 == size) {
      (void)fprintf(stream, " ");
      if ((i + 1) % 16 == 0) {
        (void)fprintf(stream, "|%s|%s\n", ascii, label);
      } else if (i + 1 == size) {
        ascii[(i + 1) % 16] = '\0';
        if ((i + 1) % 16 <= 8) {
          (void)fprintf(stream, " ");
        }
        for (j = (i + 1) % 16; j < 16; ++j) {
          (void)fprintf(stream, "   ");
        }
        (void)fprintf(stream, "|%s|%s\n", ascii, label);
      }
    }
  }
  return 0;
}

static int direction(state_t *s, char *name) {
  int d;

  if (name[0] == 'r') {
    s->raw = 1;
    name++;
  }

  if (!strcmp(name, "none"))
    d = NONE;
  else if (!strcmp(name, "in"))
    d = IN;
  else if (!strcmp(name, "out"))
    d = OUT;
  else if (!strcmp(name, "inout"))
    d = IN | OUT;
  else
    return -1;

  s->dir_initial = d;
  s->dir_cur = d;

  return 0;
}

static noreturn void usage(void) {
  (void)fprintf(stderr,
                "%s %s (using %s mode process restriction)\n"
                "usage: %s <in|out|inout|none> <cmd> <...>\n",
                __progname, HEXLOG_VERSION, RESTRICT_PROCESS, __progname);
  exit(EXIT_FAILURE);
}

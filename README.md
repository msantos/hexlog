# SYNOPSIS

hexlog [r]**in**|[r]**out**|[r]**inout**|**none** *cmd* *...*

# DESCRIPTION

hexlog: hexdump stdin and/or stdout to stderr

hexlog streams a hexdump (or optionally the raw bytes) of the standard
I/O of a subprocess to standard error. Hexdumps can be enabled or disabled
by sending a signal (see *SIGNALS*).

The hexdump code originates from:

https://gist.github.com/ccbrown/9722406

# EXAMPLES

```
$ echo abc | hexlog inout cat -n
     1  abc
61 62 63 0A                                       |abc.| (0)
20 20 20 20 20 31 09 61  62 63 0A                 |     1.abc.| (1)

$ (echo test | HEXLOG_FD_STDIN=4 HEXLOG_FD_STDOUT=5 hexlog inout cat) 4>stdin 5>stdout

$ echo abc | hexlog rinout cat -n
     1  abc
abc
     1  abc

```

# Build

```
make

# selecting process restrictions
RESTRICT_PROCESS=seccomp make

#### using musl
RESTRICT_PROCESS=rlimit ./musl-make

## linux seccomp sandbox: requires kernel headers

# clone the kernel headers somewhere
export MUSL_INCLUDE=/tmp
git clone https://github.com/sabotage-linux/kernel-headers.git $MUSL_INCLUDE/kernel-headers

# then compile
MUSL_INCLUDE=/tmp ./musl-make clean all
```

# OPTIONS

None.

# ARGUMENTS

none
: do not dump stdio

in
: dump stdin

out
: dump stdout

inout
: dump stdin/stdout

Prefacing a stream with 'r' will dump the raw bytes: rnone, rin,
rout, rinout.

# ENVIRONMENT VARIABLES

HEXLOG_LABEL_STDIN=" (0)"
: Label attached to hexdump of the stdin stream.

HEXLOG_LABEL_STDOUT=" (1)"
: Label attached to hexdump of the stdout stream.

HEXLOG_FD_STDIN="2"
: File descriptor to write dump of the stdin stream.

HEXLOG_FD_STDOUT="2"
: File descriptor to write dump of the stdout stream.

HEXLOG_TIMEOUT="0"
: Dump any buffered data after HEXLOG_TIMEOUT seconds of inactivity
(0 to disable)

# SIGNALS

SIGUSR1
: enable/disable hexdump of stdin

SIGUSR2
: enable/disable hexdump of stdout

SIGHUP
: reset hexdump stdio to initial value

SIGALRM
: dump any buffered data

# ALTERNATIVES

## bash

The general problem is duplicating stdout to stderr which can be handled
using `tee(1)` and bash's process substitution:

```
tee >(cat 1>&2) | command | tee >(cat 1>&2)
```

This pipeline forks 6 processes including bash. To hexdump:

```
tee >(stdbuf -oL hexdump -C 1>&2) | command | tee >(stdbuf -oL hexdump -C 1>&2)
```

A real version would use a format string and include the direction
(stdin vs stdout).

And to wrap it in a script:

```
#!/bin/bash

set -o errexit
set -o nounset
set -o pipefail

tee >(stdbuf -oL hexdump -C 1>&2) | $@ | tee >(stdbuf -oL hexdump -C 1>&2)
```

For example:

```
./hexlog nc -l -k 9090
```

Note: hexdump doesn't exit and flush the buffer when stdin is closed. It
will wait for the next full write.

## socat

```
socat -xv - EXEC:"command arg1",pty
```

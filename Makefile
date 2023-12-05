.PHONY: all clean test

PROG=   hexlog
SRCS=   hexlog.c \
				waitfor.c \
				restrict_process_capsicum.c \
				restrict_process_null.c \
				restrict_process_pledge.c \
				restrict_process_rlimit.c \
				restrict_process_seccomp.c

UNAME_SYS := $(shell uname -s)
ifeq ($(UNAME_SYS), Linux)
    CFLAGS ?= -D_FORTIFY_SOURCE=2 -O2 -fstack-protector-strong \
              -Wformat -Werror=format-security \
              -fno-strict-aliasing
    LDFLAGS ?= -Wl,-z,relro,-z,now -Wl,-z,noexecstack
    RESTRICT_PROCESS ?= seccomp
else ifeq ($(UNAME_SYS), OpenBSD)
    CFLAGS ?= -DHAVE_STRTONUM \
              -D_FORTIFY_SOURCE=2 -O2 -fstack-protector-strong \
              -Wformat -Werror=format-security \
              -fno-strict-aliasing
    LDFLAGS ?= -Wno-missing-braces -Wl,-z,relro,-z,now -Wl,-z,noexecstack
    RESTRICT_PROCESS ?= pledge
else ifeq ($(UNAME_SYS), FreeBSD)
    CFLAGS ?= -DHAVE_STRTONUM \
              -D_FORTIFY_SOURCE=2 -O2 -fstack-protector-strong \
              -Wformat -Werror=format-security \
              -fno-strict-aliasing
    LDFLAGS ?= -Wno-missing-braces -Wl,-z,relro,-z,now -Wl,-z,noexecstack
    RESTRICT_PROCESS ?= capsicum
endif

CFLAGS += -g -Wall -Wextra -fwrapv -pedantic -pie -fPIE $(HEXLOG_CFLAGS)
LDFLAGS += $(HEXLOG_LDFLAGS)
RESTRICT_PROCESS ?= rlimit

all:
	$(CC) $(CFLAGS) \
	 	-DRESTRICT_PROCESS=\"$(RESTRICT_PROCESS)\" -DRESTRICT_PROCESS_$(RESTRICT_PROCESS) \
	 	-o $(PROG) $(SRCS) $(LDFLAGS)

clean:
	-@rm $(PROG)

test: $(PROG)
	  @PATH=.:$(PATH) bats test

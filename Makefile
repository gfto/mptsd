CC? = $(CROSS)$(TARGET)gcc
STRIP = $(CROSS)$(TARGET)strip
VERSION="v1.1"
ifndef REPRODUCIBLE_BUILD
BUILD_ID = $(shell date +%F_%R)
GIT_VER = $(shell git describe --tags --dirty --always 2>/dev/null)
endif
CFLAGS ?= -ggdb -O2
CFLAGS += -Wall -Wextra -Wshadow -Wformat-security -Wno-strict-aliasing -D_GNU_SOURCE -DBUILD_ID=\"$(BUILD_ID)\"
ifneq "$(GIT_VER)" ""
CFLAGS += -DGIT_VER=\"$(GIT_VER)\"
else
CFLAGS += -DGIT_VER=\"$(VERSION)\"
endif

ifndef WITH_JSON
$(info enabeling JSON support)
$(info if you don't want JSON support run)
$(info make WITH_JSON=0)
WITH_JSON = 1
endif

CFLAGS += -DWITH_JSON=$(WITH_JSON)

RM = /bin/rm -f
Q = @

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
ifeq ($(uname_S),Darwin)
LIBS = -lpthread -lm
else
LIBS = -lpthread -lm -lrt
endif

ifeq "$(WITH_JSON)" "1"
LIBS += -ljson-c
endif

FUNCS_DIR = libfuncs
FUNCS_LIB = $(FUNCS_DIR)/libfuncs.a

TSFUNCS_DIR = libtsfuncs
TSFUNCS_LIB = $(TSFUNCS_DIR)/libtsfuncs.a

mptsd_OBJS =  $(FUNCS_LIB) $(TSFUNCS_LIB) \
	iniparser.o inidict.o pidref.o data.o config.o \
	sleep.o network.o \
	input.o \
	output_psi.o output_mix.o output_write.o \
	web_pages.o web_server.o \
	mptsd.o

PROGS = mptsd
CLEAN_OBJS = $(PROGS) $(mptsd_OBJS) *~

all: $(PROGS)

$(FUNCS_LIB):
	$(Q)echo "  MAKE	$(FUNCS_LIB)"
	$(Q)$(MAKE) -s -C $(FUNCS_DIR)

$(TSFUNCS_LIB):
	$(Q)echo "  MAKE	$(TSFUNCS_LIB)"
	$(Q)$(MAKE) -s -C $(TSFUNCS_DIR)

mptsd: $(mptsd_OBJS)
	$(Q)echo "  LINK	mptsd"
	$(Q)$(CC) $(CFLAGS) $(mptsd_OBJS) $(LIBS) -o mptsd

%.o: %.c data.h
	$(Q)echo "  CC	mptsd		$<"
	$(Q)$(CC) $(CFLAGS)  -c $<

strip:
	$(Q)echo "  STRIP	$(PROGS)"
	$(Q)$(STRIP) $(PROGS)

clean:
	$(Q)echo "  RM	$(CLEAN_OBJS)"
	$(Q)$(RM) $(CLEAN_OBJS)

distclean: clean
	$(Q)$(MAKE) -s -C $(TSFUNCS_DIR) clean
	$(Q)$(MAKE) -s -C $(FUNCS_DIR) clean

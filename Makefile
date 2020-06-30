TARGET  = foxhunt
PREFIX  ?= /usr/local

HEADERS := $(wildcard src/*.h)
SOURCES := $(wildcard src/*.c)
CFLAGS  := -std=c18 -Wall
LDFLAGS :=
CC ?= cc

DEBUG ?= 0
ifneq ($(DEBUG),0)
    CFLAGS  := -DDEBUG -g $(CFLAGS) $(DEFINES)
else
    CFLAGS  := -DNDEBUG -O2 -flto $(CFLAGS) $(DEFINES)
endif

WLPROTODIR := wlproto
WLPROTOS   := xdg-shell
WLHEADERS  := $(addprefix $(WLPROTODIR)/,$(WLPROTOS:=-client-protocol.h))
WLSOURCES  := $(addprefix $(WLPROTODIR)/,$(WLPROTOS:=-protocol.c))
WLPROTOXML != pkg-config wayland-protocols --variable=pkgdatadir
WLSCANNER  != pkg-config wayland-scanner --variable=wayland_scanner
WLCFLAGS   != pkg-config wayland-client --cflags
WLLDFLAGS  != pkg-config wayland-client --libs

HEADERS += $(WLHEADERS)
SOURCES += $(WLSOURCES)
CFLAGS  += $(WLCFLAGS)
LDFLAGS += $(WLLDFLAGS)
OBJECTS := $(SOURCES:.c=.o)

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS)

$(OBJECTS): %.o: %.c $(HEADERS)
	$(CC) -c $< -o $@ -I$(WLPROTODIR) $(CFLAGS)

# Функция всего лишь удаляет суффикс -unstable-v с цифрой из имени файла.
unvers = $(strip $(foreach v,1 2 3 4 5 6 7 8 9,\
             $(if $(findstring -v$(v),$(1)),$(subst -unstable-v$(v),,$(1)),)))
wlproto = $(if $(findstring unstable,$(1)),unstable/$(call unvers,$(1)),stable/$(1))

$(WLHEADERS): $(WLPROTODIR)/%-client-protocol.h: | $(WLPROTODIR)
	$(WLSCANNER) client-header $(WLPROTOXML)/$(call wlproto,$*)/$*.xml $@

$(WLSOURCES): $(WLPROTODIR)/%-protocol.c: | $(WLPROTODIR)
	$(WLSCANNER) private-code  $(WLPROTOXML)/$(call wlproto,$*)/$*.xml $@

$(WLPROTODIR):
	mkdir $(WLPROTODIR)

clean:
	$(RM) $(TARGET) $(OBJECTS) -r $(WLPROTODIR)

install:
	install $(TARGET) $(PREFIX)/bin

uninstall:
	$(RM) $(PREFIX)/bin/$(TARGET)

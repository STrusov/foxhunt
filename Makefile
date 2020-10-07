# По умолчанию собирается версия для Wayland.
# Что бы использовать X11 (XCB), запускать так: make X11=1

TARGET  = foxhunt
PREFIX  ?= /usr/local

HEADERS := $(wildcard src/*.h)
SOURCES := $(wildcard src/*.c)
SHADERS := src/shader.frag src/shader.vert
MUSICS  := $(wildcard music/*.cps)
LIBS    := alsa vulkan
CFLAGS  := -std=c18 -Wall
LDFLAGS := -lm -pthread
ifdef X11
    SOURCES := $(subst $(wildcard src/wayland*.c),,$(SOURCES))
    LIBS += xcb xcb-cursor xcb-icccm
    CFLAGS += -DFH_PLATFORM_XCB
else
    SOURCES := $(subst $(wildcard src/xcb*.c),,$(SOURCES))
    LIBS += wayland-client wayland-cursor
endif
CC ?= cc
#GLCFLAGS :=
GLC ?= glslangValidator -V
#GLC ?= glslc
BIN2TXT = hexdump -v -e '"\t" 4/1 "0x%02x, " "\n"'

DEBUG ?= 0
ifneq ($(DEBUG),0)
    CFLAGS  := -DDEBUG -g $(CFLAGS) $(DEFINES)
else
    CFLAGS  := -DNDEBUG -O2 -flto $(CFLAGS) $(DEFINES)
    LDFLAGS := -flto $(LDFLAGS)
endif

WLPROTODIR := wlproto
ifndef X11
WLPROTOS   := xdg-shell
WLHEADERS  := $(addprefix $(WLPROTODIR)/,$(WLPROTOS:=-client-protocol.h))
WLSOURCES  := $(addprefix $(WLPROTODIR)/,$(WLPROTOS:=-protocol.c))
WLPROTOXML != pkg-config wayland-protocols --variable=pkgdatadir
WLSCANNER  != pkg-config wayland-scanner --variable=wayland_scanner
endif
LIBSCFLAGS   != pkg-config $(LIBS) --cflags
LIBSLDFLAGS  != pkg-config $(LIBS) --libs

ifndef X11
HEADERS += $(WLHEADERS)
SOURCES += $(WLSOURCES)
endif
CFLAGS  += $(LIBSCFLAGS)
LDFLAGS += $(LIBSLDFLAGS)
OBJECTS := $(SOURCES:.c=.o)

SPIRVS  := $(addsuffix .spv,$(SHADERS))
SPVINLS := $(addsuffix .inl,$(SPIRVS))

MUSICINLS := $(addsuffix .inl,$(MUSICS))

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(SPVOBJS) $(LDFLAGS)

$(OBJECTS): %.o: %.c $(HEADERS) $(SPVINLS) $(MUSICINLS)
	$(CC) -c $< -o $@ -I$(WLPROTODIR) $(CFLAGS)

$(SPVINLS): %.inl: %
	$(BIN2TXT) $< > $@

$(SPIRVS): %.spv: %
	$(GLC) -c $< -o $@ $(GLCFLAGS)

$(MUSICINLS): %.inl: %
	$(BIN2TXT) $< > $@

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
	$(RM) $(TARGET) $(OBJECTS) $(SPVINLS) $(SPIRVS) $(MUSICINLS) -r $(WLPROTODIR)

install:
	install $(TARGET) $(PREFIX)/bin

uninstall:
	$(RM) $(PREFIX)/bin/$(TARGET)

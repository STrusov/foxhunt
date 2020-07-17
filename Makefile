TARGET  = foxhunt
PREFIX  ?= /usr/local

HEADERS := $(wildcard src/*.h)
SOURCES := $(wildcard src/*.c)
SHADERS := src/shader.frag src/shader.vert
CFLAGS  := -std=c18 -Wall
LDFLAGS :=
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
endif

WLPROTODIR := wlproto
WLPROTOS   := xdg-shell
WLHEADERS  := $(addprefix $(WLPROTODIR)/,$(WLPROTOS:=-client-protocol.h))
WLSOURCES  := $(addprefix $(WLPROTODIR)/,$(WLPROTOS:=-protocol.c))
WLPROTOXML != pkg-config wayland-protocols --variable=pkgdatadir
WLSCANNER  != pkg-config wayland-scanner --variable=wayland_scanner
WLCFLAGS   != pkg-config vulkan wayland-client --cflags
WLLDFLAGS  != pkg-config vulkan wayland-client --libs

HEADERS += $(WLHEADERS)
SOURCES += $(WLSOURCES)
CFLAGS  += $(WLCFLAGS)
LDFLAGS += $(WLLDFLAGS)
OBJECTS := $(SOURCES:.c=.o)

SPIRVS  := $(addsuffix .spv,$(SHADERS))
SPVINLS := $(addsuffix .inl,$(SPIRVS))

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $(OBJECTS) $(SPVOBJS) $(LDFLAGS)

$(OBJECTS): %.o: %.c $(HEADERS) $(SPVINLS)
	$(CC) -c $< -o $@ -I$(WLPROTODIR) $(CFLAGS)

$(SPVINLS): %.inl: %
	$(BIN2TXT) $< > $@

$(SPIRVS): %.spv: %
	$(GLC) -c $< -o $@ $(GLCFLAGS)

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
	$(RM) $(TARGET) $(OBJECTS) $(SPVINLS) $(SPIRVS) -r $(WLPROTODIR)

install:
	install $(TARGET) $(PREFIX)/bin

uninstall:
	$(RM) $(PREFIX)/bin/$(TARGET)

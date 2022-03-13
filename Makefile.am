AUTOMAKE_OPTIONS = foreign

if !SYSTEM_LIBINIPARSER
    SUBDIRS = iniparser
endif

ACLOCAL_AMFLAGS = -I m4

M_CPPFLAGS = -DSYSTEM_LIBINIPARSER=@SYSTEM_LIBINIPARSER@

bin_PROGRAMS = spectrum
spectrum_SOURCES = spectrum.c config.c lms.c cJSON.c input/common.c input/fifo.c input/shmem.c \
					sigproc.c \
					output/framebuffer.c output/fbplot.c
spectrum_LDFLAGS = -L/usr/local/lib -Wl,-rpath /usr/local/lib 
spectrum_CPPFLAGS = -DPACKAGE=\"$(PACKAGE)\" -DVERSION=\"$(VERSION)\" \
           -D_POSIX_SOURCE -D _POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE_EXTENDED
spectrum_CFLAGS = -std=c99 -Wall -Werror -Wextra -Wno-unused-result -Wno-unknown-warning-option -Wno-maybe-uninitialized

if OSX
    spectrum_CFLAGS += -DNORT
else
    spectrum_LDFLAGS += -lrt
endif

if ALSA
    spectrum_SOURCES += input/alsa.c
endif

if PULSE
    spectrum_SOURCES += input/pulse.c
endif

if SNDIO
    spectrum_SOURCES += input/sndio.c
endif

if !SYSTEM_LIBINIPARSER
    spectrum_LDADD = -liniparser
    spectrum_SOURCES += iniparser/libiniparser.la
    spectrum_LDADD += -Liniparser/.libs
    spectrum_CPPFLAGS += -Iiniparser/src
endif

# freetype hack
spectrum_CPPFLAGS += -I/usr/include/freetype2 -I/usr/include
spectrum_LDFLAGS += -L/usr/lib -lfreetype

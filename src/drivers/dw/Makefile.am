noinst_LTLIBRARIES = libdrivers.la

libdrivers_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(ARCH_CFLAGS) \
	$(PLATFORM_INCDIR) \
	$(SOF_INCDIR) \
	$(ARCH_INCDIR)

libdrivers_la_SOURCES = dma.c

if BUILD_SUECREEK
libdrivers_la_SOURCES += gpio.c
endif

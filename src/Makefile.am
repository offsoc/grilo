#
# Makefile.am
#
# Author: Iago Toral Quiroga <itoral@igalia.com>
#
# Copyright (C) 2010-2012 Igalia S.L. All rights reserved.

# autogenerated files
grl-marshal.h: grl-marshal.list
	$(AM_V_GEN) $(GLIB_GENMARSHAL) --prefix grl_marshal	\
	--header grl-marshal.list > $@

grl-marshal.c: grl-marshal.h grl-marshal.list
	@echo "#include \"grl-marshal.h\"" > $@
	$(AM_V_GEN) $(GLIB_GENMARSHAL) --prefix grl_marshal	\
	--body grl-marshal.list >> $@

grl-type-builtins.h: grl-media-source.h grl-type-builtins.h.template
	$(AM_V_GEN) $(GLIB_MKENUMS) --template grl-type-builtins.h.template	\
	grl-media-source.h > $@

grl-type-builtins.c: grl-type-builtins.h grl-media-source.h grl-type-builtins.c.template
	$(AM_V_GEN) $(GLIB_MKENUMS) --template grl-type-builtins.c.template	\
	grl-media-source.h > $@

lib_LTLIBRARIES = lib@GRL_NAME@.la

lib@GRL_NAME@_la_CFLAGS =	\
	$(DEPS_CFLAGS)		\
	-I$(srcdir)		\
	-I$(srcdir)/data	\
	-DGRILO_COMPILATION	\
	-DG_LOG_DOMAIN=\"Grilo\"

lib@GRL_NAME@_la_LIBADD =	\
	$(DEPS_LIBS)

lib@GRL_NAME@_la_LDFLAGS =	\
	-no-undefined

lib@GRL_NAME@_la_SOURCES =					\
	grl-media-plugin.c grl-media-plugin-priv.h		\
	grl-plugin-registry.c grl-plugin-registry-priv.h	\
	grl-metadata-key.c grl-metadata-key-priv.h		\
	grl-metadata-source.c grl-metadata-source-priv.h	\
	grl-operation.c grl-operation.h				\
	grl-type-builtins.c grl-type-builtins.h			\
	grl-marshal.c grl-marshal.h				\
	grl-media-source.c grl-util.c				\
	grl-multiple.c						\
	grl-log.c grl-log-priv.h				\
	grl-sync.c						\
	grilo.c

data_c_sources =		\
	data/grl-data.c		\
	data/grl-related-keys.c	\
	data/grl-media.c	\
	data/grl-media-audio.c	\
	data/grl-media-video.c	\
        data/grl-media-image.c	\
	data/grl-media-box.c	\
	data/grl-config.c

lib@GRL_NAME@_la_SOURCES += $(data_c_sources)

lib@GRL_NAME@incdir =	\
	$(includedir)/@GRL_NAME@

lib@GRL_NAME@inc_HEADERS =	\
	grilo.h			\
	grl-error.h		\
	grl-media-plugin.h	\
	grl-plugin-registry.h	\
	grl-metadata-key.h	\
	grl-metadata-source.h	\
	grl-media-source.h	\
	grl-log.h 		\
	grl-multiple.h		\
	grl-util.h		\
	grl-definitions.h	\
	grl-operation.h

data_h_headers =		\
	data/grl-data.h		\
	data/grl-related-keys.h	\
	data/grl-media.h	\
	data/grl-media-box.h	\
	data/grl-media-audio.h	\
	data/grl-media-video.h	\
	data/grl-media-image.h	\
	data/grl-config.h

lib@GRL_NAME@inc_HEADERS += $(data_h_headers)

noinst_HEADERS =			\
	grl-plugin-registry-priv.h	\
	grl-media-plugin-priv.h		\
	grl-metadata-source-priv.h	\
	grl-metadata-key-priv.h		\
	grl-operation-priv.h		\
	grl-sync-priv.h			\
	grl-type-builtins.h		\
	grl-marshal.h

EXTRA_DIST =				\
	grl-marshal.list		\
	grl-type-builtins.h.template	\
	grl-type-builtins.c.template

MAINTAINERCLEANFILES =	\
	*.in		\
	*.gir		\
	*~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)

CLEANFILES =

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir) --warn-all
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir) --includedir=$(srcdir)/data

# introspection support
if HAVE_INTROSPECTION
introspection_sources = \
	$(lib@GRL_NAME@inc_HEADERS)	\
	$(lib@GRL_NAME@_la_SOURCES)

Grl-@GRL_MAJORMINOR@.gir: lib@GRL_NAME@.la
Grl_@GRL_MAJORMINOR_NORM@_gir_INCLUDES = GObject-2.0 GModule-2.0
Grl_@GRL_MAJORMINOR_NORM@_gir_CFLAGS = $(lib@GRL_NAME@_la_CFLAGS)
Grl_@GRL_MAJORMINOR_NORM@_gir_LIBS = lib@GRL_NAME@.la
Grl_@GRL_MAJORMINOR_NORM@_gir_FILES = $(addprefix $(srcdir)/,$(introspection_sources))
INTROSPECTION_GIRS += Grl-@GRL_MAJORMINOR@.gir

girdir = @INTROSPECTION_GIRDIR@
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = @INTROSPECTION_TYPELIBDIR@
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(dist_gir_DATA) $(typelib_DATA)
endif

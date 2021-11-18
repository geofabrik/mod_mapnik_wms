ACLOCAL_AMFLAGS=-I ./acinclude.d/

noinst_LTLIBRARIES = libmodmapnikwms.la
noinst_DATA = mod_mapnik_wms.la

INCLUDES = ${APACHE_CFLAGS} ${FREETYPE_CFLAGS} ${MAPNIK_CFLAGS}
AM_LDFLAGS = ${FREETYPE_LIBS} ${MAPNIK_LIBS}
AM_CFLAGS=-DUSE_KEY_DATABASE -DACCEPT_USE_OF_DEPRECATED_PROJ_API_H
AM_CPPFLAGS=-DUSE_KEY_DATABASE -DACCEPT_USE_OF_DEPRECATED_PROJ_API_H

# wms.cpp logbuffer.cpp
libmodmapnikwms_la_SOURCES = mod_mapnik_wms.c wms.cpp logbuffer.cpp apachebuffer.cpp

install-exec-local:
	${APXS} -i -S LIBEXECDIR=${DESTDIR}/usr/lib/apache2/modules -n 'mapnikwms' mod_mapnik_wms.la

mod_mapnik_wms.la: libmodmapnikwms.la
	${APXS} -c -o $@ $< ${INCLUDES} ${AM_LDFLAGS}


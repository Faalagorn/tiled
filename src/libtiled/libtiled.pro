include(../../tiled.pri)
include(../qtlockedfile/qtlockedfile.pri)
include(../zlib/zlib.pri)

TEMPLATE = lib
TARGET = tiled
isEmpty(INSTALL_ONLY_BUILD) {
    target.path = $${LIBDIR}
    INSTALLS += target
}
macx {
    DESTDIR = ../../bin/TileZed.app/Contents/Frameworks
    QMAKE_LFLAGS_SONAME = -Wl,-install_name,@executable_path/../Frameworks/
} else {
    DESTDIR = ../../lib
}
DLLDESTDIR = ../..

DEFINES += QT_NO_CAST_FROM_ASCII \
    QT_NO_CAST_TO_ASCII
DEFINES += TILED_LIBRARY
DEFINES += ZOMBOID
contains(QT_CONFIG, reduce_exports): CONFIG += hide_symbols
#OBJECTS_DIR = .obj
SOURCES += compression.cpp \
    imagelayer.cpp \
    isometricrenderer.cpp \
    layer.cpp \
    map.cpp \
    maplevel.cpp \
    mapobject.cpp \
    mapreader.cpp \
    maprenderer.cpp \
    mapwriter.cpp \
    objectgroup.cpp \
    orthogonalrenderer.cpp \
    properties.cpp \
    staggeredrenderer.cpp \
    tilelayer.cpp \
    tileset.cpp \
    gidmapper.cpp \
    zlevelrenderer.cpp \
    ztilelayergroup.cpp \
    tile.cpp
HEADERS += compression.h \
    imagelayer.h \
    isometricrenderer.h \
    layer.h \
    map.h \
    maplevel.h \
    mapobject.h \
    mapreader.h \
    maprenderer.h \
    maprotation.h \
    mapwriter.h \
    object.h \
    objectgroup.h \
    orthogonalrenderer.h \
    properties.h \
    staggeredrenderer.h \
    tile.h \
    tiled_global.h \
    tilelayer.h \
    tileset.h \
    gidmapper.h \
    zlevelrenderer.h \
    ztilelayergroup.h
macx {
    contains(QT_CONFIG, ppc):CONFIG += x86 \
        ppc
}

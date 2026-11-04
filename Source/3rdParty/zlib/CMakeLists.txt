cmake_minimum_required(VERSION 3.16)
project(vendored_zlib C)

# Vendored zlib 1.3.1 (zlib license). Built here instead of using system
# zlib: minizip/ioapi.h includes this exact zlib.h via a relative path
# ("../../zlib.h"), and mixing that with Fedora's system zlib.h (zlib-ng,
# ZLIB_VERSION "1.3.1.zlib-ng") in the same translation unit produces
# conflicting macro redefinitions (gzFile/gzgetc and friends) that fail to
# compile. Matches zlib.vcxproj's own source list for the Windows build.
add_library(vendored_zlib STATIC
    adler32.c
    compress.c
    crc32.c
    deflate.c
    gzclose.c
    gzlib.c
    gzread.c
    gzwrite.c
    infback.c
    inffast.c
    inflate.c
    inftrees.c
    trees.c
    uncompr.c
    zutil.c
)
add_library(zlibstatic ALIAS vendored_zlib)
target_include_directories(vendored_zlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

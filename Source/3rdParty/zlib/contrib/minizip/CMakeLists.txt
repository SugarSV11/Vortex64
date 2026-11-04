cmake_minimum_required(VERSION 3.16)
project(minizip C)

# Vendored minizip (zlib contrib, zlib license). On Windows the project
# additionally compiles iowin32.c (a Win32 HANDLE-based I/O layer selected
# explicitly by N64Rom.cpp via fill_win32_filefunc64W); on Linux only the
# portable fopen64-based I/O layer in ioapi.c is needed, selected instead via
# fill_fopen64_filefunc (see N64System/N64Rom.cpp's #else branch).
#
# Links the vendored zlib (see ../../CMakeLists.txt), not system zlib:
# ioapi.h includes this repo's own zlib.h by relative path, and that clashes
# with Fedora's system zlib.h (zlib-ng) if both end up in one translation
# unit.
add_library(minizip STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ioapi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unzip.c
    ${CMAKE_CURRENT_SOURCE_DIR}/zip.c
)
target_include_directories(minizip PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(minizip PRIVATE _FILE_OFFSET_BITS=64 _LARGEFILE64_SOURCE=1)
target_link_libraries(minizip PUBLIC vendored_zlib)

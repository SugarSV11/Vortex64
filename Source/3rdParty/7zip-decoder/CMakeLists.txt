cmake_minimum_required(VERSION 3.16)
project(sevenzip_decoder C)

# Vendored 7-Zip C SDK sources (public domain, see 7zip.vcxproj's original
# location at Source/3rdParty/7zip). Named "7zip-decoder" here to avoid a
# target-name clash with the Windows-only PROJECT NAME used by the original
# 7zip.vcxproj. Only the archive *reading* path is exercised by
# Vortex64-core/3rdParty/7zip.cpp (SzArEx_Open/Init/Extract), but the whole
# SDK is small and portable, so it is built as one static lib rather than
# hand-picking translation units.
file(GLOB SEVENZIP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../7zip/*.c)
# LzmaLib.c / Lzma86Enc.c / LzmaEnc.c / Lzma2Enc.c / MtCoder.c / XzEnc.c pull
# in the encoder-only side of the SDK, which isn't needed for reading .7z
# ROMs and drags in Threads.h/LzFindMt.c's Win32 HANDLE/CRITICAL_SECTION
# based threading (no POSIX branch in this vendored copy); drop them so the
# static lib matches what Vortex64-core actually calls.
list(FILTER SEVENZIP_SOURCES EXCLUDE REGEX "(LzmaLib|Lzma86Enc|LzmaEnc|Lzma2Enc|MtCoder|XzEnc|LzFindMt|Threads)\\.c$")

add_library(sevenzip_decoder STATIC ${SEVENZIP_SOURCES})
target_include_directories(sevenzip_decoder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../7zip)

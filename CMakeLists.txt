# CMakeLists.txt - webp.datatype for AmigaOS 3.x / m68k
#
# Targets:
#   webpdec          libwebp decoder (+ demux), static, compiled from libwebp-main
#   webp.datatype    the picture.datatype subclass, an exec class library
#   descriptor       DEVS:DataTypes/WebP descriptor (host python script)
#   webpdtinfo       small Amiga CLI test tool (normal libnix executable)
#   dist             assembles the installable package in <build>/dist
#
# Configure (68020 build, like the EmojiGear buildAmiga dir):
#   mkdir buildAmiga && cd buildAmiga
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=/home/krabob/Code/Amiga/amigacommonlibs/cmake/Modules/Platform/m68k-amigaos.cmake -DCMAKE_BUILD_TYPE=Release
#   cmake --build .

cmake_minimum_required(VERSION 3.12)
enable_language(VASM)
project(WebpDt C)

set(CMAKE_C_STANDARD 11)

set(WEBPDT_VERSION  45)
set(WEBPDT_REVISION 1)
string(TIMESTAMP WEBPDT_DAY   "%d")
string(TIMESTAMP WEBPDT_MONTH "%m")
string(TIMESTAMP WEBPDT_YEAR  "%Y")
math(EXPR WEBPDT_DAY   "${WEBPDT_DAY}")    # Amiga dates have no leading zeros
math(EXPR WEBPDT_MONTH "${WEBPDT_MONTH}")
set(WEBPDT_DATE "${WEBPDT_DAY}.${WEBPDT_MONTH}.${WEBPDT_YEAR}")

option(WEBPDT_SAVE_TEMPS "keep .s assembler output (-save-temps)" OFF)

set(AMIGA_CPU_FLAGS -mcpu=68020 -msoft-float -O2 -fomit-frame-pointer)
if(WEBPDT_SAVE_TEMPS)
    list(APPEND AMIGA_CPU_FLAGS -save-temps)
endif()

# vasm rule from the platform file, plus our build dir for "libversion.i"
set(CMAKE_VASM_COMPILE_OBJECT
    "<CMAKE_VASM_COMPILER> -devpac -Fhunk -m68020 -I${M68KAMIGA_ROOT_PATH}/m68k-amigaos/ndk-include -I${CMAKE_CURRENT_BINARY_DIR} <SOURCE> -o <OBJECT>")

set(LIBGCC_M020 ${M68KAMIGA_ROOT_PATH}/lib/gcc/m68k-amigaos/6.5.0b/libm020/libgcc.a)

# -------------------------------------------------------------------------
# libwebp decoder subset. No SIMD exists for m68k, and libwebp's cpu.h only
# enables SSE/NEON/MIPS paths on those architectures, so the *_sse2.c etc.
# files are simply not listed. No WEBP_USE_THREAD (no pthreads).
# -------------------------------------------------------------------------
# libwebp sources are not part of this repository. Get them with:
#   git clone https://chromium.googlesource.com/webm/libwebp libwebp-main
# (or the GitHub mirror https://github.com/webmproject/libwebp), or point
# WEBP_ROOT to an existing copy: cmake .. -DWEBP_ROOT=/path/to/libwebp
set(WEBP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/libwebp-main CACHE PATH "libwebp source tree")
if(NOT EXISTS ${WEBP_ROOT}/src/webp/decode.h)
    message(FATAL_ERROR "libwebp not found in WEBP_ROOT=${WEBP_ROOT}\n"
        "Clone it: git clone https://chromium.googlesource.com/webm/libwebp ${CMAKE_CURRENT_SOURCE_DIR}/libwebp-main\n"
        "or configure with -DWEBP_ROOT=/path/to/libwebp")
endif()

# dsp/yuv.c unconditionally #defines USE_GAMMA_COMPRESSION, whose tables
# (encoder-side RGB->YUV only) need pow() and double soft-float. We compile
# a copy with that one line removed, so libwebp-main stays pristine.
set(WEBP_YUV_SRC ${WEBP_ROOT}/src/dsp/yuv.c)
file(READ ${WEBP_YUV_SRC} _yuv_c)
string(FIND "${_yuv_c}" "\n#define USE_GAMMA_COMPRESSION\n" _yuv_pos)
if(_yuv_pos EQUAL -1)
    message(FATAL_ERROR "libwebp dsp/yuv.c changed: '#define USE_GAMMA_COMPRESSION' not found, update CMakeLists.txt")
endif()
string(REPLACE "\n#define USE_GAMMA_COMPRESSION\n"
               "\n// webp.datatype: USE_GAMMA_COMPRESSION disabled (see CMakeLists.txt)\n"
               _yuv_c "${_yuv_c}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/webp_amiga/yuv_nogamma.c.tmp "${_yuv_c}")
configure_file(${CMAKE_CURRENT_BINARY_DIR}/webp_amiga/yuv_nogamma.c.tmp
               ${CMAKE_CURRENT_BINARY_DIR}/webp_amiga/yuv_nogamma.c COPYONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${WEBP_YUV_SRC})

set(WEBP_DEC_SRCS
    ${WEBP_ROOT}/src/dec/alpha_dec.c
    ${WEBP_ROOT}/src/dec/buffer_dec.c
    ${WEBP_ROOT}/src/dec/frame_dec.c
    ${WEBP_ROOT}/src/dec/idec_dec.c
    ${WEBP_ROOT}/src/dec/io_dec.c
    ${WEBP_ROOT}/src/dec/quant_dec.c
    ${WEBP_ROOT}/src/dec/tree_dec.c
    ${WEBP_ROOT}/src/dec/vp8_dec.c
    ${WEBP_ROOT}/src/dec/vp8l_dec.c
    ${WEBP_ROOT}/src/dec/webp_dec.c

    ${WEBP_ROOT}/src/demux/demux.c

    ${WEBP_ROOT}/src/dsp/alpha_processing.c
    ${WEBP_ROOT}/src/dsp/cpu.c
    ${WEBP_ROOT}/src/dsp/dec.c
    ${WEBP_ROOT}/src/dsp/dec_clip_tables.c
    ${WEBP_ROOT}/src/dsp/filters.c
    ${WEBP_ROOT}/src/dsp/lossless.c
    ${WEBP_ROOT}/src/dsp/rescaler.c
    ${WEBP_ROOT}/src/dsp/upsampling.c
    ${CMAKE_CURRENT_BINARY_DIR}/webp_amiga/yuv_nogamma.c

    ${WEBP_ROOT}/src/utils/bit_reader_utils.c
    ${WEBP_ROOT}/src/utils/color_cache_utils.c
    ${WEBP_ROOT}/src/utils/filters_utils.c
    ${WEBP_ROOT}/src/utils/huffman_utils.c
    ${WEBP_ROOT}/src/utils/palette.c
    ${WEBP_ROOT}/src/utils/quant_levels_dec_utils.c
    ${WEBP_ROOT}/src/utils/random_utils.c
    ${WEBP_ROOT}/src/utils/rescaler_utils.c
    ${WEBP_ROOT}/src/utils/thread_utils.c
    ${WEBP_ROOT}/src/utils/utils.c
)

add_library(webpdec STATIC ${WEBP_DEC_SRCS})
target_include_directories(webpdec
    PUBLIC ${WEBP_ROOT} ${WEBP_ROOT}/src)
target_compile_definitions(webpdec PRIVATE NDEBUG)
# m68k is big endian. libwebp's cpu.h derives this from __BYTE_ORDER__, but
# some files test WORDS_BIGENDIAN before including it (libwebp's own cmake
# defines it too). PUBLIC: our sources include libwebp's dsp headers.
target_compile_definitions(webpdec PUBLIC WORDS_BIGENDIAN)
if(AMIGA)
    target_compile_options(webpdec PRIVATE ${AMIGA_CPU_FLAGS})
endif()

# -------------------------------------------------------------------------
# webp.datatype - class library, built as an "executable" with our own
# startup (libinit.s first, see src/amiga_library.ld) and our own CRT.
# -------------------------------------------------------------------------
configure_file(src/libversion.h.in ${CMAKE_CURRENT_BINARY_DIR}/libversion.h @ONLY)
configure_file(src/libversion.i.in ${CMAKE_CURRENT_BINARY_DIR}/libversion.i @ONLY)

set(DTNAME "webp.datatype")
add_executable(${DTNAME}
    src/libinit.s
    src/libcinit.c
    src/webpclass.c
    src/lib_amiga_crt.c
    src/softfloat_min.c
)
target_include_directories(${DTNAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_compile_options(${DTNAME} PRIVATE ${AMIGA_CPU_FLAGS} -Wall)
target_link_options(${DTNAME} PRIVATE
    -nostdlib -ffreestanding -nostartfiles -Wl,-e,__start
    -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/src/amiga_library.ld)

# see lib_amiga_crt.c: the CRT must not be turned into calls to itself
set_source_files_properties(src/lib_amiga_crt.c src/softfloat_min.c
    PROPERTIES COMPILE_FLAGS "-fno-builtin")

# libgcc m020 variant (64-bit mul/div helpers) after webpdec so the linker
# resolves libwebp's references. No libnix at all: its soft-float goes
# through mathieee*.library bases that only libnix's startup opens; the
# few float helpers libwebp needs are in src/softfloat_min.c.
# (-Wl,--gc-sections is NOT usable: the hunk ld doesn't follow references
#  out of the vasm object and strips the whole library.)
target_link_libraries(${DTNAME} PRIVATE webpdec ${LIBGCC_M020})
target_link_options(${DTNAME} PRIVATE -Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/webp.datatype.map)

# -------------------------------------------------------------------------
# DEVS:DataTypes/WebP descriptor
# -------------------------------------------------------------------------
find_program(PYTHON3_EXE NAMES python3 python REQUIRED)
set(DESCRIPTOR ${CMAKE_CURRENT_BINARY_DIR}/WebP)
add_custom_command(
    OUTPUT ${DESCRIPTOR}
    COMMAND ${PYTHON3_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/mkdtdesc.py
            -o ${DESCRIPTOR}
            --name WebP
            --basename webp
            --version "$VER: WebP ${WEBPDT_VERSION}.${WEBPDT_REVISION} (${WEBPDT_DATE})"
            --pattern "#?"
            --mask "'R' 'I' 'F' 'F' ANY ANY ANY ANY 'W' 'E' 'B' 'P'"
            --group pict
            --id webp
            --flags "DTF_BINARY|DTF_CASE"
            --priority 0
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/mkdtdesc.py
    COMMENT "Generating datatype descriptor WebP"
    VERBATIM)
add_custom_target(descriptor ALL DEPENDS ${DESCRIPTOR})

# -------------------------------------------------------------------------
# Test tool: webpdtinfo <file> - loads through datatypes.library and prints
# what picture.datatype got. Ordinary libnix executable.
# -------------------------------------------------------------------------
add_executable(webpdtinfo tests/webpdtinfo.c)
target_compile_options(webpdtinfo PRIVATE ${AMIGA_CPU_FLAGS} -Wall)

# -------------------------------------------------------------------------
# dist: package directory ready for lha, in <build>/dist/WebPDT
# -------------------------------------------------------------------------
add_custom_target(dist
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/make_package.sh
            ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/dist/WebPDT ${WEBP_ROOT}
    DEPENDS ${DTNAME} descriptor webpdtinfo
    COMMENT "Assembling package in dist/WebPDT"
    VERBATIM)

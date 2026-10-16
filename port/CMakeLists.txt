cmake_minimum_required(VERSION 3.25)
project(parappa_port C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The port must be built 32-bit (use -DCMAKE_TOOLCHAIN_FILE=cmake/i686-mingw.cmake)")
endif()

find_package(SDL3 REQUIRED CONFIG)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(DECOMP_DIR "${CMAKE_SOURCE_DIR}/../analysis/decomp" CACHE PATH "Ghidra export (scripts/export_all.ps1)")
set(GEN_DIR "${CMAKE_BINARY_DIR}/gen")
set(GEN_SOURCES ${GEN_DIR}/game_main.c ${GEN_DIR}/dispatch.c)
foreach(n 0 1 2 3 5 6 7 8 9)
    list(APPEND GEN_SOURCES ${GEN_DIR}/ov${n}.c)
endforeach()
file(GLOB FIXED_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/game/fixed/*.c")
file(GLOB_RECURSE DECOMP_INPUTS CONFIGURE_DEPENDS "${DECOMP_DIR}/*/decomp.c" "${DECOMP_DIR}/*/*.tsv")

add_custom_command(
    OUTPUT ${GEN_SOURCES} ${GEN_DIR}/game.h
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/gen_decomp.py ${DECOMP_DIR} ${CMAKE_SOURCE_DIR}/src/game/fixed ${GEN_DIR}
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/gen_decomp.py ${CMAKE_SOURCE_DIR}/src/game/patches.json ${DECOMP_INPUTS} ${FIXED_SOURCES}
    COMMENT "Converting Ghidra decompilation to C")

# Decompiled game code: C, permissive, and with MIPS-like semantics (wrapping ints, no strict aliasing).
add_library(game STATIC ${GEN_SOURCES})
target_include_directories(game PUBLIC include ${GEN_DIR} src/game)
target_compile_options(game PRIVATE
    -fwrapv -fno-strict-aliasing -fno-delete-null-pointer-checks -fno-omit-frame-pointer
    -Wno-int-conversion -Wno-pointer-sign -Wno-incompatible-pointer-types -Wno-int-to-pointer-cast
    -Wno-pointer-to-int-cast -Wno-deprecated-non-prototype
    -Wno-parentheses-equality -Wno-dangling-else -Wno-unused-value -Wno-constant-conversion
    -Wno-tautological-constant-out-of-range-compare -Wno-shift-count-overflow -Wno-main-return-type
    -Wno-unknown-pragmas -Wno-incompatible-library-redeclaration -Wno-format)

file(GLOB_RECURSE PORT_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
# WIN32: a windowed program (no console); the log goes to %APPDATA%\PaRappaPort\parappa.log.
# The window / file icon is optional: put a parappa.ico in res/ (not in the repository: it would be
# game art).
if(EXISTS "${CMAKE_SOURCE_DIR}/res/parappa.ico")
    set(PORT_RESOURCES res/parappa.rc)
endif()
add_executable(parappa WIN32 ${PORT_SOURCES} ${PORT_RESOURCES})
target_include_directories(parappa PRIVATE include src ${GEN_DIR})
target_link_libraries(parappa PRIVATE game SDL3::SDL3 opengl32)
target_compile_options(parappa PRIVATE -Wall -Wno-unused-function -fno-strict-aliasing -fno-omit-frame-pointer)
target_link_options(parappa PRIVATE -Wl,--large-address-aware -static-libgcc -static-libstdc++)


# SDL3.dll and the saves-folder shortcut next to the exe.
add_custom_command(TARGET parappa POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3-shared>
        "${CMAKE_SOURCE_DIR}/res/Open Saves Folder.bat" $<TARGET_FILE_DIR:parappa>)

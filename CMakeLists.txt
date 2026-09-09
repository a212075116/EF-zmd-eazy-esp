cmake_minimum_required(VERSION 3.20)
project(endfield_esp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# game-side feed DLL (injected into the game; F4 toggles ESP wire)
if (WIN32)
    add_subdirectory(feed)
endif()


set(FEED_GEN_DIR ${CMAKE_BINARY_DIR}/gen)

add_executable(endfield_esp WIN32 src/main.cpp src/mapper/injector.cxx)
target_include_directories(endfield_esp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${FEED_GEN_DIR})

if (WIN32)
    # v17 all-in-one: the overlay embeds the freshly built feed DLL (injected at startup)
    add_custom_command(OUTPUT ${FEED_GEN_DIR}/feed_dll.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${FEED_GEN_DIR}
        COMMAND ${CMAKE_COMMAND} -DDLL=$<TARGET_FILE:endfield_feed> -DH=${FEED_GEN_DIR}/feed_dll.h
                -P ${CMAKE_CURRENT_SOURCE_DIR}/feed/feed2h.cmake
        DEPENDS endfield_feed ${CMAKE_CURRENT_SOURCE_DIR}/feed/feed2h.cmake
        COMMENT "converting endfield_feed.dll -> feed_dll.h")
    add_custom_target(feed_embed DEPENDS ${FEED_GEN_DIR}/feed_dll.h)
    add_dependencies(endfield_esp feed_embed)
    set_source_files_properties(${FEED_GEN_DIR}/feed_dll.h PROPERTIES GENERATED TRUE)
    target_sources(endfield_esp PRIVATE ${FEED_GEN_DIR}/feed_dll.h)
    add_dependencies(endfield_esp endfield_feed)
endif()

add_executable(probe src/probe_main.cpp)
target_include_directories(probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

if (MSVC)
    target_compile_options(endfield_esp PRIVATE /W3 /utf-8 /O2)
    target_compile_options(probe PRIVATE /W3 /utf-8 /O2)
endif()

if (WIN32)
    target_link_libraries(endfield_esp PRIVATE user32 gdi32 comctl32 comdlg32 psapi shlwapi shell32 advapi32)
    target_link_libraries(probe PRIVATE user32 psapi)
endif()

if (MINGW)
    target_link_options(endfield_esp PRIVATE -static -mwindows)
    target_link_options(probe PRIVATE -static)
endif()

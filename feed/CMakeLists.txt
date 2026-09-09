# game-side ESP feed DLL (IL2CPP bootstrap + esp_feed; inject this)
if (MSVC)
    # esp_feed uses raw SEH (__try/__except) around GC-unsafe walks.
    # /EHsc forbids mixing (C2712). Match dumper's proven recipe: NO /EH flag at all.
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "/EHs" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

add_library(endfield_feed SHARED
    src/main.cxx
    src/utils.cxx
    src/il2cpp_api.cxx
)
target_include_directories(endfield_feed PRIVATE include)
set_target_properties(endfield_feed PROPERTIES OUTPUT_NAME "endfield_feed"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release")
if (MSVC)
    target_compile_options(endfield_feed PRIVATE /W3 /utf-8 /O2)
endif()

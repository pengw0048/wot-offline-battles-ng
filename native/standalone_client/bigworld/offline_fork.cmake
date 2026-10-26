# The source input is downloaded at the fixed release commit by the builder.
# Only our binding copy changes; the fork's numerical algorithms stay intact.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(WOT_OFFLINE_FORK_GENERATED "${CMAKE_BINARY_DIR}/generated/offline-fork")
execute_process(COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/prepare_offline_fork.py"
    "${WOT_OFFLINE_FORK_ROOT}" "${WOT_OFFLINE_FORK_GENERATED}"
    RESULT_VARIABLE WOT_OFFLINE_FORK_PREPARE_RESULT)
if(NOT WOT_OFFLINE_FORK_PREPARE_RESULT EQUAL 0)
    message(FATAL_ERROR "The pinned gameplay numerical source could not be prepared")
endif()
include("${WOT_OFFLINE_FORK_GENERATED}/offline_fork_sources.cmake")
add_library(WotOfflineMath STATIC ${WOT_OFFLINE_MATH_SOURCES})
target_compile_definitions(WotOfflineMath PRIVATE WOT_PYTHON_C_API Py_BUILD_CORE NOMINMAX)
target_include_directories(WotOfflineMath PRIVATE
    "${BW_SOURCE_DIR}/third_party/python/Include"
    "${BW_SOURCE_DIR}/third_party/python/PC")
target_compile_options(WotOfflineMath PRIVATE /std:c++17 /arch:SSE2 /fp:strict
    /W3 /WX /wd4996 /wd5033)
set_target_properties(WotOfflineMath PROPERTIES CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES DISABLE_PRECOMPILE_HEADERS YES)
target_link_libraries(bwclient WotOfflineMath)
target_include_directories(bwclient PRIVATE "${WOT_OFFLINE_FORK_GENERATED}")

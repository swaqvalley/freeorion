find_program(CLANG-FORMAT_EXECUTABLE
    NAMES clang-format
    DOC "A tool for C/C++ code formatting"
)

if(CLANG-FORMAT_EXECUTABLE)
    execute_process(COMMAND ${CLANG-FORMAT_EXECUTABLE} "-version" OUTPUT_VARIABLE CLANG-FORMAT_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX REPLACE "clang-format version ([0-9]+)\\.([0-9]+)\\.([0-9]+).*" "\\1.\\2.\\3" CLANG-FORMAT_VERSION ${CLANG-FORMAT_VERSION})
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(clang-format REQUIRED_VARS CLANG-FORMAT_EXECUTABLE VERSION_VAR CLANG-FORMAT_VERSION)

mark_as_advanced(
    CLANG-FORMAT_EXECUTABLE
)

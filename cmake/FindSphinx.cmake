#.rst
# FindSphinx
# ----------
#
# Find the Sphinx documentation generator.
#
# Result Variables
# ^^^^^^^^^^^^^^^^
#
# This defines the following variables:
#
# ::
#
#   SPHINX_BUILD_EXECUTABLE     - The path to the sphinx-build command.
#   SPHINX_APIDOC_EXECUTABLE    - The path to the sphinx-apidoc command.
#   SPHINX_FOUND                - Was Sphinx found or not?
#   SPHINX_VERSION              - The version reported by sphinx-build --version


find_program(SPHINX_BUILD_EXECUTABLE
    NAMES sphinx-build-2 sphinx-build
    DOC "Sphinx documentation generator tool"
)

find_program(SPHINX_APIDOC_EXECUTABLE
    NAMES sphinx-apidoc-2 sphinx-apidoc
    DOC "Sphinx API doc generator tool"
)

if(SPHINX_BUILD_EXECUTABLE)
    execute_process(
        COMMAND
            "${SPHINX_BUILD_EXECUTABLE}" "--version"
        OUTPUT_VARIABLE
            SPHINX_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(REGEX REPLACE
        "Sphinx \\(sphinx-build\\) ([0-9]+).([0-9]+).([0-9]+)"
        "\\1.\\2.\\3"
        SPHINX_VERSION
        "${SPHINX_VERSION}"
    )
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Sphinx
    REQUIRED_VARS
        SPHINX_BUILD_EXECUTABLE
        SPHINX_APIDOC_EXECUTABLE
    VERSION_VAR
        SPHINX_VERSION
)

mark_as_advanced(
    SPHINX_BUILD_EXECUTABLE
    SPHINX_APIDOC_EXECUTABLE
)

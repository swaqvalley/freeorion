find_package(Doxygen)
find_package(Sphinx 1.3)

if(DOXYGEN_FOUND)
    set(FreeOrion_CPP_APIDOC_OUTDIR ${CMAKE_CURRENT_BINARY_DIR}/cpp-apidoc)

    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
        ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        @ONLY
    )

    if(NOT TARGET doc)
        add_custom_target(doc)
    endif()

    add_custom_target(cpp-apidoc
        COMMAND
            ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tag_parser.py --template ${CMAKE_CURRENT_SOURCE_DIR}/all_tags.dox.in --match ${FreeOrion_SOURCE_DIR}/default -f "*.py" "*.focs.txt" "*.macros" -o ${FreeOrion_CPP_APIDOC_OUTDIR}/all_tags.dox --link_source ${FreeOrion_SOURCE_DIR} "https://github.com/freeorion/freeorion/blob/master/" "#L"
        COMMAND
            ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${FreeOrion_SOURCE_DIR}
        COMMENT "Generating FreeOrion API documentation with Doxygen" VERBATIM
    )

    add_dependencies(doc cpp-apidoc)
endif()

if(SPHINX_FOUND)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/sphinx.conf.in"
        "${CMAKE_CURRENT_BINARY_DIR}/sphinx/conf.py"
        @ONLY
    )

    add_custom_target(python-apidoc
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/index.rst" "${CMAKE_CURRENT_BINARY_DIR}/sphinx/index.rst"
        COMMAND
            "${CMAKE_COMMAND}" -E  make_directory "${CMAKE_CURRENT_BINARY_DIR}/sphinx/_static"
        COMMAND
            "${SPHINX_APIDOC_EXECUTABLE}" --separate --module-first --force "--doc-project=${PROJECT_NAME}" "--output-dir=${CMAKE_CURRENT_BINARY_DIR}/sphinx" "${CMAKE_SOURCE_DIR}/default/python"
        COMMAND
            "breathe-apidoc" --force "--project=${PROJECT_NAME}" "--output-dir=${CMAKE_CURRENT_BINARY_DIR}/sphinx" "${CMAKE_CURRENT_BINARY_DIR}/cpp-apidoc/xml"
        COMMAND
            "${SPHINX_BUILD_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/sphinx" "${CMAKE_CURRENT_BINARY_DIR}/python-doc"
        COMMENT "Generating FreeOrion API documentation with Sphinx" VERBATIM
    )
    add_dependencies(doc python-apidoc)
endif()

if(SPHINX_FOUND AND DOXYGEN_FOUND)
    add_dependencies(python-apidoc cpp-apidoc)
endif()

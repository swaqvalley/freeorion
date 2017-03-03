find_package(CPPCheck)
find_package(PyCodestyle)
find_package(ClangFormat)

if(CPPCHECK_FOUND)
    add_custom_target(check-cpp
        ${CPPCHECK_EXECUTABLE}
            --quiet
            --template=gcc
            --std=c++11
            ${PROJECT_SOURCE_DIR}
        COMMENT "Check C++ for problematic code"
    )
endif()

if(CLANG-FORMAT_FOUND)
    add_custom_target(check-cpp-format
        ${CMAKE_CURRENT_SOURCE_DIR}/clang-format-report.py
            --config=${CMAKE_CURRENT_SOURCE_DIR}/clang-format
            --clang-format-executable=${CLANG-FORMAT_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/AI/*.h
            ${PROJECT_SOURCE_DIR}/AI/*.cpp
            ${PROJECT_SOURCE_DIR}/client/*.h
            ${PROJECT_SOURCE_DIR}/client/*.cpp
            ${PROJECT_SOURCE_DIR}/client/AI/*.h
            ${PROJECT_SOURCE_DIR}/client/AI/*.cpp
            ${PROJECT_SOURCE_DIR}/client/human/*.h
            ${PROJECT_SOURCE_DIR}/client/human/*.cpp
            ${PROJECT_SOURCE_DIR}/combat/*.h
            ${PROJECT_SOURCE_DIR}/combat/*.cpp
            ${PROJECT_SOURCE_DIR}/Empire/*.h
            ${PROJECT_SOURCE_DIR}/Empire/*.cpp
            ${PROJECT_SOURCE_DIR}/network/*.h
            ${PROJECT_SOURCE_DIR}/network/*.cpp
            ${PROJECT_SOURCE_DIR}/parse/*.h
            ${PROJECT_SOURCE_DIR}/parse/*.cpp
            ${PROJECT_SOURCE_DIR}/python/*.h
            ${PROJECT_SOURCE_DIR}/python/*.cpp
            ${PROJECT_SOURCE_DIR}/python/AI/*.h
            ${PROJECT_SOURCE_DIR}/python/AI/*.cpp
            ${PROJECT_SOURCE_DIR}/python/server/*.h
            ${PROJECT_SOURCE_DIR}/python/server/*.cpp
            ${PROJECT_SOURCE_DIR}/server/*.h
            ${PROJECT_SOURCE_DIR}/server/*.cpp
            ${PROJECT_SOURCE_DIR}/test/parse/*.h
            ${PROJECT_SOURCE_DIR}/test/parse/*.cpp
            ${PROJECT_SOURCE_DIR}/UI/*.h
            ${PROJECT_SOURCE_DIR}/UI/*.cpp
            ${PROJECT_SOURCE_DIR}/UI/CombatReport/*.h
            ${PROJECT_SOURCE_DIR}/UI/CombatReport/*.cpp
            ${PROJECT_SOURCE_DIR}/universe/*.h
            ${PROJECT_SOURCE_DIR}/universe/*.cpp
            ${PROJECT_SOURCE_DIR}/util/*.h
            ${PROJECT_SOURCE_DIR}/util/*.cpp
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Check C++ for malformatted code"
    )
endif()

if(PYCODESTYLE_FOUND)
    configure_file(pep8.ini.in pep8.ini)
    # The script wrapper discards the exit code of `pep8` to allow
    # continuing the build even when there are errors found by `pep8`.
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/pep8.cmake"
        "execute_process(
            COMMAND \"${PYCODESTYLE_EXECUTABLE}\" \"--config=${CMAKE_CURRENT_BINARY_DIR}/pep8.ini\"
                \"${PROJECT_SOURCE_DIR}\"
            WORKING_DIRECTORY \"${CMAKE_CURRENT_BINARY_DIR}\")
    ")
    add_custom_target(check-pep8
        "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/pep8.cmake"
        DEPENDS pep8.ini.in
        COMMENT "Check python code for PEP-8 style conformance"
        VERBATIM
        SOURCES pep8.ini.in
    )
endif()

if(NOT TARGET check)
    add_custom_target(check
        COMMENT "Run code quality checks"
    )
endif()

if(CLANG-FORMAT_FOUND)
    add_dependencies(check check-cpp-format)
endif()

if(CPPCHECK_FOUND)
    add_dependencies(check check-cpp)
endif()

if(PYCODESTYLE_FOUND)
    add_dependencies(check check-pep8)
endif()

#
# Doxygen
#

find_package(Doxygen REQUIRED)

set(DOXYGEN_INPUT_DIR ${PROJECT_SOURCE_DIR}/include)
file(GLOB_RECURSE DOXYGEN_INPUT_DEPENDENCIES ${DOXYGEN_INPUT_DIR}/*.h)

set(DOXYGEN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/docs/gattlib/doxygen)
set(DOXYGEN_INDEX_FILE ${DOXYGEN_OUTPUT_DIR}/xml/index.xml)
set(DOXYFILE_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

# Replace CMake variables by their values
configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)

# Create Doxygen output directory
file(MAKE_DIRECTORY ${DOXYGEN_OUTPUT_DIR})

add_custom_command(OUTPUT ${DOXYGEN_INDEX_FILE}
                   DEPENDS ${DOXYGEN_INPUT_DEPENDENCIES}
                   COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
                   MAIN_DEPENDENCY ${DOXYFILE_OUT} ${DOXYFILE_IN}
                   COMMENT "Generating documentation with Doxygen")
 
add_custom_target(gattlib-doxygen ALL DEPENDS ${DOXYGEN_INDEX_FILE})

#
# Sphynx
#

find_program(SPHINX_EXECUTABLE NAMES sphinx-build DOC "Path to sphinx-build executable")
if (SPHINX_EXECUTABLE)
    set(SPHINX_SOURCE ${CMAKE_CURRENT_SOURCE_DIR})
    set(SPHINX_BUILD ${CMAKE_CURRENT_BINARY_DIR}/docs/gattlib/sphinx)

    add_custom_target(Sphinx ALL
                      COMMAND ${SPHINX_EXECUTABLE} -b html
                              -Dbreathe_projects.gattlib=${DOXYGEN_OUTPUT_DIR}/xml
                              ${SPHINX_SOURCE} ${SPHINX_BUILD}
                      DEPENDS ${DOXYGEN_INDEX_FILE}
                      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                      COMMENT "Generating documentation with Sphinx")
else()
    message("Install Sphinx to generate Python documentation")
endif()

# Python module "tbsdvb" (pybind11). Built into ${CMAKE_BINARY_DIR}/python; installed into the
# interpreter's site-packages (or TBS_PYTHON_INSTALL_DIR).
# NO_EXTRAS: pybind11's own strip step does not quote the path and breaks in directories with
# spaces or parentheses; the module is stripped below instead
pybind11_add_module(tbsdvb_python MODULE NO_EXTRAS tbsdvb_py.cpp)
set_target_properties(tbsdvb_python PROPERTIES OUTPUT_NAME tbsdvb LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/python)
target_link_libraries(tbsdvb_python PRIVATE TbsDvb::tbsdvb TbsDvb::tbsbbproc)
if(CMAKE_STRIP AND NOT MSVC AND CMAKE_BUILD_TYPE MATCHES "^(Release|MinSizeRel)$")
    set(_strip_args "")
    if(APPLE)
        set(_strip_args -x) # keep the symbols the interpreter looks up
    endif()
    add_custom_command(TARGET tbsdvb_python POST_BUILD
        COMMAND ${CMAKE_STRIP} ${_strip_args} $<TARGET_FILE:tbsdvb_python>
        VERBATIM)
endif()

# Relative, so packages relocate: the Debian layout on Linux (on sys.path for the system Python
# with the /usr prefix), site-packages of the build Python elsewhere
if(NOT TBS_PYTHON_INSTALL_DIR)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(TBS_PYTHON_INSTALL_DIR lib/python3/dist-packages)
    else()
        set(TBS_PYTHON_INSTALL_DIR lib/python${Python_VERSION_MAJOR}.${Python_VERSION_MINOR}/site-packages)
    endif()
endif()
install(TARGETS tbsdvb_python LIBRARY DESTINATION ${TBS_PYTHON_INSTALL_DIR})

# Not with sanitizers: the interpreter is not instrumented, so the runtime cannot load first
if(TBS_BUILD_TESTS AND TBS_BUILD_TOOLS AND NOT TBS_SANITIZE AND NOT CMAKE_CXX_FLAGS MATCHES "sanitize")
    add_test(NAME tbsdvb-python COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_tbsdvb.py
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    set_tests_properties(tbsdvb-python PROPERTIES
        ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/python;TBS_MAKE_TEST_STREAM=$<TARGET_FILE:tbs-make-test-stream>")
endif()

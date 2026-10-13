# Python module "tbsdvb" (pybind11). Built into ${CMAKE_BINARY_DIR}/python; installed into the
# interpreter's site-packages (or TBS_PYTHON_INSTALL_DIR).
pybind11_add_module(tbsdvb_python MODULE tbsdvb_py.cpp)
set_target_properties(tbsdvb_python PROPERTIES OUTPUT_NAME tbsdvb LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/python)
target_link_libraries(tbsdvb_python PRIVATE TbsDvb::tbsdvb TbsDvb::tbsbbproc)

if(NOT TBS_PYTHON_INSTALL_DIR)
    if(Python_SITEARCH)
        set(TBS_PYTHON_INSTALL_DIR ${Python_SITEARCH})
    else()
        set(TBS_PYTHON_INSTALL_DIR lib/python3/dist-packages)
    endif()
endif()
install(TARGETS tbsdvb_python LIBRARY DESTINATION ${TBS_PYTHON_INSTALL_DIR})

if(TBS_BUILD_TESTS AND TBS_BUILD_TOOLS)
    add_test(NAME tbsdvb-python COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_tbsdvb.py
             WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    set_tests_properties(tbsdvb-python PROPERTIES
        ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/python;TBS_MAKE_TEST_STREAM=$<TARGET_FILE:tbs-make-test-stream>")
endif()

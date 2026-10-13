# Fuzz targets for the parsers that handle data received off the air.
#
#   clang:  -DTBS_BUILD_FUZZERS=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
#           builds libFuzzer binaries with ASan + UBSan (see fuzz/README.md)
#   other compilers: the same targets are built with a standalone driver that
#           replays the corpus, so ctest still catches regressions.

set(TBS_FUZZ_TARGETS bbframes pseudots psi gse dvbtext capture tsfilter)
set(TBS_FUZZ_CORPUS ${CMAKE_CURRENT_SOURCE_DIR}/corpus)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(TBS_LIBFUZZER ON)
else()
    set(TBS_LIBFUZZER OFF)
    message(STATUS "Fuzzers: compiler is not Clang, building corpus replay drivers only")
endif()

foreach(t ${TBS_FUZZ_TARGETS})
    add_executable(fuzz_${t} fuzz_${t}.cpp FuzzInput.h)
    target_link_libraries(fuzz_${t} PRIVATE TbsDvb::tbsbbproc)
    if(TBS_LIBFUZZER)
        target_compile_options(fuzz_${t} PRIVATE -fsanitize=fuzzer)
        target_link_options(fuzz_${t} PRIVATE -fsanitize=fuzzer)
        # Replay corpus + reproducers as a regular test (libFuzzer runs the files and exits)
        add_test(NAME fuzz-regress-${t}
                 COMMAND fuzz_${t} -runs=0 ${TBS_FUZZ_CORPUS}/${t} ${CMAKE_CURRENT_SOURCE_DIR}/regressions/${t})
    else()
        target_sources(fuzz_${t} PRIVATE standalone_main.cpp)
        add_test(NAME fuzz-regress-${t}
                 COMMAND fuzz_${t} ${TBS_FUZZ_CORPUS}/${t} ${CMAKE_CURRENT_SOURCE_DIR}/regressions/${t})
    endif()
endforeach()

add_executable(tbs-fuzz-gen-corpus gen_corpus.cpp ../tests/StreamBuilders.h)
target_link_libraries(tbs-fuzz-gen-corpus PRIVATE TbsDvb::tbsdvb)

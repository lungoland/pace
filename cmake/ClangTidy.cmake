function(enable_clang_tidy target)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(NOT CLANG_TIDY_EXE)
        message(WARNING "clang-tidy not found; skipping static analysis for ${target}")
        return()
    endif()
    message(STATUS "clang-tidy found: ${CLANG_TIDY_EXE}")
    set_target_properties(${target} PROPERTIES
        CXX_CLANG_TIDY "${CLANG_TIDY_EXE}"
    )
endfunction()

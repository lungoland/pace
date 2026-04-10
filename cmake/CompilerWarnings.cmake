function(set_project_warnings target warnings_as_errors)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(warnings_as_errors)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wdouble-promotion
        )
        if(warnings_as_errors)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

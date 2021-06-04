include_guard(GLOBAL)

function(enable_address_sanitizer)

    set(options NONE)
    set(oneValueArgs TARGET NAME)
    set(multiValueArgs blacklist)
    cmake_parse_arguments(Sanitizer "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Sanitizer supported only for gcc/clang")
    endif()
    message(STATUS "Address sanitizer enabled for ${Sanitizer_TARGET}")
    target_compile_options(${Sanitizer_TARGET} PRIVATE -fsanitize=address,undefined)
    target_compile_options(${Sanitizer_TARGET} PRIVATE -fno-sanitize=signed-integer-overflow)
    target_compile_options(${Sanitizer_TARGET} PRIVATE -fno-sanitize-recover=all)
    target_compile_options(${Sanitizer_TARGET} PRIVATE -fno-omit-frame-pointer)
    target_link_libraries(${Sanitizer_TARGET} -fsanitize=address,undefined)

    if(DEFINED Sanitizer_blacklist)
        if( CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            message(WARNING "gcc does`t support blacklist")
            return()
        endif()
        target_compile_options(${Sanitizer_TARGET} PRIVATE -fsanitize-blacklist=${Sanitizer_blacklist})
        target_link_libraries(${Sanitizer_TARGET} -fsanitize-blacklist=${Sanitizer_blacklist})
    endif()
endfunction()


function(enable_thread_sanitizer)
    set(options NONE)
    set(oneValueArgs TARGET NAME)
    set(multiValueArgs blacklist)
    cmake_parse_arguments(Sanitizer "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Sanitizer supported only for gcc/clang")
    endif()
    message(STATUS "Thread sanitizer enabled")
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)

    if(DEFINED Sanitizer_blacklist)
        if( CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            message(WARNING "gcc does`t support blacklist")
            return()
        endif()

        add_compile_options(-fsanitize-blacklist=${Sanitizer_blacklist})
        add_link_options(-fsanitize-blacklist=${Sanitizer_blacklist})
    endif()

endfunction()


function(enable_memory_sanitizer)
    set(options NONE)
    set(oneValueArgs TARGET NAME)
    set(multiValueArgs blacklist)
    cmake_parse_arguments(Sanitizer "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Sanitizer supported only for gcc/clang")
    endif()
    message(STATUS "Memory sanitizer enabled for ${target_name}")
    target_compile_options(${Sanitizer_TARGET} PRIVATE -fsanitize=memory)    
    target_link_libraries(${Sanitizer_TARGET} -fsanitize=memory )

    if(DEFINED Sanitizer_blacklist)        
        if( CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            message(WARNING "gcc does`t support blacklist")
            return()
        endif()
        target_compile_options(${Sanitizer_TARGET} PRIVATE -fsanitize-blacklist=${Sanitizer_blacklist})
        target_link_libraries(${Sanitizer_TARGET} -fsanitize-blacklist=${Sanitizer_blacklist})
    endif()

endfunction()
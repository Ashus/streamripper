# Copy only toolchain DLLs imported by the executable or its runtime libraries.
# Windows system libraries remain provided by the operating system.
set(pending "${SR_EXECUTABLE}")
set(visited)
get_filename_component(destination "${SR_EXECUTABLE}" DIRECTORY)

while(pending)
    list(POP_FRONT pending binary)
    get_filename_component(name "${binary}" NAME)
    string(TOLOWER "${name}" key)
    if(key IN_LIST visited)
        continue()
    endif()
    list(APPEND visited "${key}")
    execute_process(
        COMMAND "${SR_OBJDUMP}" -p "${binary}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE imports
        ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Unable to inspect runtime dependencies of ${binary}: ${error}")
    endif()
    string(REGEX MATCHALL "DLL Name:[ \t]*[^\r\n]+" dependencies "${imports}")
    foreach(dependency IN LISTS dependencies)
        string(REGEX REPLACE "^DLL Name:[ \t]*" "" dependency "${dependency}")
        string(STRIP "${dependency}" dependency)
        set(source "${SR_RUNTIME_DIR}/${dependency}")
        if(EXISTS "${source}")
            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${source}" "${destination}/${dependency}"
                RESULT_VARIABLE result)
            if(NOT result EQUAL 0)
                message(FATAL_ERROR "Unable to copy runtime library: ${source}")
            endif()
            list(APPEND pending "${source}")
        endif()
    endforeach()
endwhile()

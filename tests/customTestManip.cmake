foreach(test ${allTests})
    # The integrate tests should for now not run in parallell
    if(test MATCHES "Integrate")
        message(STATUS "* ${test} will run in serial")
        set_tests_properties(${test} PROPERTIES RUN_SERIAL TRUE)
    endif()
endforeach()

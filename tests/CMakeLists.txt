# One executable per test file; each is registered with CTest.
function(visualis_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
    # Tests never need a real display; outputs and windows render offscreen.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

visualis_add_test(tst_uistartup Qt6::Quick visualis_ui visualis_uiplugin)
visualis_add_test(tst_library visualis_core)

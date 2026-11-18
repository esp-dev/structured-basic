include_guard(GLOBAL)

# Embedders may choose their own target name and add flags or limit overrides
# with target_compile_options/target_compile_definitions after this call.
function(sbasic_add_core target)
  cmake_parse_arguments(PARSE_ARGV 1 SB "WITH_TERMINAL" "" "")
  if(SB_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "Unexpected sbasic_add_core arguments: ${SB_UNPARSED_ARGUMENTS}")
  endif()
  set(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
  add_library(${target} STATIC
    "${root}/src/compiler.c"
    "${root}/src/vm.c"
    "${root}/src/value.c"
    "${root}/src/heap.c"
    "${root}/src/builtins.c"
    "${root}/src/native.c"
    "${root}/src/validate.c"
    "${root}/src/errors.c"
    "${root}/src/storage.c")
  if(SB_WITH_TERMINAL)
    target_sources(${target} PRIVATE "${root}/src/terminal.c")
  endif()
  target_include_directories(${target} PUBLIC "${root}/include")
  target_compile_features(${target} PUBLIC c_std_99)
  if(NOT MSVC)
    target_link_libraries(${target} PUBLIC m)
  endif()
endfunction()

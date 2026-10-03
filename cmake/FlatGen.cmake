find_program(FLATC_EXECUTABLE flatc REQUIRED)
find_path(FLATBUFFERS_INCLUDE_DIR flatbuffers/flatbuffers.h REQUIRED)

function(add_flatbuffers_library target)
  set(out_dir ${CMAKE_CURRENT_BINARY_DIR}/generated)
  set(headers)

  foreach(schema IN LISTS ARGN)
    get_filename_component(abs ${schema} ABSOLUTE)
    get_filename_component(stem ${schema} NAME_WE)
    set(header ${out_dir}/${stem}_generated.h)

    add_custom_command(
      OUTPUT ${header}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${out_dir}
      COMMAND ${FLATC_EXECUTABLE} --cpp -o ${out_dir} ${abs}
      DEPENDS ${abs}
      COMMENT "flatc ${stem}.fbs"
      VERBATIM)

    list(APPEND headers ${header})
  endforeach()

  add_custom_target(${target}_gen DEPENDS ${headers})

  add_library(${target} INTERFACE)
  add_dependencies(${target} ${target}_gen)
  target_include_directories(${target} SYSTEM INTERFACE ${out_dir}
                                                        ${FLATBUFFERS_INCLUDE_DIR})
endfunction()

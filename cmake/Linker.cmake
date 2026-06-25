macro(gauges_configure_linker project_name)
  set(gauges_USER_LINKER_OPTION
    "DEFAULT"
      CACHE STRING "Linker to be used")
    set(gauges_USER_LINKER_OPTION_VALUES "DEFAULT" "SYSTEM" "LLD" "GOLD" "BFD" "MOLD" "SOLD" "APPLE_CLASSIC" "MSVC")
  set_property(CACHE gauges_USER_LINKER_OPTION PROPERTY STRINGS ${gauges_USER_LINKER_OPTION_VALUES})
  list(
    FIND
    gauges_USER_LINKER_OPTION_VALUES
    ${gauges_USER_LINKER_OPTION}
    gauges_USER_LINKER_OPTION_INDEX)

  if(${gauges_USER_LINKER_OPTION_INDEX} EQUAL -1)
    message(
      STATUS
        "Using custom linker: '${gauges_USER_LINKER_OPTION}', explicitly supported entries are ${gauges_USER_LINKER_OPTION_VALUES}")
  endif()

  set_target_properties(${project_name} PROPERTIES LINKER_TYPE "${gauges_USER_LINKER_OPTION}")
endmacro()

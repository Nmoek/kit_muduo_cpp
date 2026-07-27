#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "date::date-tz" for configuration ""
set_property(TARGET date::date-tz APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(date::date-tz PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libdate-tz.so.3.0.4"
  IMPORTED_SONAME_NOCONFIG "libdate-tz.so.3"
  )

list(APPEND _IMPORT_CHECK_TARGETS date::date-tz )
list(APPEND _IMPORT_CHECK_FILES_FOR_date::date-tz "${_IMPORT_PREFIX}/lib/libdate-tz.so.3.0.4" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)

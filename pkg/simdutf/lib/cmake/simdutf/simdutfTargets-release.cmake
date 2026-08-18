#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "simdutf::simdutf" for configuration "Release"
set_property(TARGET simdutf::simdutf APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(simdutf::simdutf PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libsimdutf.so.34.0.0"
  IMPORTED_SONAME_RELEASE "libsimdutf.so.34"
  )

list(APPEND _IMPORT_CHECK_TARGETS simdutf::simdutf )
list(APPEND _IMPORT_CHECK_FILES_FOR_simdutf::simdutf "${_IMPORT_PREFIX}/lib/libsimdutf.so.34.0.0" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)

# FindZLIB.cmake - Custom module to find bundled zlib
# This module finds the bundled zlib in externals/zlib

if(TARGET zlibstatic)
  # zlibstatic target already exists (from add_subdirectory)
  set(ZLIB_FOUND TRUE)
  set(ZLIB_LIBRARY zlibstatic)
  set(ZLIB_LIBRARIES zlibstatic)
  set(ZLIB_INCLUDE_DIR ${zlib_SOURCE_DIR})
  set(ZLIB_INCLUDE_DIRS ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
  set(ZLIB_VERSION_STRING "1.3.2.1")
  
  # Create the imported target if it doesn't exist
  if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
  endif()
else()
  # Try to find zlib in the externals directory
  find_path(ZLIB_INCLUDE_DIR zlib.h
    PATHS
      ${CMAKE_SOURCE_DIR}/externals/zlib
      ${CMAKE_SOURCE_DIR}/externals/zlib/include
    NO_DEFAULT_PATH
  )
  
  if(ZLIB_INCLUDE_DIR)
    set(ZLIB_FOUND TRUE)
    set(ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIR})
    set(ZLIB_VERSION_STRING "1.3.2.1")
    
    # Try to find the library
    find_library(ZLIB_LIBRARY zlibstatic
      PATHS
        ${CMAKE_BINARY_DIR}/externals/zlib
        ${CMAKE_BINARY_DIR}/externals/zlib/Release
        ${CMAKE_BINARY_DIR}/externals/zlib/Debug
      NO_DEFAULT_PATH
    )
    
    if(ZLIB_LIBRARY)
      set(ZLIB_LIBRARIES ${ZLIB_LIBRARY})
      
      # Create imported target
      if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB UNKNOWN IMPORTED)
        set_target_properties(ZLIB::ZLIB PROPERTIES
          IMPORTED_LOCATION ${ZLIB_LIBRARY}
          INTERFACE_INCLUDE_DIRECTORIES ${ZLIB_INCLUDE_DIR}
        )
      endif()
    endif()
  endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ZLIB
  REQUIRED_VARS ZLIB_LIBRARY ZLIB_INCLUDE_DIR
  VERSION_VAR ZLIB_VERSION_STRING
)

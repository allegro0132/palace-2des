# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

#
# Build METIS and ParMETIS
#

# Force build order
set(METIS_PARMETIS_DEPENDENCIES)

# Build GKlib
set(GKLIB_OPTIONS ${PALACE_SUPERBUILD_DEFAULT_ARGS})
list(APPEND GKLIB_OPTIONS
  "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
  "-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}"
)
if(CMAKE_BUILD_TYPE MATCHES "Debug|debug|DEBUG")
  list(APPEND GKLIB_OPTIONS
    "-DDEBUG=ON"
    "-DASSERT=ON"
    "-DASSERT2=ON"
  )
endif()
if(PALACE_WITH_OPENMP)
  list(APPEND GKLIB_OPTIONS
    "-DOPENMP=ON"
  )
endif()

string(REPLACE ";" "; " GKLIB_OPTIONS_PRINT "${GKLIB_OPTIONS}")
message(STATUS "GKLIB_OPTIONS: ${GKLIB_OPTIONS_PRINT}")

# Some build fixes
set(GKLIB_PATCH_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/patch/GKlib/patch_build.diff"
  "${CMAKE_CURRENT_SOURCE_DIR}/patch/GKlib/patch_install.diff"
)

include(ExternalProject)
ExternalProject_Add(gklib
  DEPENDS           ${METIS_PARMETIS_DEPENDENCIES}
  GIT_REPOSITORY    ${CMAKE_CURRENT_SOURCE_DIR}/GKlib
  GIT_TAG           ${EXTERN_GKLIB_GIT_TAG}
  SOURCE_DIR        ${CMAKE_CURRENT_BINARY_DIR}/GKlib
  BINARY_DIR        ${CMAKE_CURRENT_BINARY_DIR}/GKlib-build
  INSTALL_DIR       ${CMAKE_INSTALL_PREFIX}
  PREFIX            ${CMAKE_CURRENT_BINARY_DIR}/GKlib-cmake
  UPDATE_COMMAND    ""
  PATCH_COMMAND     git apply "${GKLIB_PATCH_FILES}"
  CONFIGURE_COMMAND cmake <SOURCE_DIR> "${GKLIB_OPTIONS}"
  TEST_COMMAND      ""
)
list(APPEND METIS_PARMETIS_DEPENDENCIES gklib)

# Build METIS (build settings are passed from GKlib)
set(METIS_OPTIONS ${PALACE_SUPERBUILD_DEFAULT_ARGS})
list(APPEND METIS_OPTIONS
  "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
  "-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}"
  "-DGKlib_ROOT=${CMAKE_INSTALL_PREFIX}"
)

string(REPLACE ";" "; " METIS_OPTIONS_PRINT "${METIS_OPTIONS}")
message(STATUS "METIS_OPTIONS: ${METIS_OPTIONS_PRINT}")

# Some build fixes
set(METIS_PATCH_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/patch/METIS/patch_build.diff"
  "${CMAKE_CURRENT_SOURCE_DIR}/patch/METIS/patch_install.diff"
)

# Configure width of real and integer values
list(APPEND METIS_PATCH_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/patch/METIS/patch_real32.diff"
)
if(PALACE_WITH_64BIT_INT)
  list(APPEND METIS_PATCH_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/patch/METIS/patch_idx64.diff"
  )
else()
  list(APPEND METIS_PATCH_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/patch/METIS/patch_idx32.diff"
  )
endif()

ExternalProject_Add(metis
  DEPENDS           ${METIS_PARMETIS_DEPENDENCIES}
  GIT_REPOSITORY    ${CMAKE_CURRENT_SOURCE_DIR}/METIS
  GIT_TAG           ${EXTERN_METIS_GIT_TAG}
  SOURCE_DIR        ${CMAKE_CURRENT_BINARY_DIR}/METIS
  BINARY_DIR        ${CMAKE_CURRENT_BINARY_DIR}/METIS-build
  INSTALL_DIR       ${CMAKE_INSTALL_PREFIX}
  PREFIX            ${CMAKE_CURRENT_BINARY_DIR}/METIS-cmake
  UPDATE_COMMAND    ""
  PATCH_COMMAND     git apply "${METIS_PATCH_FILES}"
  CONFIGURE_COMMAND cmake <SOURCE_DIR> ${METIS_OPTIONS}
  TEST_COMMAND      ""
)
list(APPEND METIS_PARMETIS_DEPENDENCIES metis)

# Build ParMETIS (as needed)
if(PALACE_WITH_SUPERLU OR PALACE_WITH_STRUMPACK)
  set(PARMETIS_OPTIONS ${METIS_OPTIONS})
  list(APPEND PARMETIS_OPTIONS
    "-Dmetis_ROOT=${CMAKE_INSTALL_PREFIX}"
  )

  string(REPLACE ";" "; " PARMETIS_OPTIONS_PRINT "${PARMETIS_OPTIONS}")
  message(STATUS "PARMETIS_OPTIONS: ${PARMETIS_OPTIONS_PRINT}")

  # Apply some fixes for build and from Spack build
  # (https://github.com/spack/spack/tree/develop/var/spack/repos/builtin/packages/parmetis)
  set(PARMETIS_PATCH_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/patch/ParMETIS/patch_build.diff"
    "${CMAKE_CURRENT_SOURCE_DIR}/patch/ParMETIS/patch_install.diff"
    "${CMAKE_CURRENT_SOURCE_DIR}/patch/ParMETIS/patch_spack.diff"
    "${CMAKE_CURRENT_SOURCE_DIR}/patch/ParMETIS/patch_braces.diff"
  )

  ExternalProject_Add(parmetis
    DEPENDS           ${METIS_PARMETIS_DEPENDENCIES}
    GIT_REPOSITORY    ${CMAKE_CURRENT_SOURCE_DIR}/ParMETIS
    GIT_TAG           ${EXTERN_PARMETIS_GIT_TAG}
    SOURCE_DIR        ${CMAKE_CURRENT_BINARY_DIR}/ParMETIS
    BINARY_DIR        ${CMAKE_CURRENT_BINARY_DIR}/ParMETIS-build
    INSTALL_DIR       ${CMAKE_INSTALL_PREFIX}
    PREFIX            ${CMAKE_CURRENT_BINARY_DIR}/ParMETIS-cmake
    UPDATE_COMMAND    ""
    PATCH_COMMAND     git apply "${PARMETIS_PATCH_FILES}"
    CONFIGURE_COMMAND cmake <SOURCE_DIR> "${PARMETIS_OPTIONS}"
    TEST_COMMAND      ""
  )
endif()

include(GNUInstallDirs)
if(BUILD_SHARED_LIBS)
  set(_METIS_LIB_SUFFIX ${CMAKE_SHARED_LIBRARY_SUFFIX})
else()
  set(_METIS_LIB_SUFFIX ${CMAKE_STATIC_LIBRARY_SUFFIX})
endif()
set(_METIS_LIBRARIES ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libGKlib${_METIS_LIB_SUFFIX})
set(_METIS_LIBRARIES ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libmetis${_METIS_LIB_SUFFIX}$<SEMICOLON>${_METIS_LIBRARIES})
set(METIS_LIBRARIES ${_METIS_LIBRARIES} CACHE STRING "List of library files for METIS")
set(METIS_INCLUDE_DIRS ${CMAKE_INSTALL_PREFIX}/include CACHE STRING "Path to METIS include directories")
if(TARGET parmetis)
  set(_PARMETIS_LIBRARIES ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libparmetis${_METIS_LIB_SUFFIX})
  set(PARMETIS_LIBRARIES ${_PARMETIS_LIBRARIES} CACHE STRING "List of library files for ParMETIS")
endif()

# - Try to find audaspace
# Once done, this will define
#
#  AUDASPACE_FOUND - system has audaspace
#  AUDASPACE_INCLUDE_DIRS - the audaspace include directories
#  AUDASPACE_LIBRARIES - link these to use audaspace
#  AUDASPACE_C_FOUND - system has audaspace's C binding
#  AUDASPACE_C_INCLUDE_DIRS - the audaspace's C binding include directories
#  AUDASPACE_C_LIBRARIES - link these to use audaspace's C binding
#  AUDASPACE_PY_FOUND - system has audaspace's python binding
#  AUDASPACE_PY_INCLUDE_DIRS - the audaspace's python binding include directories
#  AUDASPACE_PY_LIBRARIES - link these to use audaspace's python binding

IF(NOT AUDASPACE_ROOT_DIR AND NOT $ENV{AUDASPACE_ROOT_DIR} STREQUAL "")
 SET(AUDASPACE_ROOT_DIR $ENV{AUDASPACE_ROOT_DIR})
ENDIF()

SET(_audaspace_SEARCH_DIRS
  ${AUDASPACE_ROOT_DIR}
  /usr/local
  /sw # Fink
  /opt/local # DarwinPorts
)

# Use pkg-config to get hints about paths
FIND_PACKAGE(PkgConfig)
IF(PKG_CONFIG_FOUND)
  PKG_CHECK_MODULES(AUDASPACE_PKGCONF audaspace)
ENDIF(PKG_CONFIG_FOUND)

# Include dir
FIND_PATH(AUDASPACE_INCLUDE_DIR
  NAMES ISound.h
  HINTS ${_audaspace_SEARCH_DIRS}
  PATHS ${AUDASPACE_PKGCONF_INCLUDE_DIRS}
  PATH_SUFFIXES include/audaspace
)

# Library
FIND_LIBRARY(AUDASPACE_LIBRARY
  NAMES audaspace
  HINTS ${_audaspace_SEARCH_DIRS}
  PATHS ${AUDASPACE_PKGCONF_LIBRARY_DIRS}
  PATH_SUFFIXES lib lib64
)

# Include dir
FIND_PATH(AUDASPACE_C_INCLUDE_DIR
  NAMES AUD_Sound.h
  HINTS ${_audaspace_SEARCH_DIRS}
  PATHS ${AUDASPACE_PKGCONF_INCLUDE_DIRS}
  PATH_SUFFIXES include/audaspace
)

# Library
FIND_LIBRARY(AUDASPACE_C_LIBRARY
  NAMES audaspace-c
  HINTS ${_audaspace_SEARCH_DIRS}
  PATHS ${AUDASPACE_PKGCONF_LIBRARY_DIRS}
  PATH_SUFFIXES lib lib64
)

# Include dir
FIND_PATH(AUDASPACE_PY_INCLUDE_DIR
  NAMES python/PyAPI.h
  HINTS ${_audaspace_SEARCH_DIRS}
  PATHS ${AUDASPACE_PKGCONF_INCLUDE_DIRS}
  PATH_SUFFIXES include/audaspace
)

# Library
FIND_LIBRARY(AUDASPACE_PY_LIBRARY
  NAMES audaspace-py
  HINTS ${_audaspace_SEARCH_DIRS}
  PATHS ${AUDASPACE_PKGCONF_LIBRARY_DIRS}
  PATH_SUFFIXES lib lib64
)

FIND_PACKAGE(PackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Audaspace  DEFAULT_MSG  AUDASPACE_LIBRARY AUDASPACE_INCLUDE_DIR)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Audaspace_C  DEFAULT_MSG  AUDASPACE_C_LIBRARY AUDASPACE_C_INCLUDE_DIR)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Audaspace_Py  DEFAULT_MSG  AUDASPACE_PY_LIBRARY AUDASPACE_PY_INCLUDE_DIR)

IF(AUDASPACE_FOUND)
  SET(AUDASPACE_LIBRARIES ${AUDASPACE_LIBRARY})
  SET(AUDASPACE_INCLUDE_DIRS ${AUDASPACE_INCLUDE_DIR})
ENDIF(AUDASPACE_FOUND)

IF(AUDASPACE_C_FOUND)
  SET(AUDASPACE_C_LIBRARIES ${AUDASPACE_C_LIBRARY})
  SET(AUDASPACE_C_INCLUDE_DIRS ${AUDASPACE_C_INCLUDE_DIR})
ENDIF(AUDASPACE_C_FOUND)

IF(AUDASPACE_PY_FOUND)
  SET(AUDASPACE_PY_LIBRARIES ${AUDASPACE_PY_LIBRARY})
  SET(AUDASPACE_PY_INCLUDE_DIRS ${AUDASPACE_PY_INCLUDE_DIR})
ENDIF(AUDASPACE_PY_FOUND)

MARK_AS_ADVANCED(
  AUDASPACE_LIBRARY
  AUDASPACE_LIBRARIES
  AUDASPACE_INCLUDE_DIR
  AUDASPACE_INCLUDE_DIRS
  AUDASPACE_C_LIBRARY
  AUDASPACE_C_LIBRARIES
  AUDASPACE_C_INCLUDE_DIR
  AUDASPACE_C_INCLUDE_DIRS
  AUDASPACE_PY_LIBRARY
  AUDASPACE_PY_LIBRARIES
  AUDASPACE_PY_INCLUDE_DIR
  AUDASPACE_PY_INCLUDE_DIRS
)

##-----------------------------------------------------------------------------
##  Welcome to Streamripper
##-----------------------------------------------------------------------------
CMAKE_MINIMUM_REQUIRED (VERSION 3.16)
PROJECT (STREAMRIPPER C)

OPTION (SR_MODERN_WINDOWS "Build the CLI with current MinGW libraries and TLS" ${MINGW})
IF (SR_MODERN_WINDOWS)
  INCLUDE (cmake/ModernWindows.cmake)
  RETURN ()
ENDIF ()

## Tell CMake to be quiet
CMAKE_POLICY (SET CMP0003 NEW)

## Default to release
IF (NOT CMAKE_BUILD_TYPE)
  SET (CMAKE_BUILD_TYPE Release)
ENDIF (NOT CMAKE_BUILD_TYPE)

##-----------------------------------------------------------------------------
##  Include files
##-----------------------------------------------------------------------------
INCLUDE (CheckFunctionExists)
INCLUDE (CheckLibraryExists)
INCLUDE (FindPkgConfig)

# http://www.cmake.org/pipermail/cmake/2008-December/025886.html
INCLUDE (InstallRequiredSystemLibraries)


##-----------------------------------------------------------------------------
##  Find libraries
##-----------------------------------------------------------------------------
IF (UNIX)
  SET (MATH_LIB -lm)
ELSE (UNIX)
  SET (MATH_LIB)
ENDIF (UNIX)

IF (UNIX)
  PKG_CHECK_MODULES (GLIB2 REQUIRED glib-2.0>=2.16)
ENDIF (UNIX)

SET (CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
FIND_PACKAGE (IBERTY)
FIND_PACKAGE (MAD)
FIND_PACKAGE (OGG)
FIND_PACKAGE (VORBIS)


INCLUDE (${CMAKE_SOURCE_DIR}/cmake/SR_FindSockets.cmake)

FIND_LIBRARY (SEMAPHORE_LIBRARIES NAMES sem rt)

##-----------------------------------------------------------------------------
##  Include directories
##-----------------------------------------------------------------------------
INCLUDE_DIRECTORIES (BEFORE ${GLIB2_INCLUDE_DIRS})
INCLUDE_DIRECTORIES (BEFORE ${CMAKE_CURRENT_BINARY_DIR})
INCLUDE_DIRECTORIES (BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/lib)


##-----------------------------------------------------------------------------
##  Configure include file
##-----------------------------------------------------------------------------
CONFIGURE_FILE (${CMAKE_CURRENT_SOURCE_DIR}/sr_config.h.cmake 
  ${CMAKE_CURRENT_BINARY_DIR}/sr_config.h)

##-----------------------------------------------------------------------------
##  Copy dlls
##-----------------------------------------------------------------------------

##-----------------------------------------------------------------------------
##  Build subdirectories
##-----------------------------------------------------------------------------
ADD_SUBDIRECTORY (lib)

##-----------------------------------------------------------------------------
##  Source files
##-----------------------------------------------------------------------------
SET(STREAMRIPPER_SRC
	console/streamripper.c
	)


##-----------------------------------------------------------------------------
##  Targets
##-----------------------------------------------------------------------------

MESSAGE ("MAD_LIBRARIES = ${MAD_LIBRARIES}")
SET (STREAMRIPPER_LIBS ${MAD_LIBRARIES} ${GLIB2_LIBRARIES} ${MATH_LIB})


IF (SEMAPHORE_LIBRARIES)
  SET (STREAMRIPPER_LIBS ${STREAMRIPPER_LIBS} ${SEMAPHORE_LIBRARIES})
ENDIF (SEMAPHORE_LIBRARIES)

IF (IBERTY_FOUND)
  SET (STREAMRIPPER_LIBS ${STREAMRIPPER_LIBS} ${IBERTY_LIBRARIES})
ENDIF (IBERTY_FOUND)

IF (OGG_FOUND AND VORBIS_FOUND)
  SET (STREAMRIPPER_LIBS ${STREAMRIPPER_LIBS} ${OGG_LIBRARIES}
    ${VORBIS_LIBRARIES})
ENDIF (OGG_FOUND AND VORBIS_FOUND)

SET (STREAMRIPPER_LIBS ${STREAMRIPPER_LIBS} ${SR_EXTRA_LIBS} ${MATH_LIB})

##-----------------------------------------------------------------------------
##  Linking
##-----------------------------------------------------------------------------
## chkstk.o may be required for mingw

#ADD_LIBRARY (streamripper1 ${STREAMRIPPER_LIB_SRC})
ADD_EXECUTABLE (streamripper ${STREAMRIPPER_SRC})
TARGET_LINK_LIBRARIES (streamripper ${MY_MATH_LIB} 
  streamripper1 ${STREAMRIPPER_LIBS})



##-----------------------------------------------------------------------------
##  Install
##-----------------------------------------------------------------------------
INSTALL(TARGETS streamripper DESTINATION bin)

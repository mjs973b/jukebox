# Option for build or not TunePimp

# Copyright (c) 2006, Laurent Montel, <montel@kde.org>
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.


if(TUNEPIMP_INCLUDE_DIR AND TUNEPIMP_LIBRARIES)
	# Already in cache, be silent
	set(TunePimp_FIND_QUIETLY TRUE)	
endif(TUNEPIMP_INCLUDE_DIR AND TUNEPIMP_LIBRARIES)

FIND_PATH(TUNEPIMP_INCLUDE_DIR tunepimp/tp_c.h)

# Search tunepimp-0.5 
if(NOT TUNEPIMP_INCLUDE_DIR)
    FIND_PATH(TUNEPIMP_INCLUDE_DIR tunepimp-0.5/tp_c.h)
endif(NOT TUNEPIMP_INCLUDE_DIR)	

FIND_LIBRARY(TUNEPIMP_LIBRARIES NAMES tunepimp)

if(TUNEPIMP_INCLUDE_DIR AND TUNEPIMP_LIBRARIES)
   MESSAGE( STATUS "tunepimp found: includes in ${TUNEPIMP_INCLUDE_DIR}, library in ${TUNEPIMP_LIBRARIES}")
   set(TUNEPIMP_FOUND TRUE)
   INCLUDE(CheckLibraryExists)
   SET(old_flags ${CMAKE_REQUIRED_LIBRARIES})
   SET(CMAKE_REQUIRED_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})
   CHECK_LIBRARY_EXISTS(${TUNEPIMP_LIBRARIES} tp_SetFileNameEncoding "" TUNEPIMP_FOUND_VERSION_4)
   CHECK_LIBRARY_EXISTS(${TUNEPIMP_LIBRARIES} tr_GetPUID "" TUNEPIMP_FOUND_VERSION_5)
   SET(CMAKE_REQUIRED_LIBRARIES ${old_flags})
   MESSAGE(STATUS "TUNEPIMP_FOUND_VERSION_4 :<${TUNEPIMP_FOUND_VERSION_4}>")
   MESSAGE(STATUS "TUNEPIMP_FOUND_VERSION_5 :<${TUNEPIMP_FOUND_VERSION_5}>")
else(TUNEPIMP_INCLUDE_DIR AND TUNEPIMP_LIBRARIES)
   MESSAGE( STATUS "tunepimp not found")
endif(TUNEPIMP_INCLUDE_DIR AND TUNEPIMP_LIBRARIES)

MARK_AS_ADVANCED(TUNEPIMP_INCLUDE_DIR TUNEPIMP_LIBRARIES)


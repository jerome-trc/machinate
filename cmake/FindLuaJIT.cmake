# Locate Lua library
# This module defines
#  LUAJIT_FOUND, if false, do not try to link to Lua
#  LUAJIT_LIBRARIES
#  LUAJIT_INCLUDE_DIR, where to find lua.h
#
# Note that the expected include convention is
#  #include "lua.h"
# and not
#  #include <lua/lua.h>
# This is because, the lua location is not standardized and may exist
# in locations other than lua/

#=============================================================================
# Copyright 2007-2009 Kitware, Inc.
#
# Distributed under the OSI-approved BSD License (the "License");
# see accompanying file Copyright.txt for details.
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================
# (To distributed this file outside of CMake, substitute the full
#  License text for the above reference.)
#
# ################
# 2010 - modified for cronkite to find luajit instead of lua, as it was before.
# 2021 - modified for Vaxis to support LuaJIT as a submodule.
#

find_path(LUAJIT_INCLUDE_DIR lua.h
  HINTS
  $ENV{LUAJIT_DIR}
  PATH_SUFFIXES luajit-2.0 luajit2.0 luajit luajit-2.1
  PATHS
  /usr/local
  /usr
  ${CMAKE_SOURCE_DIR}/lib/LuaJIT/src
)

find_library(LUAJIT_LIBRARY
  NAMES libluajit-51.a libluajit-5.1.a libluajit.a libluajit-5.1.so libluajit-5.1.dll.a luajit
  HINTS
  $ENV{LUAJIT_DIR}
  PATH_SUFFIXES lib64 lib
  PATHS
  /usr/local
  /usr
  ${CMAKE_SOURCE_DIR}/lib/LuaJIT/src
)

# Situationally necessary for MSVC. YMMV
find_library(LUA_LIBRARY
  NAMES lua51
  HINTS
  $ENV{LUAJIT_DIR}
  PATH_SUFFIXES lib
  PATHS
  ${CMAKE_SOURCE_DIR}/lib/LuaJIT/src
)

if(LUAJIT_LIBRARY)
  if(UNIX AND NOT APPLE)
    find_library(LUAJIT_MATH_LIBRARY m)
	  find_library(LUAJIT_DL_LIBRARY dl)
	  set( LUAJIT_LIBRARIES "${LUAJIT_LIBRARY};${LUAJIT_DL_LIBRARY};${LUAJIT_MATH_LIBRARY}" CACHE STRING "Lua Libraries")
  else(UNIX AND NOT APPLE)
    set( LUAJIT_LIBRARIES "${LUAJIT_LIBRARY}" CACHE STRING "Lua Libraries")
  endif(UNIX AND NOT APPLE)
endif(LUAJIT_LIBRARY)

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set LUAJIT_FOUND to TRUE if
# all listed variables are TRUE
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LuaJIT  DEFAULT_MSG  LUAJIT_LIBRARIES LUAJIT_INCLUDE_DIR)

mark_as_advanced(LUAJIT_INCLUDE_DIR LUAJIT_LIBRARIES LUAJIT_LIBRARY LUAJIT_MATH_LIBRARY)

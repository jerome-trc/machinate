/**
 * @file preproc.hpp
 * @brief General-purpose preprocessor macros.
 */

#pragma once

#define DELETE_COPIERS(type)    \
	type(const type&) = delete; \
	type& operator=(const type&) = delete;
#define DELETE_MOVERS(type) \
	type(type&&) = delete;  \
	type& operator=(type&&) = delete;

#define DELETE_COPIERS_AND_MOVERS(type) DELETE_COPIERS(type) DELETE_MOVERS(type)

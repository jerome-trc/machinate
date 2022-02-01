/**
 * @file string.hpp
 * @brief String helper functions missing from both the C and C++ standard libs.
 */

#pragma once

#include <string>

/** @brief Shorthand for `strcmp(s1, s2) == 0`. */
bool streq(const char* const s1, const char* const s2);

/** @returns all parts of `string` between `delimiter`. */
[[nodiscard]] std::vector<std::string> str_split(
	const std::string& string, const std::string& delimiter);

/** @returns all parts of `string` between `delimiter`. */
[[nodiscard]] std::vector<std::string> str_split(
	const std::string& string, const char delimiter);

/** @brief Apply a lowercase transformation to the entire given string. */
[[nodiscard]] std::string str_tolower(const std::string&);

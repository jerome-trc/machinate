/**
 * @file algo.hpp
 * @brief Helper functions in the style of those from the <algorith> STL header.
 */

#pragma once

#include <functional>
#include <vector>

template<class T>
[[nodiscard]] bool dereference_less(T const* l, T const* r)
{
	return *l < *r;
}

/// @copyright Provided by StackOverflow user Potatoswatter under CC BY-SA 2.5.
/// @link https://stackoverflow.com/a/2769222/17412187
/// @returns `false` if the given range is empty.
template<class I>
[[nodiscard]] bool all_elements_unique(I first, I last)
{
	using T = std::iterator_traits<I>::value_type;

	if ((last - first) == 0) return false;

	std::vector<T const*> vp;
	vp.reserve(last - first);

	for (auto iter = first; iter < last; iter++) vp.push_back(&*iter);

	std::sort(vp.begin(), vp.end(), std::ptr_fun(&dereference_less<T>));

	return std::adjacent_find(
				vp.begin(), vp.end(),
				std::not2(std::ptr_fun(&dereference_less<T>))) // "opposite functor"
			== vp.end(); // if no adjacent pair (vp_n,vp_n+1) has *vp_n < *vp_n+1
}

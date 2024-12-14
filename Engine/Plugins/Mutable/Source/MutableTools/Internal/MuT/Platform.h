// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <vector>
#include <map>
#include <set>
#include <unordered_map>


namespace mu
{

	//! STL-like containers using this allocator
	template< typename T >
	using vector = std::vector<T>;

	template< typename K, typename T >
	using map = std::map< K, T, std::less<K> >;

	template< typename T >
	using set = std::set< T, std::less<T> >;

	template< typename T >
	using multiset = std::multiset< T, std::less<T> >;

	template< typename K, typename T >
	using pair = std::pair<K, T>;

}


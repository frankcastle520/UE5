// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "CoreTypes.h"
#include "VVMValue.h"

#define V_RETURN(Value)                   \
	return                                \
	{                                     \
		::Verse::FOpResult::Return, Value \
	}
#define V_REQUIRE_CONCRETE(Value)                  \
	if ((Value).IsPlaceholder())                   \
	{                                              \
		return {::Verse::FOpResult::Block, Value}; \
	}
#define V_FAIL_IF(Condition)               \
	if (Condition)                         \
	{                                      \
		return {::Verse::FOpResult::Fail}; \
	}
#define V_FAIL_UNLESS(Condition)           \
	if (!(Condition))                      \
	{                                      \
		return {::Verse::FOpResult::Fail}; \
	}
#define V_YIELD()                 \
	return                        \
	{                             \
		::Verse::FOpResult::Yield \
	}
#define V_RUNTIME_ERROR(Context, Message)                                 \
	return                                                                \
	{                                                                     \
		::Verse::FOpResult::Error, ::Verse::VArray::New(Context, Message) \
	}
#define V_RUNTIME_ERROR_IF(Condition, Context, Message) \
	if (Condition)                                      \
	{                                                   \
		V_RUNTIME_ERROR(Context, Message);              \
	}

namespace Verse
{

// Represents the result of a single VM operation
struct FOpResult
{
	enum EKind
	{
		Return, // All went well, and Value is the result.
		Block,  // A placeholder was encountered, and this operation should be enqueued on Value.
		Fail,   // The current choice failed. Value is undefined.
		Yield,  // The task suspended, and execution should continue in the resumer. Value is undefined.
		Error,  // A runtime error occurred, and Value holds a VArray with an error message.
	};

	FOpResult(EKind Kind, VValue Value = VValue())
		: Kind(Kind)
		, Value(Value)
	{
	}

	EKind Kind;
	VValue Value;
};

} // namespace Verse
#endif // WITH_VERSE_VM

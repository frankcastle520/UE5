// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlainPropsCtti.h"
#include <type_traits>
#include <string_view>

namespace PlainProps::Test
{

enum class E1 : uint8_t { A, B };

namespace Actual
{	
	PP_REFLECT_ENUM(PlainProps::Test, E1, A, B);
}

namespace Expect
{
	struct E1_Ctti
	{
		static constexpr char Name[] = "E1";
		using Type = ::PlainProps::Test::E1;
		static constexpr int NumEnumerators = 2;
		static constexpr struct { const char* Name; Type Constant; } Enumerators[] = { {"A", Type::A},  {"B", Type::B} };
	};
}

static_assert(std::string_view(Actual::E1_Ctti::Name) == std::string_view(Expect::E1_Ctti::Name));
static_assert(std::is_same_v<Actual::E1_Ctti::Type, Expect::E1_Ctti::Type>);
static_assert(Actual::E1_Ctti::NumEnumerators == Expect::E1_Ctti::NumEnumerators);
static_assert(Actual::E1_Ctti::Enumerators[0].Constant	== Expect::E1_Ctti::Enumerators[0].Constant);
static_assert(Actual::E1_Ctti::Enumerators[1].Constant	== Expect::E1_Ctti::Enumerators[1].Constant);
static_assert(std::string_view(Actual::E1_Ctti::Enumerators[0].Name) == std::string_view(Expect::E1_Ctti::Enumerators[0].Name));
static_assert(std::string_view(Actual::E1_Ctti::Enumerators[1].Name) == std::string_view(Expect::E1_Ctti::Enumerators[1].Name));

//////////////////////////////////////////////////////////////////////////

struct S1
{
	float x;
	int y;
};

namespace Actual
{	
	PP_REFLECT_STRUCT(PlainProps::Test, S1, void, x, y);
}

namespace Expect
{
	struct S1_Ctti
	{
		static constexpr char Name[] = "S1";
		using Type = ::PlainProps::Test::S1;
		using Super = void;
		static constexpr int NumVars = 2;
		template<int> struct Var;
	};

	template<> struct S1_Ctti::Var<2-2>
	{
		static constexpr char Name[] = "x";
		using Type = decltype(::PlainProps::Test::S1::x);
		static constexpr auto Pointer = &::PlainProps::Test::S1::x;
		static constexpr std::size_t Offset = offsetof(::PlainProps::Test::S1, x);
		static constexpr int Index = 2-2;
	};

	template<> struct S1_Ctti::Var<2-1>
	{
		static constexpr char Name[] = "y";
		using Type = decltype(::PlainProps::Test::S1::y);
		static constexpr auto Pointer = &::PlainProps::Test::S1::y;
		static constexpr std::size_t Offset = offsetof(::PlainProps::Test::S1, y);
		static constexpr int Index = 2-1;
	};
}

template<class Actual, class Expect>
static constexpr bool AssertVarEquivalence()
{
	static_assert(std::string_view(Actual::Name) == std::string_view(Expect::Name));
	static_assert(std::is_same_v<typename Actual::Type, typename Expect::Type>);
	static_assert(Actual::Offset == Expect::Offset);
	static_assert(Actual::Pointer == Expect::Pointer);
	static_assert(Actual::Index == Expect::Index);
	return true;
}

static_assert(std::string_view(Actual::S1_Ctti::Name) == std::string_view(Expect::S1_Ctti::Name));
static_assert(std::is_same_v<Actual::S1_Ctti::Type, Expect::S1_Ctti::Type>);
static_assert(std::is_same_v<Actual::S1_Ctti::Super, Expect::S1_Ctti::Super>);
static_assert(Actual::S1_Ctti::NumVars == Expect::S1_Ctti::NumVars);
static_assert(AssertVarEquivalence<Actual::S1_Ctti::Var<0>, Expect::S1_Ctti::Var<0>>());
static_assert(AssertVarEquivalence<Actual::S1_Ctti::Var<1>, Expect::S1_Ctti::Var<1>>());

// CttiOf only works when CTTI exists in same or parent namespace, it uses argument dependent lookup
// to find the "canonical" CTTI if exists. Regenerate S1_Ctti in S1's namespace to test it.
PP_REFLECT_STRUCT(PlainProps::Test, S1, void, x, y);
static_assert(std::is_same_v<CttiOf<S1>, S1_Ctti>);

//////////////////////////////////////////////////////////////////////////

template<class T>
struct S2
{
	bool _; // unreflected
	T a;
};

PP_REFLECT_STRUCT_TEMPLATE(PlainProps::Test, S2, void, a);

static_assert(std::string_view(CttiOf<S2<int>>::Name) == std::string_view("S2"));
static_assert(std::is_same_v<CttiOf<S2<int>>::Type, S2<int>>);
static_assert(std::is_same_v<CttiOf<S2<int>>::TemplateArgs, std::tuple<int>>);
static_assert(CttiOf<S2<int>>::NumVars == 1);
static_assert(CttiOf<S2<int>>::Var<0>::Name == std::string_view("a"));
static_assert(CttiOf<S2<int>>::Var<0>::Offset == offsetof(S2<int>, a));

}
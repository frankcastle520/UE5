// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/AssertionMacros.h"

namespace UE::Private
{

template <typename TypeTraits>
class TInstrusiveListIterator
{
public:
	using ElementType = typename TypeTraits::ElementType;

	explicit TInstrusiveListIterator(ElementType* InElement)
		: Element(InElement)
	{ }

	ElementType&	operator*() const	{ check(Element); return *Element; }
	explicit		operator bool()		{ return Element != nullptr; }
	void			operator++()		{ check(Element); Element = TypeTraits::GetNext(Element); }
	bool			operator!=(const TInstrusiveListIterator& Other) const { return Element != Other.Element; }

private:
	ElementType* Element;
};

} // namespace UE::Private

template <typename TypeTraits>
class TInstrusiveList
{
public:
	using ElementType		= typename TypeTraits::ElementType;
	using FIterator			= UE::Private::TInstrusiveListIterator<TypeTraits>;
	using FConstIterator	= UE::Private::TInstrusiveListIterator<const TypeTraits>;

	TInstrusiveList() = default;
	TInstrusiveList(const TInstrusiveList&) = delete;
	TInstrusiveList(TInstrusiveList&& Other)
		: Head(Other.Head)
		, Tail(Other.Tail)
	{
		Other.Head = Other.Tail = nullptr;
	}

	TInstrusiveList& operator=(const TInstrusiveList&) = delete;
	TInstrusiveList& operator=(TInstrusiveList&& Other)
	{
		Head = Other.Head;
		Tail = Other.Tail;
		Other.Head = Other.Tail = nullptr;
		return *this;
	}

	void AddTail(ElementType* Element)
	{
		check(Element != nullptr && TypeTraits::GetNext(Element) == nullptr);

		if (Tail != nullptr)
		{
			check(Head != nullptr);
			TypeTraits::SetNext(Tail, Element);
			Tail = Element;
		}
		else
		{
			check(Head == nullptr);
			Head = Tail = Element;
		}
	}

	void AddTail(ElementType* First, ElementType* Last)
	{
		check(First && Last);
		check(TypeTraits::GetNext(First) != nullptr || First == Last);

		if (Tail != nullptr)
		{
			check(Head != nullptr);
			TypeTraits::SetNext(Tail, First);
			Tail = Last;
		}
		else
		{
			check(Head == nullptr);
			Head = First;
			Tail = Last;
		}
	}

	void AddTail(TInstrusiveList&& Other)
	{
		if (!Other.IsEmpty())
		{
			AddTail(Other.Head, Other.Tail);
			Other.Head = Other.Tail = nullptr;
		}
	}

	ElementType* PopHead()
	{
		ElementType* Element = Head;
		if (Element != nullptr)
		{
			Head = TypeTraits::GetNext(Element);
			if (Head == nullptr)
			{
				Tail = nullptr;
			}
			TypeTraits::SetNext(Element, nullptr);
		}

		return Element;
	}

	bool				IsEmpty() const { return Head == nullptr; }
	ElementType*		GetHead()		{ return Head; }
	const ElementType*	GetHead() const { return Head; }
	ElementType*		GetTail()		{ return Tail; }
	const ElementType*	GetTail() const { return Tail; }

	FIterator			begin()			{ return FIterator(Head); }
	FConstIterator		begin() const	{ return FConstIterator(Head); }
	FIterator			end()			{ return FIterator(nullptr); }
	FConstIterator		end() const		{ return FConstIterator(nullptr); }

private:
	ElementType* Head = nullptr;
	ElementType* Tail = nullptr;
};

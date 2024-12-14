// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMWriteBarrier.h"

namespace Verse
{
template <typename DerivedType>
struct TIntrusiveTree
{
	TWriteBarrier<DerivedType> Parent;
	TWriteBarrier<DerivedType> LastChild;
	TWriteBarrier<DerivedType> Prev;
	TWriteBarrier<DerivedType> Next;

	TIntrusiveTree(FAccessContext Context, DerivedType* Parent)
		: Parent(Context, Parent)
	{
		DerivedType* This = static_cast<DerivedType*>(this);

		if (Parent)
		{
			if (Parent->LastChild)
			{
				Prev.Set(Context, Parent->LastChild.Get());
				Parent->LastChild->Next.Set(Context, This);
			}
			Parent->LastChild.Set(Context, This);
		}
	}

	void Detach(FAccessContext Context)
	{
		DerivedType* This = static_cast<DerivedType*>(this);

		if (Parent && Parent->LastChild.Get() == This)
		{
			V_DIE_IF(Next);
			Parent->LastChild.Set(Context, Prev.Get());
		}

		if (Prev)
		{
			V_DIE_UNLESS(Prev->Next.Get() == This);
			Prev->Next.Set(Context, Next.Get());
		}
		if (Next)
		{
			V_DIE_UNLESS(Next->Prev.Get() == This);
			Next->Prev.Set(Context, Prev.Get());
		}

		Prev.Reset();
		Next.Reset();
	}

	// Visit each element of the subtree rooted at `this`.
	template <typename FunctionType>
	void ForEach(FunctionType&& Function)
	{
		if (LIKELY(!LastChild.Get()))
		{
			Function(static_cast<DerivedType&>(*this));
			return;
		}

		TArray<TIntrusiveTree*> ToVisit;
		ToVisit.Push(this);
		while (ToVisit.Num())
		{
			TIntrusiveTree* Task = ToVisit.Pop();
			Function(static_cast<DerivedType&>(*Task));
			for (TIntrusiveTree* Child = Task->LastChild.Get(); Child; Child = Child->Prev.Get())
			{
				ToVisit.Push(Child);
			}
		}
	}

	template <typename TVisitor>
	void VisitReferencesImpl(TVisitor& Visitor)
	{
		Visitor.Visit(Parent, TEXT("Parent"));
		Visitor.Visit(LastChild, TEXT("LastChild"));
		Visitor.Visit(Prev, TEXT("Prev"));
		Visitor.Visit(Next, TEXT("Next"));
	}
};
} // namespace Verse

#endif

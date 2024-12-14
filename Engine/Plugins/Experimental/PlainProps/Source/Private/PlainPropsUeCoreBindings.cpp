// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlainPropsUeCoreBindings.h"
#include "PlainPropsBuild.h"
#include "PlainPropsSave.h"
#include "Math/Transform.h"

namespace PlainProps::UE
{

void FTransformBinding::Save(FMemberBuilder& Dst, const FTransform& Src, const FTransform* Default, const FSaveContext& Ctx) const
{
	static_assert(std::is_same_v<decltype(FTransform().GetTranslation().X), double>);
	
	const FStructDeclaration& VectorDecl = Ctx.Declarations.Get(VectorId);
	const FStructDeclaration& QuatDecl = Ctx.Declarations.Get(QuatId);
	FDenseMemberBuilder Inner = { Ctx.Scratch, Ctx.Declarations.GetDebug() };

	FVector T = Src.GetTranslation();
	FQuat R = Src.GetRotation();
	FVector S = Src.GetScale3D();

	if (Default)
	{
		if (T != Default->GetTranslation())
		{
			Dst.AddStruct(MemberIds[(uint8)EMember::Translate], VectorId, Inner.BuildHomogeneous(VectorDecl, T.X, T.Y, T.Z));
		}

		if (R != Default->GetRotation())
		{
			Dst.AddStruct(MemberIds[(uint8)EMember::Rotate], QuatId, Inner.BuildHomogeneous(QuatDecl, R.X, R.Y, R.Z, R.W));
		}

		if (S != Default->GetScale3D())
		{
			Dst.AddStruct(MemberIds[(uint8)EMember::Scale], VectorId, Inner.BuildHomogeneous(VectorDecl, S.X, S.Y, S.Z));
		}
	}
	else
	{	
		Dst.AddStruct(MemberIds[(uint8)EMember::Translate], VectorId, Inner.BuildHomogeneous(VectorDecl, T.X, T.Y, T.Z));		
		Dst.AddStruct(MemberIds[(uint8)EMember::Rotate], QuatId, Inner.BuildHomogeneous(QuatDecl, R.X, R.Y, R.Z, R.W));
		Dst.AddStruct(MemberIds[(uint8)EMember::Scale], VectorId, Inner.BuildHomogeneous(VectorDecl, S.X, S.Y, S.Z));
	}
}

template<typename T>
T GrabAndMemcpy(FMemberReader& Members)
{
	T Out;
	FStructView Struct = Members.GrabStruct();
	Struct.Values.CheckSize(sizeof(T));
	FMemory::Memcpy(&Out, Struct.Values.Peek(), sizeof(T));
	return Out;
}

void FTransformBinding::Load(FTransform& Dst, FStructView Src, ECustomLoadMethod Method, const FLoadBatch& Batch) const
{
	static_assert(std::is_same_v<decltype(FTransform().GetTranslation().X), double>);

	FMemberReader Members(Src);

	if (Method == ECustomLoadMethod::Construct)
	{
		::new (&Dst) FTransform;
	}
				
	if (!Members.HasMore())
	{
		return;
	}

	if (Members.PeekNameUnchecked() == MemberIds[(uint8)EMember::Translate])
	{
		Dst.SetTranslation(GrabAndMemcpy<FVector>(Members));

		if (!Members.HasMore())
		{
			return;
		}
	}

	if (Members.PeekNameUnchecked() == MemberIds[(uint8)EMember::Rotate])
	{
		Dst.SetRotation(GrabAndMemcpy<FQuat>(Members));

		if (!Members.HasMore())
		{
			return;
		}
	}

	checkSlow(Members.PeekNameUnchecked() == MemberIds[(uint8)EMember::Scale]);
	Dst.SetScale3D(GrabAndMemcpy<FVector>(Members));
	checkSlow(!Members.HasMore());
}

} // namespace PlainProps::UE

//////////////////////////////////////////////////////////////////////////
namespace PlainProps
{
	
template<>
void AppendString(FString& Out, const FName& Name)
{
	Name.AppendString(Out);
}

}
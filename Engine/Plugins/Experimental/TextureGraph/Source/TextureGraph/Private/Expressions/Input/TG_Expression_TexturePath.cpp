// Copyright Epic Games, Inc. All Rights Reserved.

#include "Expressions/Input/TG_Expression_TexturePath.h"

#include "TG_Graph.h"
#include "2D/TextureHelper.h"
#include "Model/StaticImageResource.h"

// Special case for TexturePath Constant signature, we want to keep the Path Input connectable in that case
// so do this in the override version of BuildInputConstantSignature()
FTG_SignaturePtr  UTG_Expression_TexturePath::BuildInputConstantSignature() const
{
	FTG_Signature::FInit SignatureInit = GetSignatureInitArgsFromClass();
	for (auto& Arg : SignatureInit.Arguments)
	{
		if (Arg.IsInput() && Arg.IsParam())
		{
			Arg.ArgumentType = Arg.ArgumentType.Unparamed();
		}
	}
	return MakeShared<FTG_Signature>(SignatureInit);
}

bool UTG_Expression_TexturePath::ValidateInputPath(FString& ValidatedPath) const
{
	// empty but that's ok
	if (Path.IsEmpty())
	{
		return true;
	}

	// Check that the local path exists
	FString LocalPath = Path.TrimQuotes();
	FPackagePath PackagePath;
	FString PathExt = FPaths::GetExtension(Path);

	// Try to find a file in a mounted package
	if (FPackagePath::TryFromMountedName(LocalPath, PackagePath))
	{
		LocalPath = PackagePath.GetLocalFullPath();
		FString LocalPathExt = FPaths::GetExtension(LocalPath);

		if (LocalPathExt != PathExt)
		{
			LocalPath = FPaths::ChangeExtension(LocalPath, PathExt);
		}
		ValidatedPath = LocalPath;
		return true;
	}
	else if(FPaths::FileExists(LocalPath))
	{
		ValidatedPath = LocalPath;
		return true;
	}

	return false;
}

void UTG_Expression_TexturePath::Evaluate(FTG_EvaluationContext* InContext)
{
	Super::Evaluate(InContext);

	FString LocalPath;
	bool bValidation = ValidateInputPath(LocalPath);

	if (!Path.IsEmpty() && bValidation)
	{
		UStaticImageResource* StaticImageResource = UStaticImageResource::CreateNew<UStaticImageResource>();
		StaticImageResource->SetAssetUUID(LocalPath);
		StaticImageResource->SetIsFileSystem(true);

		//Until we have srgb value exposed in the UI we need to set the Srgb of the Output Descriptor here from the source
		//This gets updated for the late bond case but since we do not have the UI to specify the override in other nodes 
		// the override value will always be set to false while combining the buffers
		auto DesiredDesc = Output.GetBufferDescriptor();
		DesiredDesc.bIsSRGB = true;

		Output = StaticImageResource->GetBlob(InContext->Cycle, DesiredDesc, 0);
	}
	else
	{
		Output = FTG_Texture::GetBlack();
	}

	//For the connected pin we will report error here in eveluate because it does not have the updated value during validation.
	UTG_Pin* PathPin = GetParentNode()->GetPin(GET_MEMBER_NAME_CHECKED(UTG_Expression_TexturePath, Path));

	if (PathPin->IsConnected() && !bValidation)
	{
		ReportError(InContext->Cycle);
	}
}

bool UTG_Expression_TexturePath::Validate(MixUpdateCyclePtr Cycle)
{
	FString LocalPath;
	UTG_Pin* PathPin = GetParentNode()->GetPin(GET_MEMBER_NAME_CHECKED(UTG_Expression_TexturePath, Path));

	if (!PathPin->IsConnected() && !ValidateInputPath(LocalPath))
	{
		ReportError(Cycle);
		return true;
	}

	return true;
}

void UTG_Expression_TexturePath::ReportError(MixUpdateCyclePtr Cycle)
{
	auto ErrorType = static_cast<int32>(ETextureGraphErrorType::NODE_WARNING);
	TextureGraphEngine::GetErrorReporter(Cycle->GetMix())->ReportWarning(ErrorType, FString::Printf(TEXT("Input Path <%s> is not a valid local path"), *Path), GetParentNode());
}

void UTG_Expression_TexturePath::SetTitleName(FName NewName)
{
	GetParentNode()->GetPin(GET_MEMBER_NAME_CHECKED(UTG_Expression_TexturePath, Path))->SetAliasName(NewName);
}

FName UTG_Expression_TexturePath::GetTitleName() const
{
	return GetParentNode()->GetPin(GET_MEMBER_NAME_CHECKED(UTG_Expression_TexturePath, Path))->GetAliasName();
}


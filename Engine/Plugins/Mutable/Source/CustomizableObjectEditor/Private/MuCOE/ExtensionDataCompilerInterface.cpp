// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/ExtensionDataCompilerInterface.h"

#include "StructUtils/InstancedStruct.h"
#include "MuCO/CustomizableObjectStreamedResourceData.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSource.h"
#include "MuCOE/CustomizableObjectCompiler.h"
#include "MuR/ExtensionData.h"

FExtensionDataCompilerInterface::FExtensionDataCompilerInterface(FMutableGraphGenerationContext& InGenerationContext)
	: GenerationContext(InGenerationContext)
{
}

mu::ExtensionDataPtrConst FExtensionDataCompilerInterface::MakeStreamedExtensionData(UCustomizableObjectResourceDataContainer*& OutContainer)
{
	mu::ExtensionDataPtr Result = new mu::ExtensionData;
	Result->Origin = mu::ExtensionData::EOrigin::ConstantStreamed;
	Result->Index = GenerationContext.StreamedExtensionData.Num();
	
	if (!GenerationContext.bParticipatingObjectsPass)
	{
		// Generate a deterministic name to help with deterministic cooking
		const FString ContainerName = FString::Printf(TEXT("Streamed_%d"), Result->Index);

		OutContainer = NewObject<UCustomizableObjectResourceDataContainer>(
			GetTransientPackage(),
			FName(*ContainerName),
			RF_Public);

		GenerationContext.StreamedExtensionData.Emplace(ContainerName, OutContainer);
	}
	
	return Result;
}

mu::ExtensionDataPtrConst FExtensionDataCompilerInterface::MakeAlwaysLoadedExtensionData(FInstancedStruct&& Data)
{
	mu::ExtensionDataPtr Result = new mu::ExtensionData;
	Result->Origin = mu::ExtensionData::EOrigin::ConstantAlwaysLoaded;
	Result->Index = GenerationContext.AlwaysLoadedExtensionData.Num();

	FCustomizableObjectResourceData* CompileTimeExtensionData = &GenerationContext.AlwaysLoadedExtensionData.AddDefaulted_GetRef();
	CompileTimeExtensionData->Data = MoveTemp(Data);

	return Result;
}

const UObject* FExtensionDataCompilerInterface::GetOuterForAlwaysLoadedObjects()
{
	check(GenerationContext.Object);
	return GenerationContext.Object;
}

void FExtensionDataCompilerInterface::AddGeneratedNode(const UCustomizableObjectNode* InNode)
{
	check(InNode);

	// A const_cast here is required because the new node needs to be added in the GeneratedNodes list so mutable can
	// discover new parameters that can potentially be attached to the extension node, however, this
	// function is called as ICustomizableObjectExtensionNode::GenerateMutableNode(this), so we need to cast the const away here.
	// Decided to do the case here so the use of AddGeneratedNode is as clean as possible
	GenerationContext.GeneratedNodes.Add(const_cast<UCustomizableObjectNode*>(InNode));
}

void FExtensionDataCompilerInterface::CompilerLog(const FText& InLogText, const UCustomizableObjectNode* InNode)
{
	GenerationContext.Log(InLogText, InNode);
}

void FExtensionDataCompilerInterface::AddParticipatingObject(const UObject& Object)
{
	GenerationContext.AddParticipatingObject(Object);
}


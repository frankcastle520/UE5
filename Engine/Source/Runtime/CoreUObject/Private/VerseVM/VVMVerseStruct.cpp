// Copyright Epic Games, Inc. All Rights Reserved.

#include "VerseVM/VVMVerseStruct.h"

#include "Logging/LogMacros.h"
#include "Templates/TypeHash.h"
#include "UObject/ObjectSaveContext.h"
#include "VerseVM/VVMEngineEnvironment.h"
#include "VerseVM/VVMExecutionContext.h"
#include "VerseVM/VVMVerse.h"
#include "VerseVM/VVMVerseClass.h"

#if WITH_EDITOR
#include "UObject/CookedMetaData.h"
#endif

void UVerseStruct::Link(FArchive& Ar, bool bRelinkExistingProperties)
{
	Super::Link(Ar, bRelinkExistingProperties);

	// Only do this for classes we're loading from disk/file -- in-memory generated ones
	// have these functions executed for them via Verse::FUObjectGenerator or FVerseVMAssembler
	if (HasAnyFlags(RF_WasLoaded))
	{
		// For native classes, we need to bind them explicitly here -- we need to do it
		// after Super::Link() (so it can find named properties/functions), but before
		// CDO creation (since binding can affect property offsets and class size).
		if ((VerseClassFlags & VCLASS_NativeBound) != EVerseClassFlags::VCLASS_None)
		{
			Verse::IEngineEnvironment* Environment = Verse::VerseVM::GetEngineEnvironment();
			ensure(Environment);
			Environment->TryBindVniStruct(this);
		}
	}
}

void UVerseStruct::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Super::PreSave(ObjectSaveContext);

#if WITH_EDITOR
	// Note: We do this in PreSave rather than PreSaveRoot since Verse stores multiple generated types in the same package, and PreSaveRoot is only called for the main "asset" within each package
	if (ObjectSaveContext.IsCooking() && (ObjectSaveContext.GetSaveFlags() & SAVE_Optional))
	{
		if (!CachedCookedMetaDataPtr)
		{
			CachedCookedMetaDataPtr = CookedMetaDataUtil::NewCookedMetaData<UStructCookedMetaData>(this, "CookedStructMetaData");
		}

		CachedCookedMetaDataPtr->CacheMetaData(this);

		if (!CachedCookedMetaDataPtr->HasMetaData())
		{
			CookedMetaDataUtil::PurgeCookedMetaData<UStructCookedMetaData>(CachedCookedMetaDataPtr);
		}
	}
	else if (CachedCookedMetaDataPtr)
	{
		CookedMetaDataUtil::PurgeCookedMetaData<UStructCookedMetaData>(CachedCookedMetaDataPtr);
	}
#endif
}

uint32 UVerseStruct::GetStructTypeHash(const void* Src) const
{
	// If this is a C++ struct, call the C++ GetTypeHash function.
	if (UScriptStruct::ICppStructOps* TheCppStructOps = GetCppStructOps())
	{
		if (ensureMsgf(TheCppStructOps->HasGetTypeHash(), TEXT("Expected comparable C++/Verse struct %s to have C++ GetTypeHash function defined"), *GetName()))
		{
			return TheCppStructOps->GetStructTypeHash(Src);
		}
	}

	// Hash each field of the struct, and use HashCombineFast to reduce those hashes to a single hash for the whole struct.
	uint32 CumulativeHash = 0;
	for (TFieldIterator<FProperty> PropertyIt(this); PropertyIt; ++PropertyIt)
	{
		for (int32 ArrayIndex = 0; ArrayIndex < PropertyIt->ArrayDim; ArrayIndex++)
		{
			const uint32 PropertyHash = PropertyIt->GetValueTypeHash(PropertyIt->ContainerPtrToValuePtr<uint8>(Src, ArrayIndex));
			CumulativeHash = HashCombineFast(CumulativeHash, PropertyHash);
		}
	}
	return CumulativeHash;
}

FString UVerseStruct::GetAuthoredNameForField(const FField* Field) const
{
#if WITH_EDITORONLY_DATA

	if (Field)
	{
		static const FName NAME_DisplayName(TEXT("DisplayName"));
		if (const FString* NativeDisplayName = Field->FindMetaData(NAME_DisplayName))
		{
			return *NativeDisplayName;
		}
	}

#endif

	return Super::GetAuthoredNameForField(Field);
}

void UVerseStruct::InvokeDefaultFactoryFunction(uint8* InStructData) const
{
	if (!verse::FExecutionContext::IsExecutionBlocked())
	{
		if (FactoryFunction && ModuleClass)
		{
			ModuleClass->ProcessEvent(FactoryFunction, InStructData);
		}
	}
}

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
void UVerseStruct::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);
	UVerseStruct* This = static_cast<UVerseStruct*>(InThis);
	Collector.AddReferencedVerseValue(This->EmergentType);
}
#endif

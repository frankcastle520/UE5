// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UObjectThreadContext.cpp: Unreal object globals
=============================================================================*/

#include "UObject/UObjectThreadContext.h"
#include "UObject/Object.h"
#include "UObject/LinkerLoad.h"

DEFINE_LOG_CATEGORY(LogUObjectThreadContext);

FUObjectThreadContext::FUObjectThreadContext()
: IsRoutingPostLoad(false)
, IsDeletingLinkers(false)
, SyncLoadUsingAsyncLoaderCount(0)
, IsInConstructor(0)
, ConstructedObject(nullptr)
, CurrentlyPostLoadedObjectByALT(nullptr)
, AsyncPackage(nullptr)
, AsyncPackageLoader(nullptr)
, SerializeContext(new FUObjectSerializeContext())
{}

PRAGMA_DISABLE_DEPRECATION_WARNINGS;
FUObjectThreadContext::~FUObjectThreadContext()
{
#if WITH_EDITORONLY_DATA
	// Remove PRAGMA_DISABLE_DEPRECATION_WARNINGS when PackagesMarkedEditorOnlyByOtherPackage is deleted
	(void)PackagesMarkedEditorOnlyByOtherPackage;
#endif
}
#if WITH_EDITORONLY_DATA
FUObjectThreadContext::FUObjectThreadContext(const FUObjectThreadContext& Other) = default;
FUObjectThreadContext::FUObjectThreadContext(FUObjectThreadContext&& Other) = default;
#endif
PRAGMA_ENABLE_DEPRECATION_WARNINGS;

FObjectInitializer& FUObjectThreadContext::ReportNull()
{
	FObjectInitializer* ObjectInitializerPtr = TopInitializer();
	UE_CLOG(!ObjectInitializerPtr, LogUObjectThreadContext, Fatal, TEXT("Tried to get the current ObjectInitializer, but none is set. Please use NewObject to construct new UObject-derived classes."));
	return *ObjectInitializerPtr;
}

FUObjectSerializeContext::FUObjectSerializeContext()
	: RefCount(0)
	, ImportCount(0)
	, ForcedExportCount(0)
	, ObjBeginLoadCount(0)
	, SerializedObject(nullptr)
	, SerializedPackageLinker(nullptr)
	, SerializedImportIndex(0)
	, SerializedImportLinker(nullptr)
	, SerializedExportIndex(0)
	, SerializedExportLinker(nullptr)
#if WITH_EDITORONLY_DATA
	, bTrackSerializedPropertyPath(false)
	, bTrackInitializedProperties(false)
	, bTrackSerializedProperties(false)
	, bTrackUnknownProperties(false)
	, bTrackUnknownEnumNames(false)
	, bImpersonateProperties(false)
#endif
{
}

FUObjectSerializeContext::~FUObjectSerializeContext()
{
	checkf(!HasLoadedObjects(), TEXT("FUObjectSerializeContext is being destroyed but it still has pending loaded objects in its ObjectsLoaded list."));
}

int32 FUObjectSerializeContext::IncrementBeginLoadCount()
{
	return ++ObjBeginLoadCount;
}
int32 FUObjectSerializeContext::DecrementBeginLoadCount()
{
	check(HasStartedLoading());
	return --ObjBeginLoadCount;
}

void FUObjectSerializeContext::AddUniqueLoadedObjects(const TArray<UObject*>& InObjects)
{
	for (UObject* NewLoadedObject : InObjects)
	{
		ObjectsLoaded.AddUnique(NewLoadedObject);
	}
	
}

void FUObjectSerializeContext::AddLoadedObject(UObject* InObject)
{
	ObjectsLoaded.Add(InObject);
}

bool FUObjectSerializeContext::PRIVATE_PatchNewObjectIntoExport(UObject* OldObject, UObject* NewObject)
{
	const int32 ObjLoadedIdx = ObjectsLoaded.Find(OldObject);
	if (ObjLoadedIdx != INDEX_NONE)
	{
		ObjectsLoaded[ObjLoadedIdx] = NewObject;
		return true;
	}
	else
	{
		return false;
	}
}
void FUObjectSerializeContext::AttachLinker(FLinkerLoad* InLinker)
{
	check(!GEventDrivenLoaderEnabled);
}

void FUObjectSerializeContext::DetachLinker(FLinkerLoad* InLinker)
{
}

void FUObjectSerializeContext::DetachFromLinkers()
{
	check(!GEventDrivenLoaderEnabled);
}

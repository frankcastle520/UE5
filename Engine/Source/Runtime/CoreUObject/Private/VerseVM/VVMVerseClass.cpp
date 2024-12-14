// Copyright Epic Games, Inc. All Rights Reserved.

#include "VerseVM/VVMVerseClass.h"
#include "AutoRTFM/AutoRTFM.h"
#include "Containers/VersePath.h"
#include "Logging/LogMacros.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/PropertyOptional.h"
#include "UObject/UObjectThreadContext.h"
#include "UObject/UnrealType.h"
#include "VerseVM/VVMEngineEnvironment.h"
#include "VerseVM/VVMNames.h"
#include "VerseVM/VVMVerse.h"
#include "VerseVM/VVMVerseStruct.h"

#if WITH_EDITOR
#include "Interfaces/ITargetPlatform.h"
#include "UObject/CookedMetaData.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(VVMVerseClass)

DEFINE_LOG_CATEGORY_STATIC(LogSolGeneratedClass, Log, All);

const FName UVerseClass::InitCDOFunctionName(TEXT("$InitCDO"));
const FName UVerseClass::StructPaddingDummyName(TEXT("$StructPaddingDummy"));
UVerseClass::FOnPropertyRemoved UVerseClass::OnPropertyRemoved;

UE::Core::FVersePath UVerseClass::GetVersePath() const
{
	if (MangledPackageVersePath.IsNone())
	{
		return {};
	}

	FString PackageVersePath = Verse::Names::Private::UnmangleCasedName(MangledPackageVersePath);
	FString VersePath = PackageRelativeVersePath.IsEmpty() ? PackageVersePath : PackageVersePath / PackageRelativeVersePath;
	UE::Core::FVersePath Result;
	ensure(UE::Core::FVersePath::TryMake(Result, MoveTemp(VersePath)));
	return Result;
}

void UVerseClass::Link(FArchive& Ar, bool bRelinkExistingProperties)
{
	Super::Link(Ar, bRelinkExistingProperties);

	// Properties which represent native C++ members need to be removed from the
	// destruct chain, as they will be destructed by the native C++ destructor.
	bool bPropertiesChanged = false;

	UEProperty_Private::FPropertyListBuilderDestructorLink DestructorLinkBuilder(&DestructorLink);
	for (FProperty* Prop = DestructorLinkBuilder.GetListStart(); Prop;)
	{
		FProperty* NextProp = DestructorLinkBuilder.GetNext(*Prop);

		const UVerseClass* SolOwnerClass = Cast<UVerseClass>(Prop->GetOwnerClass());
		if (SolOwnerClass && (SolOwnerClass->SolClassFlags & VCLASS_NativeBound) != EVerseClassFlags::VCLASS_None)
		{
			// property should be removed from linked list
			DestructorLinkBuilder.Remove(*Prop);
			bPropertiesChanged = true;
		}

		Prop = NextProp;
	}

	// Only do this for classes we're loading from disk/file -- in-memory generated ones
	// have these functions executed for them via Verse::FUObjectGenerator or FVerseVMAssembler
	if (HasAnyFlags(RF_WasLoaded))
	{
#if WITH_VERSE_BPVM
		// Make sure coroutine task classes have been loaded at this point
		if (!IsEventDrivenLoaderEnabled())
		{
			for (UVerseClass* TaskClass : TaskClasses)
			{
				if (TaskClass)
				{
					Ar.Preload(TaskClass);
				}
			}
		}
#endif

		// For native classes, we need to bind them explicitly here -- we need to do it
		// after Super::Link() (so it can find named properties/functions), but before
		// CDO creation (since binding can affect property offsets and class size).
		if ((SolClassFlags & VCLASS_NativeBound) != EVerseClassFlags::VCLASS_None)
		{
			Verse::IEngineEnvironment* Environment = Verse::VerseVM::GetEngineEnvironment();
			ensure(Environment);
			Environment->TryBindVniStruct(this);
		}

#if WITH_VERSE_BPVM
		// Connect native function thunks of loaded classes
		for (const FNativeFunctionLookup& NativeFunctionLookup : NativeFunctionLookupTable)
		{
			UFunction* Function = FindFunctionByName(NativeFunctionLookup.Name);
			if (ensure(Function))
			{
				Function->SetNativeFunc(NativeFunctionLookup.Pointer);
				Function->FunctionFlags |= FUNC_Native;
			}
		}
#endif
	}

#if WITH_VERSE_BPVM
	// Manually build token stream for Solaris classes but only when linking cooked classes or
	// when linking a duplicated class during class reinstancing.
	// However, when classes are first created (from script source) this happens in
	// FAssembleClassOrStructTask as we want to make sure all dependencies are properly set up first
	if (HasAnyFlags(RF_WasLoaded) || HasAnyClassFlags(CLASS_NewerVersionExists))
	{
		AssembleReferenceTokenStream(bPropertiesChanged || bRelinkExistingProperties);
	}
#endif
}

void UVerseClass::PreloadChildren(FArchive& Ar)
{
#if WITH_VERSE_BPVM
	// Preloading functions for UVerseClass may end up with circular dependencies regardless of EDL being enabled or not
	// Since UVerseClass is not a UBlueprintGeneratedClass it does not use the deferred dependency loading path in FLinkerLoad
	// so we don't want to deal with circular dependencies here. They will be resolved by the linker eventually though.
	for (UField* Field = Children; Field; Field = Field->Next)
	{
		if (!Cast<UFunction>(Field))
		{
			Ar.Preload(Field);
		}
	}
#endif
}

FProperty* UVerseClass::CustomFindProperty(const FName InName) const
{
	OnPropertyRemoved.Broadcast(this, InName);
	return nullptr;
}

FString UVerseClass::GetAuthoredNameForField(const FField* Field) const
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

void UVerseClass::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	Super::GetPreloadDependencies(OutDeps);

	// UClass::Serialize() will instantiate this class's CDO, but that means we need the
	// Super's CDO serialized before this class serializes
	OutDeps.Add(GetSuperClass()->GetDefaultObject());

	// For natively-bound classes, we need their coroutine objects serialized first,
	// because we bind on Link() (called during Serialize()) and native binding
	// for a class will binds its coroutine task objects at the same time.
	if ((SolClassFlags & VCLASS_NativeBound) != EVerseClassFlags::VCLASS_None)
	{
		for (UVerseClass* TaskClass : TaskClasses)
		{
			OutDeps.Add(TaskClass);
		}
	}
}

void UVerseClass::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);

#if WITH_EDITOR
	{
		FString NativeParentClassName;
		if (UClass* ParentClass = GetSuperClass())
		{
			// Walk up until we find a native class
			UClass* NativeParentClass = ParentClass;
			while (!NativeParentClass->HasAnyClassFlags(CLASS_Native | CLASS_Intrinsic))
			{
				NativeParentClass = NativeParentClass->GetSuperClass();
			}
			NativeParentClassName = FObjectPropertyBase::GetExportPath(NativeParentClass);
		}
		else
		{
			NativeParentClassName = TEXT("None");
		}

		static const FName NAME_NativeParentClass = "NativeParentClass";
		Context.AddTag(FAssetRegistryTag(NAME_NativeParentClass, MoveTemp(NativeParentClassName), FAssetRegistryTag::TT_Alphabetical));
	}
#endif
}

static bool NeedsPostLoad(UObject* InObj)
{
	return InObj->HasAnyFlags(RF_NeedPostLoad);
}

static bool NeedsInit(UObject* InObj)
{
	if (NeedsPostLoad(InObj))
	{
		return false;
	}
	if (InObj->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		if (NeedsPostLoad(InObj->GetClass()))
		{
			return false;
		}
	}
	return true;
}

void UVerseClass::PostInitInstance(UObject* InObj, FObjectInstancingGraph* InstanceGraph)
{
	Super::PostInitInstance(InObj, InstanceGraph);

	if (NeedsInit(InObj))
	{
		// #jira SOL-6303: What should we do with a failing transaction?
		AutoRTFM::Transact([this, InObj, InstanceGraph] {
			CallInitInstanceFunctions(InObj, InstanceGraph);
		});

		AddSessionVars(InObj);
	}

	AddPersistentVars(InObj);
}

void UVerseClass::PostLoadInstance(UObject* InObj)
{
	Super::PostLoadInstance(InObj);

	if (bNeedsSubobjectInstancingForLoadedInstances && RefLink && !InObj->HasAnyFlags(RF_ClassDefaultObject))
	{
		InstanceNewSubobjects(InObj);
	}

	// #jira SOL-6303: What should we do with a failing transaction?
	AutoRTFM::Transact([this, InObj] {
		CallInitInstanceFunctions(InObj, nullptr);
	});

	AddSessionVars(InObj);
}

#if WITH_EDITORONLY_DATA
bool UVerseClass::CanCreateInstanceDataObject() const
{
	return true;
}
#endif

#if WITH_EDITOR
FTopLevelAssetPath UVerseClass::GetReinstancedClassPathName_Impl() const
{
#if WITH_VERSE_COMPILER
	return FTopLevelAssetPath(PreviousPathName);
#else
	return nullptr;
#endif
}
#endif

const TCHAR* UVerseClass::GetPrefixCPP() const
{
	return TEXT("");
}

void UVerseClass::AddPersistentVars(UObject* InObj)
{
	Verse::IEngineEnvironment* Environment = Verse::VerseVM::GetEngineEnvironment();
	ensure(Environment);
	Environment->AddPersistentVars(InObj, PersistentVars);
}

void UVerseClass::AddSessionVars(UObject* InObj)
{
	Verse::IEngineEnvironment* Environment = Verse::VerseVM::GetEngineEnvironment();
	ensure(Environment);
	Environment->AddSessionVars(InObj, SessionVars);
}

void UVerseClass::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	Super::PreSave(ObjectSaveContext);

#if WITH_EDITOR
	// Hack: if cooking for clients, clear the InitInstanceFunction to make sure clients don't try to run it.
	if (ObjectSaveContext.IsCooking()
		&& ensure(ObjectSaveContext.GetTargetPlatform())
		&& !ObjectSaveContext.GetTargetPlatform()->IsServerOnly())
	{
		InitInstanceFunction = nullptr;
	}

	// Note: We do this in PreSave rather than PreSaveRoot since Verse stores multiple generated types in the same package, and PreSaveRoot is only called for the main "asset" within each package
	if (ObjectSaveContext.IsCooking() && (ObjectSaveContext.GetSaveFlags() & SAVE_Optional))
	{
		if (!CachedCookedMetaDataPtr)
		{
			CachedCookedMetaDataPtr = CookedMetaDataUtil::NewCookedMetaData<UClassCookedMetaData>(this, "CookedClassMetaData");
		}

		CachedCookedMetaDataPtr->CacheMetaData(this);

		if (!CachedCookedMetaDataPtr->HasMetaData())
		{
			CookedMetaDataUtil::PurgeCookedMetaData<UClassCookedMetaData>(CachedCookedMetaDataPtr);
		}
	}
	else if (CachedCookedMetaDataPtr)
	{
		CookedMetaDataUtil::PurgeCookedMetaData<UClassCookedMetaData>(CachedCookedMetaDataPtr);
	}
#endif
}

void UVerseClass::CallInitInstanceFunctions(UObject* InObj, FObjectInstancingGraph* InstanceGraph)
{
#if WITH_EDITOR
	InObj->SetFlags(RF_Transactional);
#endif

	if (InObj->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		// The construction of the CDO should not invoke class blocks.
		return;
	}
	if (InstanceGraph && InObj == InstanceGraph->GetDestinationRoot())
	{
		// The root's class blocks will be invoked by the archetype instantiation.
		return;
	}

	if (GIsClient && !GIsEditor && !WITH_VERSE_COMPILER)
	{
		// SOL-4610: Don't run the InitInstance function on clients.
		return;
	}

	if (InitInstanceFunction)
	{
		// Make sure the function has been loaded and PostLoaded
		checkf(!InitInstanceFunction->HasAnyFlags(RF_NeedLoad), TEXT("Trying to call \"%s\" on \"%s\" but the function has not yet been loaded."), *InitInstanceFunction->GetPathName(), *InObj->GetFullName());
		InitInstanceFunction->ConditionalPostLoad();

		// DANGER ZONE: We're allowing VM code to potentially run during post load so fingers crossed it has no side effects
		TGuardValue<bool> GuardIsRoutingPostLoad(FUObjectThreadContext::Get().IsRoutingPostLoad, false);
		InObj->ProcessEvent(InitInstanceFunction, nullptr);
	}

	CallPropertyInitInstanceFunctions(InObj, InstanceGraph);
}

void UVerseClass::CallPropertyInitInstanceFunctions(UObject* InObj, FObjectInstancingGraph* InstanceGraph)
{
	checkf(!GIsClient || GIsEditor || WITH_VERSE_COMPILER, TEXT("SOL-4610: UEFN clients are not supposed to run Verse code."));

	for (FProperty* Property = (FProperty*)ChildProperties; Property; Property = (FProperty*)Property->Next)
	{
		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			UVerseStruct* SolarisStruct = Cast<UVerseStruct>(StructProperty->Struct);
			if (SolarisStruct && SolarisStruct->InitFunction && SolarisStruct->ModuleClass && (!InstanceGraph || !InstanceGraph->IsPropertyInSubobjectExclusionList(Property)))
			{
				SolarisStruct->ModuleClass->GetDefaultObject()->ProcessEvent(SolarisStruct->InitFunction, StructProperty->ContainerPtrToValuePtr<void>(InObj));
			}
		}
	}
}

void UVerseClass::InstanceNewSubobjects(UObject* InObj)
{
	bool bHasInstancedProperties = false;
	for (FProperty* Property = RefLink; Property != nullptr && !bHasInstancedProperties; Property = Property->NextRef)
	{
		bHasInstancedProperties = Property->ContainsInstancedObjectProperty();
	}

	if (bHasInstancedProperties)
	{
		FObjectInstancingGraph InstancingGraph(EObjectInstancingGraphOptions::InstanceTemplatesOnly);
		UObject* Archetype = GetDefaultObject();

		InstancingGraph.AddNewObject(InObj, Archetype);
		// We call the base class InstanceSubobjectTemplates which tries to instance subobjects on all instanced properties
		// because it should only instance subobject templates and keep already instanced subobjects without changes
		InstanceSubobjectTemplates(InObj, Archetype, nullptr, InObj, &InstancingGraph);
	}
}

namespace VerseClassPrivate
{

void GenerateSubobjectName(FString& OutName, const FString& InPrefix, const FProperty* InProperty, int32 Index)
{
	if (InPrefix.Len())
	{
		OutName = InPrefix;
		OutName += TEXT("_");
	}
	OutName += InProperty->GetName();
	if (Index > 0)
	{
		OutName += FString::Printf(TEXT("_%d"), Index);
	}
}

void RenameSubobject(UObject* Subobject, const FString& InName)
{
	const ERenameFlags RenameFlags = REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional;
	UObject* ExistingSubobject = StaticFindObjectFast(UObject::StaticClass(), Subobject->GetOuter(), *InName, false);
	if (ExistingSubobject && ExistingSubobject != Subobject)
	{
		// ExistingSubobject is an object with the same name and outer as the subobject currently assigned to the property we're traversing
		// The engine does not allow renaming on top of existing objects so we need to rename the old object first
		ExistingSubobject->Rename(*MakeUniqueObjectName(ExistingSubobject->GetOuter(), ExistingSubobject->GetClass()).ToString(), nullptr, RenameFlags);
	}
	Subobject->Rename(*InName, nullptr, RenameFlags);
}

void RenameDefaultSubobjectsInternal(UObject* InArchetype, void* ContainerPtr, UStruct* Struct, const FString& Prefix);

void RenameDefaultSubobjectsInternal(UObject* InArchetype, void* ContainerPtr, FProperty* RefProperty, const FString& Prefix)
{
	{
		UStruct* OwnerStruct = RefProperty ? RefProperty->GetOwner<UStruct>() : nullptr;

		// If the direct owner of RefProperty is not a UStruct then we're traversing an inner property of a property that has already passed this test (FArray/FMap/FSetProperty)
		if (OwnerStruct && !OwnerStruct->IsA<UVerseClass>() && !OwnerStruct->IsA<UVerseStruct>())
		{
			// Skip non-verse properties
			return;
		}
	}

	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(RefProperty))
	{
		// Rename all subobjects referenced by this property (potentially in a C-style array)
		for (int32 ObjectIndex = 0; ObjectIndex < ObjProp->ArrayDim; ++ObjectIndex)
		{
			void* Address = ObjProp->ContainerPtrToValuePtr<void>(ContainerPtr, ObjectIndex);
			UObject* Subobject = ObjProp->GetObjectPropertyValue(Address);
			if (Subobject && Subobject->GetOuter() == InArchetype)
			{
				FString SubobjectName;
				GenerateSubobjectName(SubobjectName, Prefix, ObjProp, ObjectIndex);
				RenameSubobject(Subobject, SubobjectName);
			}
		}
	}
	else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(RefProperty))
	{
		// Rename all subobjects referenced by this array property (potentially in a C-style array)
		for (int32 Index = 0; Index < ArrayProp->ArrayDim; ++Index)
		{
			FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(ContainerPtr, Index));
			for (int32 ElementIndex = 0; ElementIndex < ArrayHelper.Num(); ++ElementIndex)
			{
				FString NewPrefix;
				GenerateSubobjectName(NewPrefix, Prefix, ArrayProp, ElementIndex);
				void* ElementAddress = ArrayHelper.GetRawPtr(ElementIndex);
				RenameDefaultSubobjectsInternal(InArchetype, ElementAddress, ArrayProp->Inner, NewPrefix);
			}
		}
	}
	else if (FSetProperty* SetProp = CastField<FSetProperty>(RefProperty))
	{
		for (int32 Index = 0; Index < SetProp->ArrayDim; ++Index)
		{
			FScriptSetHelper SetHelper(SetProp, SetProp->ContainerPtrToValuePtr<void>(ContainerPtr, Index));
			for (int32 ElementIndex = 0, Count = SetHelper.Num(); Count; ++ElementIndex)
			{
				if (SetHelper.IsValidIndex(ElementIndex))
				{
					FString NewPrefix;
					GenerateSubobjectName(NewPrefix, Prefix, SetProp, ElementIndex);
					void* ElementAddress = SetHelper.GetElementPtr(ElementIndex);
					RenameDefaultSubobjectsInternal(InArchetype, ElementAddress, SetProp->ElementProp, NewPrefix);
					--Count;
				}
			}
		}
	}
	else if (FMapProperty* MapProp = CastField<FMapProperty>(RefProperty))
	{
		for (int32 Index = 0; Index < MapProp->ArrayDim; ++Index)
		{
			FScriptMapHelper MapHelper(MapProp, MapProp->ContainerPtrToValuePtr<void>(ContainerPtr, Index));
			for (int32 ElementIndex = 0, Count = MapHelper.Num(); Count; ++ElementIndex)
			{
				if (MapHelper.IsValidIndex(ElementIndex))
				{
					FString NewPrefix;
					GenerateSubobjectName(NewPrefix, Prefix, MapProp, ElementIndex);
					uint8* ValuePairPtr = MapHelper.GetPairPtr(ElementIndex);

					RenameDefaultSubobjectsInternal(InArchetype, ValuePairPtr, MapProp->KeyProp, NewPrefix + TEXT("_Key"));
					RenameDefaultSubobjectsInternal(InArchetype, ValuePairPtr, MapProp->ValueProp, NewPrefix + TEXT("_Value"));

					--Count;
				}
			}
		}
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(RefProperty))
	{
		for (int32 Index = 0; Index < StructProp->ArrayDim; ++Index)
		{
			FString NewPrefix;
			GenerateSubobjectName(NewPrefix, Prefix, StructProp, Index);
			void* StructAddress = StructProp->ContainerPtrToValuePtr<void>(ContainerPtr, Index);
			RenameDefaultSubobjectsInternal(InArchetype, StructAddress, StructProp->Struct, NewPrefix);
		}
	}
	else if (FOptionalProperty* OptionProp = CastField<FOptionalProperty>(RefProperty))
	{
		FProperty* ValueProp = OptionProp->GetValueProperty();
		checkf(ValueProp->GetOffset_ForInternal() == 0, TEXT("Expected offset of value property of option property \"%s\" to be 0, got %d"), *OptionProp->GetFullName(), ValueProp->GetOffset_ForInternal());
		FString NewPrefix(Prefix);
		for (int32 Index = 0; Index < OptionProp->ArrayDim; ++Index)
		{
			// If for some reason the offset of ValueProp is not 0 then we may need to adjust how we calculate the ValueAddress
			void* ValueAddress = OptionProp->ContainerPtrToValuePtr<void>(ContainerPtr, Index);
			// Update the prefix only if this is an actual C-style array
			if (OptionProp->ArrayDim > 1)
			{
				GenerateSubobjectName(NewPrefix, Prefix, OptionProp, Index);
			}
			RenameDefaultSubobjectsInternal(InArchetype, ValueAddress, ValueProp, NewPrefix);
		}
	}
}

void RenameDefaultSubobjectsInternal(UObject* InArchetype, void* ContainerPtr, UStruct* Struct, const FString& Prefix)
{
	for (FProperty* RefProperty = Struct->RefLink; RefProperty; RefProperty = RefProperty->NextRef)
	{
		RenameDefaultSubobjectsInternal(InArchetype, ContainerPtr, RefProperty, Prefix);
	}
}

} // namespace VerseClassPrivate

void UVerseClass::RenameDefaultSubobjects(UObject* InObject)
{
	VerseClassPrivate::RenameDefaultSubobjectsInternal(InObject, InObject, InObject->GetClass(), FString());
}

int32 UVerseClass::GetVerseFunctionParameterCount(UFunction* Func)
{
	int32 ParameterCount = 0;
	if (FStructProperty* TupleProperty = CastField<FStructProperty>(Func->ChildProperties))
	{
		if (UStruct* TupleStruct = TupleProperty->Struct)
		{
			for (TFieldIterator<FProperty> It(TupleProperty->Struct); It; ++It)
			{
				if (It->GetFName() != UVerseClass::StructPaddingDummyName)
				{
					ParameterCount++;
				}
			}
		}
	}
	else
	{
		for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_OutParm))
			{
				ParameterCount++;
			}
		}
	}
	return ParameterCount;
}

void UVerseClass::ForEachVerseFunction(UObject* Object, TFunctionRef<bool(FVerseFunctionDescriptor)> Operation, EFieldIterationFlags IterationFlags)
{
#if WITH_VERSE_BPVM
	checkf(Object, TEXT("Object instance must be provided when iterating Verse functions"));
	for (UVerseClass* Class = Cast<UVerseClass>(Object->GetClass());
		 Class != nullptr;
		 Class = Cast<UVerseClass>(Class->GetSuperClass()))
	{
		for (const TPair<FName, FName>& NamePair : Class->DisplayNameToUENameFunctionMap)
		{
			if (UFunction* VMFunc = Class->FindFunctionByName(NamePair.Value))
			{
				FVerseFunctionDescriptor Descriptor(Object, VMFunc, NamePair.Key, NamePair.Value);
				if (!Operation(Descriptor))
				{
					break;
				}
			}
		}

		if (!EnumHasAnyFlags(IterationFlags, EFieldIterationFlags::IncludeSuper))
		{
			break;
		}
	}
#endif // WITH_VERSE_BPVM
}

#if WITH_VERSE_BPVM
FVerseFunctionDescriptor UVerseClass::FindVerseFunctionByDisplayName(UObject* Object, const FString& DisplayName, EFieldIterationFlags SearchFlags)
{
	FName DisplayFName(DisplayName);
	checkf(Object, TEXT("Object instance must be provided when searching for Verse functions"));
	for (UVerseClass* Class = Cast<UVerseClass>(Object->GetClass());
		 Class != nullptr;
		 Class = Cast<UVerseClass>(Class->GetSuperClass()))
	{
		if (FName* UEName = Class->DisplayNameToUENameFunctionMap.Find(DisplayFName))
		{
			return FVerseFunctionDescriptor(Object, nullptr, DisplayFName, *UEName);
		}

		if (!EnumHasAnyFlags(SearchFlags, EFieldIterationFlags::IncludeSuper))
		{
			break;
		}
	}
	return FVerseFunctionDescriptor();
}
#endif // WITH_VERSE_BPVM

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
void UVerseClass::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);
	UVerseClass* This = static_cast<UVerseClass*>(InThis);
	Collector.AddReferencedVerseValue(This->Shape);
	Collector.AddReferencedVerseValue(This->Class);
}
#endif
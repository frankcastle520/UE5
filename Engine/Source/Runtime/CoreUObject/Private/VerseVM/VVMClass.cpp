// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)
#include "VerseVM/VVMClass.h"
#include "Async/ExternalMutex.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/VerseValueProperty.h"
#include "VerseVM/Inline/VVMAbstractVisitorInline.h"
#include "VerseVM/Inline/VVMClassInline.h"
#include "VerseVM/Inline/VVMMarkStackVisitorInline.h"
#include "VerseVM/Inline/VVMNativeStructInline.h"
#include "VerseVM/Inline/VVMObjectInline.h"
#include "VerseVM/Inline/VVMScopeInline.h"
#include "VerseVM/Inline/VVMShapeInline.h"
#include "VerseVM/Inline/VVMUniqueStringInline.h"
#include "VerseVM/Inline/VVMValueObjectInline.h"
#include "VerseVM/VVMEngineEnvironment.h"
#include "VerseVM/VVMFunction.h"
#include "VerseVM/VVMGlobalTrivialEmergentTypePtr.h"
#include "VerseVM/VVMNativeStruct.h"
#include "VerseVM/VVMPackage.h"
#include "VerseVM/VVMProcedure.h"
#include "VerseVM/VVMTypeCreator.h"
#include "VerseVM/VVMValuePrinting.h"
#include "VerseVM/VVMVar.h"
#include "VerseVM/VVMVerse.h"
#include "VerseVM/VVMVerseClass.h"
#include "VerseVM/VVMVerseStruct.h"

namespace Verse
{

bool VConstructor::VEntry::IsMethod() const
{
	VValue EntryValue = Value.Get();
	if (VFunction* EntryFunction = EntryValue.DynamicCast<VFunction>())
	{
		return !EntryFunction->HasSelf();
	}
	else if (VNativeFunction* EntryNativeFunction = EntryValue.DynamicCast<VNativeFunction>())
	{
		return !EntryNativeFunction->HasSelf();
	}
	return false;
}

DEFINE_DERIVED_VCPPCLASSINFO(VConstructor);
TGlobalTrivialEmergentTypePtr<&VConstructor::StaticCppClassInfo> VConstructor::GlobalTrivialEmergentType;

void VConstructor::SerializeImpl(VConstructor*& This, FAllocationContext Context, FAbstractVisitor& Visitor)
{
	if (Visitor.IsLoading())
	{
		uint64 ScratchNumEntries = 0;
		Visitor.BeginArray(TEXT("Entries"), ScratchNumEntries);
		This = &VConstructor::NewUninitialized(Context, (uint32)ScratchNumEntries);
		for (uint32 Index = 0; Index < This->NumEntries; ++Index)
		{
			Visitor.VisitObject(TEXT(""), [This, &Visitor, Index] {
				Visitor.Visit(This->Entries[Index].Name, TEXT("Name"));
				Visitor.Visit(This->Entries[Index].bNative, TEXT("Native"));
				Visitor.Visit(This->Entries[Index].Type, TEXT("Type"));
				Visitor.Visit(This->Entries[Index].Value, TEXT("Value"));
				Visitor.Visit(This->Entries[Index].bDynamic, TEXT("Dynamic"));
			});
		}
		Visitor.EndArray();
	}
	else
	{
		This->VisitReferences(Visitor);
	}
}

template <typename TVisitor>
void VConstructor::VisitReferencesImpl(TVisitor& Visitor)
{
	if constexpr (TVisitor::bIsAbstractVisitor)
	{
		uint64 ScratchNumEntries = NumEntries;
		Visitor.BeginArray(TEXT("Entries"), ScratchNumEntries);
		for (uint32 Index = 0; Index < NumEntries; ++Index)
		{
			Visitor.VisitObject(TEXT(""), [this, &Visitor, Index] {
				Visitor.Visit(Entries[Index].Name, TEXT("Name"));
				Visitor.Visit(Entries[Index].bNative, TEXT("Native"));
				Visitor.Visit(Entries[Index].Type, TEXT("Type"));
				Visitor.Visit(Entries[Index].Value, TEXT("Value"));
				Visitor.Visit(Entries[Index].bDynamic, TEXT("Dynamic"));
			});
		}
		Visitor.EndArray();
	}
	else
	{
		for (uint32 Index = 0; Index < NumEntries; ++Index)
		{
			Visitor.Visit(Entries[Index].Name, TEXT("Name"));
			Visitor.Visit(Entries[Index].Type, TEXT("Type"));
			Visitor.Visit(Entries[Index].Value, TEXT("Value"));
		}
	}
}

void VConstructor::ToStringImpl(FStringBuilderBase& Builder, FAllocationContext Context, const FCellFormatter& Formatter)
{
	Builder.Append(TEXT("\n"));
	for (uint32 Index = 0; Index < NumEntries; ++Index)
	{
		const VEntry& Entry = Entries[Index];
		Builder.Append(TEXT("\t"));
		Formatter.Append(Builder, Context, *Entry.Name);
		Builder.Append(TEXT(" : Entry(Value: "));
		Entry.Value.Get().ToString(Builder, Context, Formatter);
		Builder.Append(TEXT(", Dynamic: "));
		Builder.Append(Entry.bDynamic ? TEXT("true") : TEXT("false"));
		Builder.Append(TEXT("))\n"));
	}
}

VFunction* VConstructor::LoadFunction(FAllocationContext Context, VUniqueString& FieldName, VValue SelfObject)
{
	// TODO: (yiliang.siew) This should probably be improved with inline caching or a hashtable instead for constructors
	// with lots of entries.
	for (uint32 Index = 0; Index < NumEntries; ++Index)
	{
		VEntry& CurrentEntry = Entries[Index];
		if (*CurrentEntry.Name.Get() != FieldName)
		{
			continue;
		}
		if (VFunction* Procedure = Entries[Index].Value.Get().DynamicCast<VFunction>(); Procedure && !Procedure->HasSelf())
		{
			// At this point (super:)/scope should already be filled in.
			VFunction& NewFunction = Procedure->Bind(Context, SelfObject);
			return &NewFunction;
		}
	}
	return nullptr;
}

DEFINE_DERIVED_VCPPCLASSINFO(VClass)
TGlobalTrivialEmergentTypePtr<&VClass::StaticCppClassInfo> VClass::GlobalTrivialEmergentType;

template <typename TVisitor>
void VClass::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.VisitClass(GetName(), [this, &Visitor] {
		Visitor.Visit(ClassName, TEXT("ClassName"));
		Visitor.Visit(UEMangledName, TEXT("UEMangledName"));
		Visitor.Visit(Scope, TEXT("Scope"));
		Visitor.Visit(Constructor, TEXT("Constructor"));
		Visitor.Visit(AssociatedUStruct, TEXT("AssociatedUStruct"));

		// Mark the inherited classes to ensure that they don't get swept during GC since we want to keep their information
		// around when anything needs to query the class inheritance hierarchy.
		if constexpr (TVisitor::bIsAbstractVisitor)
		{
			uint64 ScratchNumInherited = NumInherited;
			Visitor.BeginArray(TEXT("Inherited"), ScratchNumInherited);
		}
		Visitor.Visit(Inherited, Inherited + NumInherited);
		if constexpr (TVisitor::bIsAbstractVisitor)
		{
			Visitor.EndArray();
		}

		// We need both the unique string sets and emergent types that are being cached for fast lookup of emergent types to remain allocated.
		UE::FExternalMutex ExternalMutex(Mutex);
		UE::TUniqueLock Lock(ExternalMutex);
		Visitor.Visit(EmergentTypesCache, TEXT("EmergentTypesCache"));
	});
}

VClass::VClass(FAllocationContext Context, VPackage* InScope, VArray* InName, VArray* InUEMangledName, UStruct* InImportStruct, bool bInNative, EKind InKind, const TArray<VClass*>& InInherited, VConstructor& InConstructor)
	: VType(Context, &GlobalTrivialEmergentType.Get(Context))
	, Scope(Context, InScope)
	, ClassName(Context, InName)
	, UEMangledName(Context, InUEMangledName)
	, bNative(bInNative)
	, Kind(InKind)
	, NumInherited(InInherited.Num())
{
	if (InImportStruct != nullptr)
	{
		AssociatedUStruct.Set(Context, InImportStruct);
	}

	// NOTE: (yiliang.siew) If a class has no base class, we still want to set the scope accordingly for lambda captures,
	// which may capture other variables but not necessitate having a superclass.
	// We only need to create one scope, since all methods of this class should share the same one.
	VScope& NewFunctionScope = VScope::New(Context, nullptr);
	for (VClass* CurrentInheritedType : InInherited)
	{
		// NOTE: (yiliang.siew) We're not interested in structs/interfaces, since they can't have methods anyway.
		if (CurrentInheritedType->GetKind() == VClass::EKind::Class)
		{
			NewFunctionScope.SuperClass.Set(Context, CurrentInheritedType);
			// `(super:)` only refers to the first superclass in the inheritance hierarchy - `(super:)` can't be chained.
			break;
		}
	}

	// We flatten the entries here from the base class and all its superclasses - the values of the base class's entries
	// stomp that of its immediate superclass and so on.
	TSet<VUniqueString*> Fields;
	Fields.Reserve(InConstructor.NumEntries);
	TArray<VConstructor::VEntry> Entries;
	Entries.Reserve(InConstructor.NumEntries);
	Extend(Fields, Entries, InConstructor);

	// NOTE: (yiliang.siew) We stuff the `(super:)` information into the constant functions for this class pointing
	// directly to the superclass, since that is the same for all instances. We're doing it here since this is the first
	// occurrence where the function entries first gets associated with the class by virtue of the constructor being
	// passed here. Additionally, we only run this for the entries of the current constructor being passed in since
	// the base classes don't need to have their entries updated. (They should be updated when they themselves are
	// being constructed.)
	for (VConstructor::VEntry& CurrentEntry : Entries)
	{
		// If we get a procedure, we wrap it in a function that can store the given scope - we differentiate
		// between _actual_ functions that are in entries (e.g. if a field points to a free function, for example),
		// whose scopes we _don't_ want to modify (since they already presumably capture whatever their lexical scope is
		// outside of the fact that this class field is pointing to said function.)
		if (VProcedure* CurrentProcedure = CurrentEntry.Value.Get().DynamicCast<VProcedure>())
		{
			// The constructor being passed in shouldn't be responsible for filling in the `(super:)` since
			// it doesn't know what class it's creating it for - it's here, when we construct the class that we know that.
			VFunction& NewFunction = VFunction::NewUnbound(Context, *CurrentProcedure, NewFunctionScope);
			CurrentEntry.Value.Set(Context, NewFunction);
		}
	}

	// Now add the entries for the superclasses, which don't need the scopes (containing `(super:)`) updated.
	for (int32 Index = 0; Index < InInherited.Num(); ++Index)
	{
		V_DIE_IF(Index != 0 && InInherited[Index]->Kind == EKind::Class);
		Extend(Fields, Entries, *InInherited[Index]->Constructor.Get());
	}
	Constructor.Set(Context, VConstructor::New(Context, Entries));

	for (uint32 Index = 0; Index < NumInherited; ++Index)
	{
		new (&Inherited[Index]) TWriteBarrier<VClass>(Context, InInherited[Index]);
	}
}

void VClass::Extend(TSet<VUniqueString*>& Fields, TArray<VConstructor::VEntry>& Entries, const VConstructor& Base)
{
	for (uint32 Index = 0; Index < Base.NumEntries; ++Index)
	{
		const VConstructor::VEntry& Entry = Base.Entries[Index];
		if (VUniqueString* FieldName = Entry.Name.Get())
		{
			bool bIsAlreadyInSet;
			Fields.FindOrAdd(FieldName, &bIsAlreadyInSet);
			if (bIsAlreadyInSet)
			{
				continue;
			}
		}
		Entries.Add(Entry);
	}
}

VValueObject& VClass::NewVObject(FAllocationContext Context, VUniqueStringSet& ArchetypeFields, const TArray<VValue>& ArchetypeValues, TArray<VFunction*>& OutInitializers)
{
	V_DIE_IF(IsNative());

	// Combine the class and archetype to determine which fields will live in the object.
	VEmergentType& NewEmergentType = GetOrCreateEmergentTypeForArchetype(Context, ArchetypeFields, &VValueObject::StaticCppClassInfo);
	VValueObject* NewObject = &VValueObject::NewUninitialized(Context, NewEmergentType);

	if (Kind == EKind::Struct)
	{
		NewObject->SetIsStruct();
	}

	// Initialize fields from the archetype.
	// NOTE: This assumes that the order of values matches the IDs of the field set.
	for (auto It = ArchetypeFields.begin(); It != ArchetypeFields.end(); ++It)
	{
		FOpResult FieldResult = NewObject->SetField(Context, *It->Get(), ArchetypeValues[It.GetId().AsInteger()]);
		V_DIE_UNLESS(FieldResult.Kind == FOpResult::Return);
	}

	// Build the sequence of VProcedures to finish object construction.
	GatherInitializers(ArchetypeFields, OutInitializers);

	return *NewObject;
}

FOpResult VClass::NewNativeStruct(FAllocationContext Context, VUniqueStringSet& ArchetypeFields, const TArray<VValue>& ArchetypeValues, TArray<VFunction*>& OutInitializers)
{
	V_DIE_UNLESS(IsNativeStruct());

	VEmergentType& NewEmergentType = *GetUStruct<UVerseStruct>()->EmergentType;
	VNativeStruct* NewObject = &VNativeStruct::NewUninitialized(Context, NewEmergentType);
	FOpResult Result = InitInstance(Context, *NewEmergentType.Shape, NewObject->GetData(*NewEmergentType.CppClassInfo));
	if (Result.Kind != FOpResult::Return)
	{
		return Result;
	}

	// Initialize fields from the archetype.
	// NOTE: This assumes that the order of values matches the IDs of the field set.
	for (auto It = ArchetypeFields.begin(); It != ArchetypeFields.end(); ++It)
	{
		FOpResult FieldResult = NewObject->SetField(Context, *It->Get(), ArchetypeValues[It.GetId().AsInteger()]);
		if (FieldResult.Kind != FOpResult::Return)
		{
			return FieldResult;
		}
	}

	// Build the sequence of VProcedures to finish object construction.
	GatherInitializers(ArchetypeFields, OutInitializers);

	V_RETURN(*NewObject);
}

UObject* VClass::NewUObject(FAllocationContext Context, VUniqueStringSet& ArchetypeFields, const TArray<VValue>& ArchetypeValues, TArray<VFunction*>& OutInitializers)
{
	V_DIE_IF(IsStruct());

	UVerseClass* ObjectUClass = GetOrCreateUStruct<UVerseClass>(Context);

	FStaticConstructObjectParameters Parameters(ObjectUClass);
	// Note: Object will get a default name based on class name
	// TODO: Migrate FSolarisInstantiationScope functionality here to determine Outer and SetFlags
	// TODO: Set instancing graph properly
	Parameters.Outer = GetTransientPackage();
	Parameters.SetFlags = RF_NoFlags;
	UObject* NewObject = StaticConstructObject_Internal(Parameters);

	for (auto It = ArchetypeFields.begin(); It != ArchetypeFields.end(); ++It)
	{
		const VShape::VEntry* Field = ObjectUClass->Shape->GetField(*It->Get());
		V_DIE_UNLESS(Field);
		VValue Value = ArchetypeValues[It.GetId().AsInteger()];
		switch (Field->Type)
		{
			case EFieldType::FProperty:
				VNativeRef::Set<false>(Context, nullptr, NewObject, Field->UProperty, Value);
				break;
			case EFieldType::FPropertyVar:
				VNativeRef::Set<false>(Context, nullptr, NewObject, Field->UProperty, Value.StaticCast<VVar>().Get(Context));
				break;
			case EFieldType::FVerseProperty:
				Field->UProperty->ContainerPtrToValuePtr<VRestValue>(NewObject)->Set(Context, Value);
				break;
			default:
				V_DIE("Unexpected field type");
				break;
		}
	}

	// Build the sequence of VProcedures to finish object construction.
	GatherInitializers(ArchetypeFields, OutInitializers);

	return NewObject;
}

void VClass::GatherInitializers(VUniqueStringSet& ArchetypeFields, TArray<VFunction*>& OutInitializers)
{
	// Build the sequence of VProcedures to finish object construction.
	V_DIE_UNLESS(OutInitializers.IsEmpty());
	OutInitializers.Reserve(Constructor->NumEntries);
	for (uint32 Index = 0; Index < Constructor->NumEntries; ++Index)
	{
		VConstructor::VEntry& Entry = Constructor->Entries[Index];

		// Skip fields which were already initialized by the archetype.
		if (const VUniqueString* Field = Entry.Name.Get())
		{
			FSetElementId ElementId = ArchetypeFields.FindId(Field->AsStringView());
			if (ArchetypeFields.IsValidId(ElementId))
			{
				continue;
			}
		}

		// Record procedures for default initializers and blocks.
		if (VFunction* Initializer = Entry.Initializer())
		{
			OutInitializers.Add(Initializer);
		}
	}
}

VEmergentType& VClass::GetOrCreateEmergentTypeForArchetype(FAllocationContext Context, VUniqueStringSet& ArchetypeFieldNames, VCppClassInfo* CppClassInfo)
{
	// Limit archetype instantiation to VObject-derived types for now
	V_DIE_UNLESS(!IsNativeStruct());

	// Note: We can look up the emergent type without locking our Mutex since this thread is the only one mutating the hash table
	// TODO: This in the future shouldn't even require a hash table lookup when we introduce inline caching for this.
	const uint32 ArcheTypeHash = GetTypeHash(ArchetypeFieldNames);
	if (TWriteBarrier<VEmergentType>* ExistingEmergentType = EmergentTypesCache.FindByHash(ArcheTypeHash, ArchetypeFieldNames))
	{
		return *ExistingEmergentType->Get();
	}

	// Build a combined map of all fields from the archetype, this class, and superclasses.
	// Earlier fields (from the archetype and subclasses) override later fields via `FindOrAdd`.
	VShape::FieldsMap Fields;
	for (const TWriteBarrier<VUniqueString>& Field : ArchetypeFieldNames)
	{
		// Always store fields from the archetype in the object.
		Fields.Add({Context, Field.Get()}, VShape::VEntry::Offset());
	}
	for (uint32 Index = 0; Index < Constructor->NumEntries; ++Index)
	{
		VConstructor::VEntry& Entry = Constructor->Entries[Index];
		if (VUniqueString* FieldName = Entry.Name.Get())
		{
			if (Entry.bDynamic)
			{
				// Store dynamically-initialized and uninitialized fields in the object.
				Fields.FindOrAdd({Context, FieldName}, VShape::VEntry::Offset());
			}
			else
			{
				// Store constant-initialized fields in the shape.
				Fields.FindOrAdd({Context, FieldName}, VShape::VEntry::Constant(Context, Entry.Value.Get()));
			}
		}
	}

	// Compute the shape by interning the set of fields.
	VShape* NewShape = VShape::New(Context, MoveTemp(Fields));
	VEmergentType* NewEmergentType = VEmergentType::New(Context, NewShape, this, CppClassInfo);
	V_DIE_IF(NewEmergentType == nullptr);

	UE::FExternalMutex ExternalMutex(Mutex);
	UE::TUniqueLock Lock(ExternalMutex);

	// This new type will then be kept alive in the cache to re-vend if ever the exact same set of fields are used for
	// archetype instantiation of a different object.
	EmergentTypesCache.AddByHash(ArcheTypeHash, {Context, ArchetypeFieldNames}, {Context, *NewEmergentType});

	return *NewEmergentType;
}

VEmergentType& VClass::GetOrCreateEmergentTypeForImportedNativeStruct(FAllocationContext Context)
{
	V_DIE_UNLESS(IsNativeStruct());

	// Note: We can look up the emergent type without locking our Mutex since this thread is the only one mutating the hash table
	const uint32 SingleHash = 0; // For native structs, we only ever store one emergent type, regardless of archetype
	if (TWriteBarrier<VEmergentType>* ExistingEmergentType = EmergentTypesCache.FindByHash(SingleHash, TWriteBarrier<VUniqueStringSet>{}))
	{
		return *ExistingEmergentType->Get();
	}

	// Make sure alignment holds for this native struct
	UScriptStruct::ICppStructOps* CppStructOps = GetUStruct<UScriptStruct>()->GetCppStructOps();
	V_DIE_UNLESS(CppStructOps->GetAlignment() <= VObject::DataAlignment);

	// Imported structs have no shape since their internals are opaque
	VEmergentType* NewEmergentType = VEmergentType::New(Context, nullptr, this, &VNativeStruct::StaticCppClassInfo);

	UE::FExternalMutex ExternalMutex(Mutex);
	UE::TUniqueLock Lock(ExternalMutex);

	// Keep alive in cache for future requests
	EmergentTypesCache.AddByHash(SingleHash, {Context, nullptr}, {Context, NewEmergentType});

	return *NewEmergentType;
}

UStruct* VClass::CreateUStruct(FAllocationContext Context)
{
	ensure(!AssociatedUStruct); // Caller must ensure that this is not already set

	// Create the new UClass/UScriptStruct object

	IEngineEnvironment* Environment = VerseVM::GetEngineEnvironment();
	check(Environment);
	Environment->CreateUStruct(Context, this, AssociatedUStruct);

	return GetUStruct<UStruct>();
}

FOpResult VClass::InitInstance(FAllocationContext Context, VShape& Shape, void* Data) const
{
	for (uint32 Index = 0; Index < Constructor->NumEntries; ++Index)
	{
		VConstructor::VEntry& Entry = Constructor->Entries[Index];
		if (const VUniqueString* FieldName = Entry.Name.Get())
		{
			// NOTE: (yiliang.siew) Methods which are already-bound (i.e. with `Self` initialized) are stored in the object,
			// while unbound methods/functions stay in the shape (since they don't change).
			if (!Entry.bDynamic && !Entry.IsMethod())
			{
				FOpResult Result = VObject::SetField(Context, Shape, *FieldName, Data, Entry.Value.Get());
				if (Result.Kind != FOpResult::Return)
				{
					return Result;
				}
			}
		}
	}
	return {FOpResult::Return};
}

bool VClass::SubsumesImpl(FAllocationContext Context, VValue Value)
{
	VClass* InputType = nullptr;
	if (VObject* Object = Value.DynamicCast<VObject>())
	{
		VCell* TypeCell = Object->GetEmergentType()->Type.Get();
		checkSlow(TypeCell->IsA<VClass>());
		InputType = static_cast<VClass*>(TypeCell);
	}
	else if (Value.IsUObject())
	{
		InputType = CastChecked<UVerseClass>(Value.AsUObject()->GetClass())->Class.Get();
	}
	else
	{
		return false;
	}

	if (InputType == this)
	{
		return true;
	}

	TArray<VClass*, TInlineAllocator<8>> ToCheck;
	auto PushInherited = [&ToCheck](VClass* Class) {
		for (uint32 I = 0; I < Class->NumInherited; ++I)
		{
			ToCheck.Push(Class->Inherited[I].Get());
		}
	};

	PushInherited(InputType);
	while (ToCheck.Num())
	{
		VClass* Class = ToCheck.Pop();
		if (Class == this)
		{
			return true;
		}
		PushInherited(Class);
	}

	return false;
}

FUtf8StringView VClass::ExtractClassName() const
{
	FUtf8StringView ScratchName = GetName();
	if (ScratchName.Len() > 0)
	{
		int StartOfName = ScratchName.Find(":)");
		StartOfName = StartOfName == INDEX_NONE ? 0 : StartOfName + 2;
		int EndOfName = ScratchName.Find("(", StartOfName);
		EndOfName = EndOfName == INDEX_NONE ? ScratchName.Len() : EndOfName;
		ScratchName = ScratchName.SubStr(StartOfName, EndOfName - StartOfName);
	}
	return ScratchName;
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)

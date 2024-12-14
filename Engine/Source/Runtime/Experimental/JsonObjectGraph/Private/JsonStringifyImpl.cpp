// Copyright Epic Games, Inc. All Rights Reserved.

#include "JsonStringifyImpl.h"
#include "PrettyJsonWriter.h"
#include "Algo/Copy.h"
#include "Algo/ForEach.h"
#include "Algo/RemoveIf.h"
#include "Misc/App.h"
#include "JsonObjectGraphConventions.h"
#include "JsonStringifyArchive.h"
#include "JsonStringifyStructuredArchive.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatters/JsonArchiveOutputFormatter.h"
#include "Serialization/ArchiveUObjectFromStructuredArchive.h"
#include "UObject/EnumProperty.h"
#include "UObject/PropertyOptional.h"
#include "UObject/Package.h"
#include "UObject/TextProperty.h"

namespace UE::Private
{

FUtf8String GetPackageModuleName(FUtf8StringView PackageName)
{
	if (PackageName.StartsWith("/Script/"))
	{
		// native package, module name will immediately follow script
		// and object name will immediately follow that module name:
		return FUtf8String(UE_JSON_DOMAIN_IDENTIFIER_NATIVE);
	}
	else if (PackageName.StartsWith("/Game/"))
	{
		return TCHAR_TO_UTF8(FApp::GetProjectName());
	}
	else
	{
		check(PackageName[0] == '/');
		FUtf8StringView PackageNameNoSlash(PackageName.GetData() + 1, PackageName.Len() - 1);
		// normal asset, it should be located within a plugin or
		// project, that will serve as its 'module' name:
		int32 FolderIndex = INDEX_NONE;
		check(PackageNameNoSlash.FindChar(static_cast<UTF8CHAR>('/'), FolderIndex));

		return FUtf8String(PackageName.Mid(1, FolderIndex));
	}
}

FUtf8String GetPackageRelativePath(FUtf8StringView PackageName)
{
	check(PackageName[0] == '/');
	FUtf8StringView PackageNameNoSlash(PackageName.GetData() + 1, PackageName.Len() - 1);
	int32 FolderIndex = INDEX_NONE;
	check(PackageNameNoSlash.FindChar(static_cast<UTF8CHAR>('/'), FolderIndex));
	++FolderIndex;
	return FUtf8String(PackageName.RightChop(FolderIndex));
}

// gathering utils
struct FPackageReferenceFinder : public FArchiveUObject
{
	FPackageReferenceFinder(const UObject* Obj, TArray<const UObject*>& InReferences, bool bFilterEditorOnly)
		: FArchiveUObject()
		, References(InReferences)
		, OwningPackage(Obj->GetOutermost())
	{
		// Copying FPackageHarvester:
		SetIsPersistent(true);
		SetIsSaving(true);
		SetFilterEditorOnly(bFilterEditorOnly);
		ArIsObjectReferenceCollector = true;
		ArShouldSkipBulkData = true;
		if (Obj->HasAnyFlags(RF_ClassDefaultObject))
		{
			Obj->GetClass()->SerializeDefaultObject((UObject*)Obj, *this);
		}
		else
		{
			((UObject*)Obj)->Serialize(*this);
		}
	}

private:
	virtual FArchive& operator<<(UObject*& ObjRef) override
	{
		if (ObjRef != nullptr &&
			(!ObjRef->HasAnyFlags(RF_Transient) || ObjRef->IsNative()) &&
			!ObjRef->IsIn(GetTransientPackage()) &&
			ObjRef != OwningPackage)
		{
			// Set to null any pointer to an external asset
			References.Add(ObjRef);
		}

		return *this;
	}

	TArray<const UObject*>& References;
	const UPackage* const OwningPackage;
};

static const UClass* GetFirstNativeClass(const UClass* Class)
{
	const UClass* Iter = Class;
	while (Iter && !Iter->HasAnyClassFlags(CLASS_Native))
	{
		Iter = Iter->GetSuperClass();
	}
	return Iter;
}

static void FilterEditorOnlyObjects(
	TArray<UObject*>& Objects
)
{
	#if WITH_EDITOR
	Objects.SetNum(Algo::StableRemoveIf(Objects, [](const UObject* Obj) {
		const UClass* NativeClass = GetFirstNativeClass(Obj->GetClass());
		return IsEditorOnlyObject(NativeClass) || !(NativeClass->ClassDefaultObject->NeedsLoadForClient() || NativeClass->ClassDefaultObject->NeedsLoadForServer());
		}));
	#endif
}

static void GatherExports(
	TConstArrayView<const UObject*> Roots,
	TConstArrayView<const UObject*> DisallowList,
	bool bFilterEditorOnly,
	TArray<const UObject*>& OutRoots,
	TArray<const UObject*>& OutExports)
{
	// find everything roots references that is within roots and put it into
	// OutExports, unless it is in the disallow list:

	TSet<const UObject*> DisallowSet;
	DisallowSet.Append(DisallowList);
	for (const UObject* Obj : DisallowList)
	{
		// const hacks and GetObjects expressiveness junk
		TArray<UObject*> DisallowedSubObjects;
		GetObjectsWithOuter(Obj, DisallowedSubObjects, true, RF_Transient);
		for (UObject* DisallowedObj : DisallowedSubObjects)
		{
			DisallowSet.Add(DisallowedObj);
		}
	}
	TSet<const UObject*> AllowedRoots;
	AllowedRoots.Append(Roots);
	for (const UObject* Obj : Roots)
	{
		if (Obj && !Obj->IsA<UPackage>())
		{
			OutRoots.Add(Obj);
		}
	}

	TArray<const UObject*> PendingRefs;
	Algo::CopyIf(Roots, PendingRefs, [](const UObject* Obj) { return IsValid(Obj); });

	TSet<const UObject*> RefsProcessed;
	RefsProcessed.Append(PendingRefs);

	TArray<const UObject*> ScratchRefs; // just keeping this allocation alive across iterations

	// Roots is invalid after this point!

	UObject* TransientPackage = GetTransientPackage();
	while (PendingRefs.Num())
	{
		const UObject* Iter = PendingRefs.Pop();
		if (Iter->GetOutermost() == TransientPackage)
		{
			continue;
		}

		if (AllowedRoots.Contains(Iter) && Iter->IsA<UPackage>())
		{
			// add all immediate roots of a package, if filter 'editor only' objects
			// exclude any object that are of an editor only native type:
			TArray<UObject*> PackageInners;
			GetObjectsWithOuter(Iter, PackageInners, false, RF_Transient);
			if (bFilterEditorOnly)
			{
				FilterEditorOnlyObjects(PackageInners);
			}
			PackageInners.Sort([](const UObject& A, const UObject& B) { return A.GetFName().LexicalLess(B.GetFName()); });
			OutRoots.Append(PackageInners);
			PendingRefs.Append(PackageInners);
			continue;
		}

		OutExports.Add(Iter);

		ScratchRefs.Add(Iter->GetClass());
		FPackageReferenceFinder ReferencedObjects(Iter, ScratchRefs, bFilterEditorOnly);
		for (const UObject* Obj : ScratchRefs)
		{
			if (RefsProcessed.Contains(Obj))
			{
				continue;
			}
			RefsProcessed.Add(Obj);

			if (DisallowSet.Contains(Obj))
			{
				continue;
			}

			const UObject* OuterIter = Obj->GetOuter();
			bool bIsInRoot = false;
			while (OuterIter)
			{
				if (AllowedRoots.Contains(OuterIter))
				{
					bIsInRoot = true;
					break;
				}
				OuterIter = OuterIter->GetOuter();
			}
			if (!bIsInRoot)
			{
				continue;
			}

			PendingRefs.Add(Obj);
		}
		ScratchRefs.Reset();
	}
}

static void WriteObjectPath(FUtf8StringBuilderBase& OutPath, const UObject* ForObject, const UObject* OuterLimit)
{
	const auto WriteObjectPathImpl = [&OutPath, OuterLimit](const UObject* ForObject, auto Self)
		{
			check(ForObject);
			// ofpa files still have an outer package, but the GetPackage() terminator
			// will not be reachable via the outer chain, replace any encoutered UPackage
			// w/ the OuterLimit (acquired via GetPackage) when serializing:
			if(Cast<UPackage>(ForObject) && ForObject != OuterLimit)
			{
				ForObject = OuterLimit;
			}

			if (ForObject == OuterLimit)
			{
				return;
			}
			Self(ForObject->GetOuter(), Self);
			OutPath << "/";
			OutPath << ForObject->GetName();
		};
	WriteObjectPathImpl(ForObject, WriteObjectPathImpl);
}

static void WriteFieldPath(FUtf8StringBuilderBase& OutPath, const FField* ForField, const UObject* OuterLimit)
{
	const auto WriteWriteFieldPathImpl = [&OutPath, OuterLimit](const UObject* ForObject, const FField* ForField, auto Self)
		{
			if (ForObject == OuterLimit)
			{
				return;
			}
			if (ForField)
			{
				FFieldVariant Owner = ForField->GetOwnerVariant();
				Self(Owner.ToUObject(), Owner.ToField(), Self);
			}
			else if (ForObject)
			{
				Self(ForObject->GetOuter(), nullptr, Self);
			}
			OutPath << "/";
			if (ForObject)
			{
				OutPath << ForObject->GetName();
			}
			else
			{
				check(ForField);
				OutPath << ForField->GetName();
			}
		};
	WriteWriteFieldPathImpl(nullptr, ForField, WriteWriteFieldPathImpl);
}

FJsonStringifyImpl::FJsonStringifyImpl(TConstArrayView<const UObject*> Roots, const FJsonStringifyOptions& Options)
	: WriteOptions(Options)
	, MemoryWriter(Result)
	, Writer(FJsonWriter::Create(&MemoryWriter))
	, CurrentObject(nullptr)
{
	const bool bFilterEditorOnly = (Options.Flags & EJsonStringifyFlags::FilterEditorOnlyData) != EJsonStringifyFlags::Default;
	TArray<const UObject*> Exports;
	GatherExports(Roots, {}, bFilterEditorOnly, RootObjects, Exports);

	ObjectsToExport = TSet<const UObject*>(Exports);

	MemoryWriter.SetIsPersistent(true);
	MemoryWriter.SetFilterEditorOnly(bFilterEditorOnly);
	MemoryWriter.SetIsTextFormat(true);
}

FUtf8String FJsonStringifyImpl::ToJson()
{
	ToJsonBytes();
	// Hacking around TJsonWriter and FMemoryWriter, is an array of bytes by
	// any other name not exceedingly dangerous? Everyone should be working
	// with utf8:
	TArray<ANSICHAR>& ResultTyped = reinterpret_cast<TArray<ANSICHAR>&>(Result);
	return FUtf8String(MoveTemp(ResultTyped));
}

void FJsonStringifyImpl::WriteObjectAsJsonToWriter(const UObject* OwningObject, const UObject* InObject, TSharedRef<FJsonWriter> WriterToUse)
{
	if (CurrentScope)
	{
		CurrentScope->Apply();
	}

	if (InObject && InObject->IsIn(OwningObject) &&
		ObjectsToExport.Contains(InObject) &&
		!ObjectsExported.Contains(InObject))
	{
		ObjectsExported.Add(InObject);
		TSharedRef<FJsonWriter> RootWriter = Writer;
		Writer = WriterToUse;
		WriteObjectToJson(InObject);
		Writer = RootWriter;
	}
	else
	{
		// we just need to write a path
		WriterToUse->WriteValueInline(FUtf8StringView(WriteObjectReference(InObject)));
	}
}

void FJsonStringifyImpl::WriteFieldReferenceTo(const UObject* OwningObject, const FField* Value, TSharedRef<FJsonWriter> WriterToUse)
{
	WriterToUse->WriteValueInline(FUtf8StringView(WriteFieldReference(Value)));
}

void FJsonStringifyImpl::WriteObjectAsJsonToArchive(const UObject* OwningObject, const UObject* InObject, FArchive* ArchiveToUse, int32 InitialIndentLevel)
{
	TSharedRef<FJsonWriter> JsonWriter = FJsonWriter::Create(ArchiveToUse, InitialIndentLevel);
	JsonWriter->HACK_SetPreviousTokenWritten();
	WriteObjectAsJsonToWriter(OwningObject, InObject, JsonWriter);
}

void FJsonStringifyImpl::ToJsonBytes()
{
	if (RootObjects.Num() == 0)
	{
		return;
	}

	// main entry point to our writer state machine:
	Writer->WriteObjectStart();
	Writer->WriteIdentifierPrefix(UE_JSON_ROOT_OBJECTS_KEY_TCHAR);
	Writer->WriteArrayStartInline();
	Writer->WriteLineTerminator();
	for (const UObject* Object : RootObjects)
	{
		ObjectsExported.Add(Object);
		WriteObjectToJson(Object);
	}
	Writer->WriteArrayEnd();
	
#if WITH_TEXT_ARCHIVE_SUPPORT
	if (Versions.Num() != 0)
	{
		// filter dupes, preserving order of first encounter - we need some stable order:
		TSet<FGuid> Encountered;
		Versions.SetNum(Algo::StableRemoveIf(Versions,
			[&Encountered](const FCustomVersion& V)
			{
				if (Encountered.Contains(V.Key))
				{
					return true;
				}
				else
				{
					Encountered.Add(V.Key);
					return false;
				}
			}));

		Writer->WriteIdentifierPrefix(UE_JSON_CUSTOM_VERSIONS_KEY_TCHAR);
		FJsonStringifyStructuredArchive::WriteCustomVersionValueInline(Versions, Writer->GetIndentLevel(), MemoryWriter);
	}
#endif // WITH_TEXT_ARCHIVE_SUPPORT
	Writer->WriteObjectEnd();
	// Result is now populated with Json representation
}

void FJsonStringifyImpl::WriteObjectToJson(const UObject* Object)
{
	CurrentObject = Object;

	// loop properties, writing any that have changed:
	const UObject* const Archetype = Object->GetArchetype();
	const UClass* const ArchetypeClass = Archetype->GetClass();
	Writer->WriteObjectStartInline();
	// Write native UObject data - name, type, flags, native user serialize, etc
	WriteNativeObjectData();

	for (TFieldIterator<FProperty> FieldIt(Object->GetClass()); FieldIt; ++FieldIt)
	{
		FProperty* Property = *FieldIt;
		const UObject* ValidatedArchtype = ArchetypeClass->IsChildOf(Property->GetOwnerClass()) ?
			Archetype : nullptr;

		WriteIdentifierAndValueToJson(Object, ValidatedArchtype, Property);
	}
	// We may have inner objects that were not referenced directly by this object, 
	// but will be referenced by other objects in the graph. We must write them here, or we
	// would have to encode them at the root level, which would disrupt locality.
	// The draw back of recording them here is that we must order them ourselves,
	// which will degrade the stability of the serialized buffer. It feels like 'the best
	// we can do' is to write these objects in alphabetical order:
	WriteIndirectlyReferencedContainedObjects(Object);
	Writer->WriteObjectEnd();

	// emit stream serializer, omitting tagged properties:
	CurrentObject = CurrentObject->GetOuter();
}

void FJsonStringifyImpl::WriteNativeObjectData()
{
	const UObject* Object = CurrentObject;
	check(Object);

	// Could use a structured archive to write this more declaratively...
	Writer->WriteIdentifierPrefix(UE_JSON_OBJECT_INSTANCE_KEY_TCHAR);
	Writer->WriteObjectStartInline();
	Writer->WriteValue(UE_JSON_OBJECT_NAME_KEY_TCHAR, Object->GetName());
	Writer->WriteUtf8Value(UE_JSON_OBJECT_CLASS_KEY_TCHAR, WriteObjectReference(Object->GetClass()));
	Writer->WriteValue(UE_JSON_OBJECT_FLAGS_KEY_TCHAR, Object->GetFlags() & RF_Load);
	Writer->WriteObjectEnd();

	// This is tricky, we have no good mechanism for detecting whether an object wants a structured
	// serialization or a traditional stream serialization. So I've decided to 'try' the structured
	// serializer and if it writes nothing we fall back to the stream (FArchive) serializer. Hopefully
	// some day we get rid of FStructuredArchive as it does not spark joy.
#if WITH_TEXT_ARCHIVE_SUPPORT
	TArray<uint8> StructuredData = StructuredDataToJson(Object, Writer->GetIndentLevel());
	if (!StructuredData.IsEmpty())
	{
		Writer->WriteIdentifierPrefix(UE_JSON_OBJECT_STRUCTURED_DATA_KEY_TCHAR);
		Writer->WriteJsonRaw(FAnsiStringView((ANSICHAR*)StructuredData.GetData(), StructuredData.Num()));
	}
	else
#endif
	{
		// no useful native structured data, write the native serial data:
		TArray<uint8> SerialData = SerialDataToJson(Object, Writer->GetIndentLevel());
		if (!SerialData.IsEmpty())
		{
			Writer->WriteIdentifierPrefix(UE_JSON_OBJECT_SERIAL_DATA_KEY_TCHAR);
			Writer->WriteJsonRaw(FAnsiStringView((ANSICHAR*)SerialData.GetData(), SerialData.Num()));
		}
	}

	// Sparse class data is ambiguously serialized as part of SerializeDefaultObject,
	// which our text serializer does not use (except for reference gathering). Lets write
	// the special SparseClassData member here:
	if (const UClass* AsClass = Cast<UClass>(Object))
	{
		if (const void* SCD = const_cast<UClass*>(AsClass)->GetSparseClassData(EGetSparseClassDataMethod::ReturnIfNull))
		{
			const UScriptStruct* SCDStruct = AsClass->GetSparseClassDataStruct();
			const void* DefaultSCD = AsClass->GetArchetypeForSparseClassData();
			const UScriptStruct* DefaultSCDStruct = AsClass->GetSparseClassDataArchetypeStruct();
			WriteStructToJsonWithIdentifier(UE_JSON_OBJECT_SPARSE_CLASS_DATA_KEY_TCHAR, SCD, DefaultSCD, SCDStruct, DefaultSCDStruct);
		}
	}
}

void FJsonStringifyImpl::WriteIndirectlyReferencedContainedObjects(const UObject* ForObject)
{
	TArray<const UObject*> UnwrittenInners;
	// @todo: naive implementation - is looping over every object here too slow?
	for (const UObject* ObjectToExport : ObjectsToExport)
	{
		if (ObjectToExport->GetOuter() == ForObject && !ObjectsExported.Contains(ObjectToExport))
		{
			UnwrittenInners.Add(ObjectToExport);
		}
	}

	if (UnwrittenInners.Num() == 0)
	{
		return;
	}

	UnwrittenInners.Sort([](const UObject& A, const UObject& B) { return A.GetName().Compare(B.GetName()) < 0; });

	// write all UnwrittenInners into the __IndirectlyReferenced member, for simplicity we 
	// will always encode this as an array. We know that these unwritten inners will not be outered
	// to the written objects here, so we don't need to tag them as exported ahead of time:
	Writer->WriteIdentifierPrefix(UE_JSON_OBJECT_INDIRECTLY_REFERENCED_KEY_TCHAR);
	Writer->WriteArrayStartInline();
	Writer->WriteLineTerminator();
	for (const UObject* Object : UnwrittenInners)
	{
		WriteObjectToJson(Object);
	}
	Writer->WriteArrayEnd();
}

FUtf8String FJsonStringifyImpl::WriteObjectReference(const UObject* ForObject) const
{
	// matching FSaveContext::GetSaveableStatusNoOuter, if we integrate into
	// save package we can reuse the logic from there:
	if (!ForObject || 
		!IsValid(ForObject) || 
		(ForObject->HasAnyFlags(RF_Transient) && !ForObject->IsNative()))
	{
		return UE_JSON_OBJECT_REF_PREFIX UE_JSON_REF_NONE;
	}

	// We want users to be able to move these files around the filesystem with
	// minimal fixup, so intra artifact references are relative to the file itself,
	// intra module references are relative to the module, but cross module references
	// are unambiguous and fully qualified. Here are some examples:
	//
	// uobject:__file__/a/path/to/a/child - this is a reference to an object within our file
	// uobject:__native__/native_module/native_object - this is a reference to a natively defined object in native_module
	// uobject:real_module/a/path/to/a/child.uasset/a/path/to/a/child/object - this is a reference to an object in another file
	//
	// file extensions are included in the path to disambiguate between folders and assets
	// that share a name. The special __file__ identifier exists so that assets internal
	// reference remain valid when copied, the special __native__ identifier exists so that
	// natively defined objects can be unambiguously referenced
	//
	// real_module in this example will refer to either a uplugin or uproject.

	FUtf8StringBuilderBase Reference;
	Reference << UE_JSON_OBJECT_REF_PREFIX;
	if (ObjectsToExport.Contains(ForObject))
	{
		Reference << UE_JSON_DOMAIN_IDENTIFIER_FILE;
		const UObject* OuterLimit = ForObject;
		while (ObjectsToExport.Contains(OuterLimit))
		{
			OuterLimit = OuterLimit->GetOuter();
		}
		WriteObjectPath(Reference, ForObject, OuterLimit);
	}
	else if (const UPackage* Package = ForObject->GetPackage())
	{
		const FUtf8String PackageName = TCHAR_TO_UTF8(*Package->GetName());
		const FUtf8String ModuleName = GetPackageModuleName(PackageName);

		Reference << ModuleName;

		FUtf8String PackagePath = GetPackageRelativePath(PackageName);
		Reference << PackagePath;
		if (ModuleName != UE_JSON_DOMAIN_IDENTIFIER_NATIVE)
		{
			Reference << UE_JSON_REF_POSTFIX;
		}
		WriteObjectPath(Reference, ForObject, Package);
	}
	else
	{
		checkf(false, TEXT("Found object with no package: %s"), *ForObject->GetPathName());
	}

	return Reference.ToString();
}

FUtf8String FJsonStringifyImpl::WriteFieldReference(const FField* Value) const
{
	if (!Value)
	{
		return UE_JSON_FIELD_REF_PREFIX UE_JSON_REF_NONE;
	}

	// identical convention for FField as above, albeit with a fieldref prefix
	// instead of a uobject:
	FUtf8StringBuilderBase Reference;

	Reference << UE_JSON_FIELD_REF_PREFIX;
	const UObject* OwningObject = Value->GetOwnerUObject();
	check(OwningObject);
	if (ObjectsToExport.Contains(OwningObject))
	{
		// object is within this file:
		Reference << UE_JSON_DOMAIN_IDENTIFIER_FILE;
		const UObject* OuterLimit = OwningObject;
		while (ObjectsToExport.Contains(OuterLimit))
		{
			OuterLimit = OuterLimit->GetOuter();
		}

		WriteFieldPath(Reference, Value, OuterLimit);
	}
	else if (const UPackage* Package = OwningObject->GetPackage())
	{
		// external object:
		const FUtf8String PackageName = TCHAR_TO_UTF8(*Package->GetName());
		const FUtf8String ModuleName = GetPackageModuleName(PackageName);

		Reference << ModuleName;

		FUtf8String PackagePath = GetPackageRelativePath(PackageName);
		Reference << PackagePath;
		if (ModuleName != UE_JSON_DOMAIN_IDENTIFIER_NATIVE)
		{
			Reference << UE_JSON_REF_POSTFIX;
		}
		WriteFieldPath(Reference, Value, Package);
	}
	else
	{
		checkf(false, TEXT("Found field with no package: %s"), *OwningObject->GetPathName());
	}

	return Reference.ToString();
}

void FJsonStringifyImpl::WriteIdentifierAndValueToJson(const void* Container, const void* DefaultContainer, const FProperty* Property)
{
	if (!Property->ShouldSerializeValue(MemoryWriter))
	{
		return;
	}

	// this is inefficient for structs... but it papers over some
	// problematic identical implementations. We will improve it
	bool bMatchesDefault = DefaultContainer != nullptr;
	for (int32 Idx = 0; Idx < Property->ArrayDim && bMatchesDefault; ++Idx)
	{
		const void* Value = Property->ContainerPtrToValuePtr<void>(Container, Idx);
		const void* ArchetypeValue = Property->ContainerPtrToValuePtr<void>(DefaultContainer, Idx);

		FString StringValue;
		Property->ExportText_Direct(StringValue, Value, Value, nullptr, PPF_ForDiff);
		if (ArchetypeValue)
		{
			FString DefaultStringValue;
			Property->ExportText_Direct(DefaultStringValue, ArchetypeValue, ArchetypeValue, nullptr, PPF_ForDiff);
			bMatchesDefault = DefaultStringValue.Equals(StringValue);
		}
	}

	if (bMatchesDefault)
	{
		return;
	}

	FPendingScope PropertyIdentifier(this, [Impl = this, Property]()
		{
			Impl->Writer->WriteIdentifierPrefix(Property->GetName());
		});
	
	if (Property->ArrayDim > 1)
	{
		// encode as a static array:
		FPendingScope ArrayIdentifier(this, [Impl = this]()
			{
				Impl->Writer->WriteArrayStartInline();
				Impl->Writer->WriteLineTerminator();
			},
			[Impl = this]()
			{
				Impl->Writer->WriteArrayEnd();
			}
		);
		for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
		{
			WriteValueToJson(
				Property->ContainerPtrToValuePtr<void>(Container, Index),
				DefaultContainer ? Property->ContainerPtrToValuePtr<void>(DefaultContainer, Index) : nullptr,
				Property);
		}
	}
	else
	{
		WriteValueToJson(
			Property->ContainerPtrToValuePtr<void>(Container),
			DefaultContainer ? Property->ContainerPtrToValuePtr<void>(DefaultContainer) : nullptr,
			Property);
	}
}

void FJsonStringifyImpl::WriteValueToJson(const void* Value, const void* DefaultValue, const FProperty* Property)
{
	bool bHandledAsAggregate = true;
	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		// write any tagged data for the struct - but be sure to delta serialize
		WriteStructToJson(Value, DefaultValue, StructProperty->Struct, StructProperty->Struct);
	}
	else if (CastField<FObjectProperty>(Property) || CastField<FClassProperty>(Property))
	{
		const UObject* Object = *(UObject**)Value;
		WriteObjectAsJsonToWriter(CurrentObject, Object, Writer);
	}
	// containers:
	else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		WriteArrayToJson(Value, ArrayProperty);
	}
	else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		WriteSetToJson(Value, SetProperty);
	}
	else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		WriteMapToJson(Value, MapProperty);
	}
	else if (const FOptionalProperty* OptionalProperty = CastField<FOptionalProperty>(Property))
	{
		WriteOptionalToJson(Value, OptionalProperty);
	}
	else
	{
		bHandledAsAggregate = false; // fall back to value serialization
	}

	if (!bHandledAsAggregate)
	{
		WriteIntrinsicToJson(Value, Property);
	}
}

void FJsonStringifyImpl::WriteIntrinsicToJson(const void* Value, const FProperty* Property)
{
	if (CurrentScope)
	{
		CurrentScope->Apply();
	}

	// bools are special, because of GetPropertyValue, which can apply a mask
	if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		bool bValue = BoolProp->GetPropertyValue(Value);
		Writer->WriteValueInline(bValue);
	}
	else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		// byte property is special because of the enum/byte duality:
		uint8 ValueAsIntrinsic = *(uint8*)Value;
		if (const UEnum* EnumDef = ByteProperty->Enum)
		{
			FString StringValue = EnumDef->GetAuthoredNameStringByValue(ValueAsIntrinsic);
			Writer->WriteValueInline(StringValue);
		}
		else
		{
			Writer->WriteValueInline(ValueAsIntrinsic);
		}
	}
	else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		// export enums as strings
		const UEnum* EnumDef = EnumProperty->GetEnum();
		FString StringValue = EnumDef->GetAuthoredNameStringByValue(
			EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value));
		Writer->WriteValueInline(StringValue);
	}
	else if( const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
	{
		FText ValueAsText = *(FText*)Value;
		Writer->WriteValueInline(ValueAsText);
	}
#define INTRINSIC_TABLE \
INTRINSIC_ENTRY(FInt8Property, int8) \
INTRINSIC_ENTRY(FInt16Property, int16) \
INTRINSIC_ENTRY(FIntProperty, int32) \
INTRINSIC_ENTRY(FInt64Property, int64) \
INTRINSIC_ENTRY(FUInt16Property, uint16) \
INTRINSIC_ENTRY(FUInt32Property, uint32) \
INTRINSIC_ENTRY(FUInt64Property, uint64) \
INTRINSIC_ENTRY(FFloatProperty, float) \
INTRINSIC_ENTRY(FDoubleProperty, double)

#define INTRINSIC_ENTRY(FPropertyType, intrinsic_type) \
	else if (const FPropertyType* As##intrinsic_type = CastField<FPropertyType>(Property)) \
	{ \
		intrinsic_type ValueAsIntrinsic = *(intrinsic_type*)Value; \
		Writer->WriteValueInline(ValueAsIntrinsic); \
	}
	INTRINSIC_TABLE
#undef INTRINSIC_ENTRY
#undef INTRINSIC_TABLE
	else
	{
		FString StringValue;
		Property->ExportText_Direct(StringValue, Value, Value, nullptr, PPF_None);
		Writer->WriteValueInline(StringValue);
	}
}

void FJsonStringifyImpl::WriteStructToJsonWithIdentifier(const TCHAR* Identifier, const void* StructInstance, const void* DefaultInstance, const UScriptStruct* Struct, const UScriptStruct* DefaultStruct)
{
	check(StructInstance && Struct);
	FPendingScope StructIdentifier(this, [Impl = this, Identifier]()
		{
			Impl->Writer->WriteIdentifierPrefix(Identifier);
		});

	WriteStructToJson(
		StructInstance,
		DefaultInstance,
		Struct,
		DefaultStruct);
}

void FJsonStringifyImpl::WriteStructToJson(const void* StructInstance, const void* DefaultInstance, const UScriptStruct* Struct, const UScriptStruct* DefaultStruct)
{
	FPendingScope ObjectStartIdentifier( 
		this, 
		[Impl = this]() { Impl->Writer->WriteObjectStartInline(); }, 
		[Impl = this]() { Impl->Writer->WriteObjectEnd(); }
		);

	for (TFieldIterator<FProperty> PropertyIt(Struct); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		WriteIdentifierAndValueToJson(
			StructInstance,
			Property->IsInContainer(const_cast<UScriptStruct*>(DefaultStruct)) ? DefaultInstance : nullptr,
			Property);
	}
}

void FJsonStringifyImpl::WriteArrayToJson(const void* ArrayInstance, const FArrayProperty* Array)
{
	if (CurrentScope)
	{
		CurrentScope->Apply();
	}

	Writer->WriteArrayStartInline();
	FScriptArrayHelper ArrayHelper(Array, ArrayInstance);
	const int32 ArrayNum = ArrayHelper.Num();
	if (ArrayNum != 0)
	{
		Writer->WriteLineTerminator();
	}
	for (int32 Index = 0; Index < ArrayNum; ++Index)
	{
		const void* Element = ArrayHelper.GetRawPtr(Index);
		const FProperty* Property = Array->Inner;
		WriteValueToJson(Element, nullptr, Property);
	}

	Writer->WriteArrayEnd();
}

void FJsonStringifyImpl::WriteSetToJson(const void* SetInstance, const FSetProperty* SetProperty)
{
	if (CurrentScope)
	{
		CurrentScope->Apply();
	}

	Writer->WriteArrayStartInline();
	FScriptSetHelper SetHelper(SetProperty, SetInstance);
	// Num() is number of elements, max index expensive to calculate, hence the odd loop:
	// @todo: use set iterator
	int32 SetNum = SetHelper.Num();
	if (SetNum != 0)
	{
		Writer->WriteLineTerminator();
	}
	for (int32 Index = 0; SetNum; ++Index)
	{
		if (!SetHelper.IsValidIndex(Index))
		{
			continue;
		}

		--SetNum;
		const void* Element = SetHelper.GetElementPtr(Index);
		const FProperty* Property = SetProperty->ElementProp;
		WriteValueToJson(Element, nullptr, Property);
	}

	Writer->WriteArrayEnd();
}

void FJsonStringifyImpl::WriteMapToJson(const void* MapInstance, const FMapProperty* MapProperty)
{
	if (CurrentScope)
	{
		CurrentScope->Apply();
	}

	Writer->WriteArrayStartInline();
	FScriptMapHelper MapHelper(MapProperty, MapInstance);
	// Num() is number of elements, max index expensive to calculate, hence the odd loop:
	// @todo: use map iterator
	int32 MapNum = MapHelper.Num();
	if (MapNum != 0)
	{
		Writer->WriteLineTerminator();
	}
	for (int32 Index = 0; MapNum; ++Index)
	{
		if (!MapHelper.IsValidIndex(Index))
		{
			continue;
		}

		--MapNum;
		const void* Key = MapHelper.GetKeyPtr(Index);
		const FProperty* KeyProperty = MapProperty->KeyProp;
		const void* Value = MapHelper.GetValuePtr(Index);
		const FProperty* ValueProperty = MapProperty->ValueProp;

		// age old question - how do you want to encode a tuple in json? I've
		// chosen named tuple:
		Writer->WriteObjectStartInline();
		Writer->WriteIdentifierPrefix(UE_JSON_TMAP_KEY_KEY_TCHAR);
		WriteValueToJson(Key, nullptr, KeyProperty);
		Writer->WriteIdentifierPrefix(UE_JSON_TMAP_VALUE_KEY_TCHAR);
		WriteValueToJson(Value, nullptr, ValueProperty);
		Writer->WriteObjectEnd();
	}

	Writer->WriteArrayEnd();
}

void FJsonStringifyImpl::WriteOptionalToJson(const void* OptionalInstnace, const FOptionalProperty* OptionalProperty)
{
	if (CurrentScope)
	{
		CurrentScope->Apply();
	}
	Writer->WriteObjectStartInline();
	if (const void* ValueAddress = static_cast<const void*>(OptionalProperty->GetValuePointerForReadOrReplaceIfSet(OptionalInstnace)))
	{
		Writer->WriteIdentifierPrefix(UE_JSON_OPTIONAL_VALUE_KEY_TCHAR);
		WriteValueToJson(ValueAddress, nullptr, OptionalProperty->GetValueProperty());
	}
	Writer->WriteObjectEnd();
}

TArray<uint8> FJsonStringifyImpl::SerialDataToJson(const UObject* Object, int32 InitialIndentLevel)
{
	FJsonStringifyArchive SerialData(Object, InitialIndentLevel, this, Versions, MemoryWriter.IsFilterEditorOnly());
	return SerialData.ToJson();
}

#if WITH_TEXT_ARCHIVE_SUPPORT
TArray<uint8> FJsonStringifyImpl::StructuredDataToJson(const UObject* Object, int32 InitialIndentLevel)
{
	FJsonStringifyStructuredArchive StructuredData(Object, InitialIndentLevel, this, Versions, MemoryWriter.IsFilterEditorOnly());
	return StructuredData.ToJson();
}
#endif // WITH_TEXT_ARCHIVE_SUPPORT

FJsonStringifyImpl::FPendingScope::FPendingScope(FJsonStringifyImpl* To, TFunctionRef<void()> Prefix)
	: Owner(To)
	, Outer(To->CurrentScope)
	, PendingPrefix(Prefix)
{
	To->CurrentScope = this;
}

FJsonStringifyImpl::FPendingScope::FPendingScope(FJsonStringifyImpl* To, TFunctionRef<void()> Prefix, TFunctionRef<void()> Postfix)
	: Owner(To)
	, Outer(To->CurrentScope)
	, PendingPrefix(Prefix)
	, PendingPostfix({Postfix})
{
	To->CurrentScope = this;
}

FJsonStringifyImpl::FPendingScope::~FPendingScope()
{
	if (bHasBeenApplied && PendingPostfix)
	{
		(*PendingPostfix)();
	}

	// scope left, if we've applied the prefix then we need to apply the postfix:
	Owner->CurrentScope = Outer;
}

void FJsonStringifyImpl::FPendingScope::Apply()
{
	// writer has decided to apply the scope, write all pending prefixes:
	if (bHasBeenApplied)
	{
		return;
	}

	bHasBeenApplied = true;
	if (Outer)
	{
		Outer->Apply();
	}
	PendingPrefix();
}

}

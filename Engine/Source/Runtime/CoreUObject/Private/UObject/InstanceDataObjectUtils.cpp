// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/InstanceDataObjectUtils.h"

#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"

#if WITH_EDITORONLY_DATA

#include "HAL/IConsoleManager.h"
#include "Misc/ReverseIterate.h"
#include "UObject/Class.h"
#include "UObject/EnumProperty.h"
#include "UObject/Field.h"
#include "UObject/Package.h"
#include "UObject/PropertyBagRepository.h"
#include "UObject/PropertyHelper.h"
#include "UObject/PropertyOptional.h"
#include "UObject/PropertyPathNameTree.h"
#include "UObject/UnrealType.h"

static const FName NAME_InitializedValues(ANSITEXTVIEW("_InitializedValues"));
static const FName NAME_SerializedValues(ANSITEXTVIEW("_SerializedValues"));

/** Type used for InstanceDataObject classes. */
class UInstanceDataObjectClass final : public UClass
{
public:
	DECLARE_CASTED_CLASS_INTRINSIC(UInstanceDataObjectClass, UClass, CLASS_Transient, TEXT("/Script/CoreUObject"), CASTCLASS_UClass)

	FByteProperty* InitializedValuesProperty = nullptr;
	FByteProperty* SerializedValuesProperty = nullptr;
};

IMPLEMENT_CORE_INTRINSIC_CLASS(UInstanceDataObjectClass, UClass,
{
});

/** Type used for InstanceDataObject structs to provide support for hashing and custom guids. */
class UInstanceDataObjectStruct final : public UScriptStruct
{
public:
	DECLARE_CASTED_CLASS_INTRINSIC(UInstanceDataObjectStruct, UScriptStruct, CLASS_Transient, TEXT("/Script/CoreUObject"), CASTCLASS_UScriptStruct)

	uint32 GetStructTypeHash(const void* Src) const final;
	FGuid GetCustomGuid() const final { return Guid; }

	FByteProperty* InitializedValuesProperty = nullptr;
	FByteProperty* SerializedValuesProperty = nullptr;
	FGuid Guid;
};

IMPLEMENT_CORE_INTRINSIC_CLASS(UInstanceDataObjectStruct, UScriptStruct,
{
});

uint32 UInstanceDataObjectStruct::GetStructTypeHash(const void* Src) const
{
	class FBoolHash
	{
	public:
		inline void Hash(bool bValue)
		{
			BoolValues = (BoolValues << 1) | (bValue ? 1 : 0);
			if ((++BoolCount & 63) == 0)
			{
				Flush();
			}
		}

		inline uint32 CalculateHash()
		{
			if (BoolCount & 63)
			{
				Flush();
			}
			return BoolHash;
		}

	private:
		inline void Flush()
		{
			BoolHash = HashCombineFast(BoolHash, GetTypeHash(BoolValues));
			BoolValues = 0;
		}

		uint32 BoolHash = 0;
		uint32 BoolCount = 0;
		uint64 BoolValues = 0;
	};

	FBoolHash BoolHash;
	uint32 ValueHash = 0;
	for (TFieldIterator<const FProperty> It(this); It; ++It)
	{
		if (It->GetFName() == NAME_InitializedValues || It->GetFName() == NAME_SerializedValues)
		{
			continue;
		}
		if (const FBoolProperty* BoolProperty = CastField<const FBoolProperty>(*It))
		{
			for (int32 I = 0; I < It->ArrayDim; ++I)
			{
				BoolHash.Hash(BoolProperty->GetPropertyValue_InContainer(Src, I));
			}
		}
		else if (ensure(It->HasAllPropertyFlags(CPF_HasGetValueTypeHash)))
		{
			for (int32 I = 0; I < It->ArrayDim; ++I)
			{
				uint32 Hash = It->GetValueTypeHash(It->ContainerPtrToValuePtr<void>(Src, I));
				ValueHash = HashCombineFast(ValueHash, Hash);
			}
		}
		else
		{
			ValueHash = HashCombineFast(ValueHash, It->ArrayDim);
		}
	}

	if (const uint32 Hash = BoolHash.CalculateHash())
	{
		ValueHash = HashCombineFast(ValueHash, Hash);
	}

	return ValueHash;
}

namespace UE
{
	static const FName NAME_DisplayName(ANSITEXTVIEW("DisplayName"));
	static const FName NAME_PresentAsTypeMetadata(ANSITEXTVIEW("PresentAsType"));
	static const FName NAME_IsLooseMetadata(ANSITEXTVIEW("IsLoose"));
	static const FName NAME_ContainsLoosePropertiesMetadata(ANSITEXTVIEW("ContainsLooseProperties"));
	static const FName NAME_VerseClass(ANSITEXTVIEW("VerseClass"));
	static const FName NAME_VerseDevice(ANSITEXTVIEW("VerseDevice_C"));
	static const FName NAME_IDOMapKey(ANSITEXTVIEW("Key"));
	static const FName NAME_IDOMapValue(ANSITEXTVIEW("Value"));

	bool bEnableIDOSupport = false;
	FAutoConsoleVariableRef EnableIDOSupportCVar(
		TEXT("IDO.Enable"),
		bEnableIDOSupport,
		TEXT("Allows property bags and IDOs to be created for supported classes.")
	);

	FString ExcludedLoosePropertyTypesVar = TEXT("VerseFunctionProperty");
	FAutoConsoleVariableRef ExcludedLoosePropertyTypesCVar(
		TEXT("IDO.ExcludedLoosePropertyTypes"),
		ExcludedLoosePropertyTypesVar,
		TEXT("Comma separated list of property types that will be excluded from loose properties in IDOs.")
	);

	static TSet<FString> GetExcludedLoosePropertyTypes()
	{
		TArray<FString> Result;
		ExcludedLoosePropertyTypesVar.ParseIntoArray(Result, TEXT(","));
		return TSet<FString>(Result);
	}

	bool IsInstanceDataObjectSupportEnabled()
	{
		return bEnableIDOSupport;
	}

	bool IsInstanceDataObjectSupportEnabled(const UObject* InObject)
	{
		if(!IsInstanceDataObjectSupportEnabled() || !InObject)
		{
			return false;
		}

		// Property bag placeholder objects are always enabled for IDO support
		if (UE::FPropertyBagRepository::IsPropertyBagPlaceholderObject(InObject))
		{
			return true;
		}

		//@todo FH: change to check trait when available or use config object
		const UClass* ObjClass = InObject->GetClass();
		if (!ObjClass->CanCreateInstanceDataObject())
		{
			return false;
		}

		// TODO: Temp! Remove with the conditions below.
		while (ObjClass && ObjClass->GetClass()->GetFName() != NAME_VerseClass)
		{
			ObjClass = ObjClass->GetSuperClass();
		}

		if (ObjClass)
		{
			// TODO: Temp! Don't generate IDOs for anything within a creative device
			for (UObject* Outer = InObject->GetOuter(); Outer; Outer = Outer->GetOuter())
			{
				if (Outer->GetClass()->GetFName() == NAME_VerseDevice)
				{
					return false;
				}
			}

			// TODO: Temp! Don't generate IDOs for anything transient that isn't a CDO.
			if (!InObject->HasAnyFlags(RF_ClassDefaultObject))
			{
				const UPackage* Package = InObject->GetPackage();
				if (!Package || Package->HasAnyFlags(RF_Transient) || Package == GetTransientPackage())
				{
					return false;
				}
			}
		}

		return true;
	}

	bool CanCreatePropertyBagPlaceholderTypeForImportClass(const UClass* ImportClass)
	{
		// @todo - Expand to other import types (e.g. prefab BPs) later; for now restricted to Verse class objects only.
		return ImportClass && ImportClass->GetFName() == NAME_VerseClass;
	}

	bool IsClassOfInstanceDataObjectClass(UStruct* Class)
	{
		return Class->IsA(UInstanceDataObjectClass::StaticClass()) || Class->IsA(UInstanceDataObjectStruct::StaticClass());
	}


	bool StructContainsLooseProperties(const UStruct* Struct)
	{
		return Struct->GetBoolMetaData(NAME_ContainsLoosePropertiesMetadata);
	}

	static UStruct* CreateInstanceDataObjectStructRec(const UClass* StructClass, UStruct* TemplateStruct, UObject* Outer, const FPropertyPathNameTree* PropertyTree, const FUnknownEnumNames* EnumNames);

	template <typename StructType>
	StructType* CreateInstanceDataObjectStructRec(UStruct* TemplateStruct, UObject* Outer, const FPropertyPathNameTree* PropertyTree, const FUnknownEnumNames* EnumNames)
	{
		return CastChecked<StructType>(CreateInstanceDataObjectStructRec(StructType::StaticClass(), TemplateStruct, Outer, PropertyTree, EnumNames));
	}

	UEnum* FindOrCreateInstanceDataObjectEnum(UEnum* TemplateEnum, UObject* Outer, const FProperty* Property, const FUnknownEnumNames* EnumNames)
	{
		if (!TemplateEnum || !EnumNames)
		{
			return TemplateEnum;
		}

		TArray<FName> UnknownNames;
		bool bHasFlags = false;

		// Use the original type name because the template may be a fallback enum or an IDO.
		FPropertyTypeName EnumTypeName = FindOriginalType(Property);
		if (EnumTypeName.IsEmpty())
		{
			FPropertyTypeNameBuilder Builder;
			Builder.AddPath(TemplateEnum);
			EnumTypeName = Builder.Build();
		}

		EnumNames->Find(EnumTypeName, UnknownNames, bHasFlags);
		if (UnknownNames.IsEmpty())
		{
			return TemplateEnum;
		}

		int64 MaxEnumValue = -1;
		int64 CombinedEnumValues = 0;
		TArray<TPair<FName, int64>> EnumValueNames;
		TStringBuilder<128> EnumName(InPlace, EnumTypeName.GetName());

		const auto MakeFullEnumName = [&EnumName, Form = TemplateEnum->GetCppForm()](FName Name) -> FName
		{
			if (Form == UEnum::ECppForm::Regular)
			{
				return Name;
			}
			return FName(WriteToString<128>(EnumName, TEXTVIEW("::"), Name));
		};

		const auto MakeNextEnumValue = [&MaxEnumValue, &CombinedEnumValues, bHasFlags]() -> int64
		{
			if (!bHasFlags)
			{
				return ++MaxEnumValue;
			}
			const int64 NextEnumValue = ~CombinedEnumValues & (CombinedEnumValues + 1);
			CombinedEnumValues |= NextEnumValue;
			return NextEnumValue;
		};

		// Copy existing values except for MAX.
		const bool bContainsExistingMax = TemplateEnum->ContainsExistingMax();
		for (int32 Index = 0, Count = TemplateEnum->NumEnums() - (bContainsExistingMax ? 1 : 0); Index < Count; ++Index)
		{
			FName EnumValueName = TemplateEnum->GetNameByIndex(Index);
			int64 EnumValue = TemplateEnum->GetValueByIndex(Index);
			EnumValueNames.Emplace(EnumValueName, EnumValue);
			MaxEnumValue = FMath::Max(MaxEnumValue, EnumValue);
			CombinedEnumValues |= EnumValue;
		}

		// Copy unknown names and assign values sequentially.
		for (FName UnknownName : UnknownNames)
		{
			EnumValueNames.Emplace(MakeFullEnumName(UnknownName), MakeNextEnumValue());
		}

		// Copy or create MAX with a new value.
		const FName MaxEnumName = bContainsExistingMax ? TemplateEnum->GetNameByIndex(TemplateEnum->NumEnums() - 1) : MakeFullEnumName("MAX");
		EnumValueNames.Emplace(MaxEnumName, bHasFlags ? CombinedEnumValues : MaxEnumValue);

		// Construct a transient type that impersonates the original type.
		const FName InstanceDataObjectName(WriteToString<128>(EnumName, TEXTVIEW("_InstanceDataObject")));
		UEnum* Enum = NewObject<UEnum>(Outer, MakeUniqueObjectName(Outer, UEnum::StaticClass(), InstanceDataObjectName));
		Enum->SetEnums(EnumValueNames, TemplateEnum->GetCppForm(), bHasFlags ? EEnumFlags::Flags : EEnumFlags::None, /*bAddMaxKeyIfMissing*/ false);
		Enum->SetMetaData(*WriteToString<32>(NAME_OriginalType), *WriteToString<128>(EnumTypeName));

		// TODO: Detect out-of-bounds values and increase the size of the underlying type accordingly.

		return Enum;
	}

	static FString UnmanglePropertyName(const FName MaybeMangledName, bool& bOutNameWasMangled)
	{
		FString Result = MaybeMangledName.ToString();
		if (Result.StartsWith(TEXTVIEW("__verse_0x")))
		{
			// chop "__verse_0x" (10 char) + CRC (8 char) + "_" (1 char)
			Result = Result.RightChop(19);
			bOutNameWasMangled = true;
		}
		else
		{
			bOutNameWasMangled = false;
		}
		return Result;
	}

	// recursively re-instances all structs contained by this property to include loose properties
	static void ConvertToInstanceDataObjectProperty(FProperty* Property, FPropertyTypeName PropertyType, UObject* Outer, const FPropertyPathNameTree* PropertyTree, const FUnknownEnumNames* EnumNames)
	{
		if (!Property->HasMetaData(NAME_DisplayName))
		{
			bool bNeedsDisplayName = false;
			FString DisplayName = UnmanglePropertyName(Property->GetFName(), bNeedsDisplayName);
			if (bNeedsDisplayName)
			{
				Property->SetMetaData(NAME_DisplayName, MoveTemp(DisplayName));
			}
		}

		if (FStructProperty* AsStructProperty = CastField<FStructProperty>(Property))
		{
			if (!AsStructProperty->Struct->UseNativeSerialization())
			{
				//@note: Transfer existing metadata over as we build the InstanceDataObject from the struct or it owners, if any, this is useful for testing purposes
				FString OriginalName;
				if (const FString* OriginalType = FindOriginalTypeName(AsStructProperty))
				{
					OriginalName = *OriginalType;
				}

				if (OriginalName.IsEmpty())
				{
					UE::FPropertyTypeNameBuilder OriginalNameBuilder;
					OriginalNameBuilder.AddPath(AsStructProperty->Struct);
					OriginalName = WriteToString<256>(OriginalNameBuilder.Build()).ToView();
				}

				UInstanceDataObjectStruct* Struct = CreateInstanceDataObjectStructRec<UInstanceDataObjectStruct>(AsStructProperty->Struct, Outer, PropertyTree, EnumNames);
				if (const FName StructGuidName = PropertyType.GetParameterName(1); !StructGuidName.IsNone())
				{
					FGuid::Parse(StructGuidName.ToString(), Struct->Guid);
				}
				AsStructProperty->Struct = Struct;
				AsStructProperty->SetMetaData(NAME_OriginalType, *OriginalName);
				AsStructProperty->SetMetaData(NAME_PresentAsTypeMetadata, *OriginalName);
				AsStructProperty->Struct->SetMetaData(NAME_OriginalType, *OriginalName);
				AsStructProperty->Struct->SetMetaData(NAME_PresentAsTypeMetadata, *OriginalName);
			}
		}
		else if (FByteProperty* AsByteProperty = CastField<FByteProperty>(Property))
		{
			AsByteProperty->Enum = FindOrCreateInstanceDataObjectEnum(AsByteProperty->Enum, Outer, Property, EnumNames);
		}
		else if (FEnumProperty* AsEnumProperty = CastField<FEnumProperty>(Property))
		{
			AsEnumProperty->SetEnumForImpersonation(FindOrCreateInstanceDataObjectEnum(AsEnumProperty->GetEnum(), Outer, Property, EnumNames));
		}
		else if (FArrayProperty* AsArrayProperty = CastField<FArrayProperty>(Property))
		{
			ConvertToInstanceDataObjectProperty(AsArrayProperty->Inner, PropertyType.GetParameter(0), Outer, PropertyTree, EnumNames);
		}
		else if (FSetProperty* AsSetProperty = CastField<FSetProperty>(Property))
		{
			ConvertToInstanceDataObjectProperty(AsSetProperty->ElementProp, PropertyType.GetParameter(0), Outer, PropertyTree, EnumNames);
		}
		else if (FMapProperty* AsMapProperty = CastField<FMapProperty>(Property))
		{
			const FPropertyPathNameTree* KeyTree = nullptr;
			const FPropertyPathNameTree* ValueTree = nullptr;
			if (PropertyTree)
			{
				FPropertyPathName Path;
				Path.Push({NAME_IDOMapKey});
				KeyTree = PropertyTree->Find(Path).GetSubTree();
				Path.Pop();
				Path.Push({NAME_IDOMapValue});
				ValueTree = PropertyTree->Find(Path).GetSubTree();
				Path.Pop();
			}

			ConvertToInstanceDataObjectProperty(AsMapProperty->KeyProp, PropertyType.GetParameter(0), Outer, KeyTree, EnumNames);
			ConvertToInstanceDataObjectProperty(AsMapProperty->ValueProp, PropertyType.GetParameter(1), Outer, ValueTree, EnumNames);
		}
		else if (FOptionalProperty* AsOptionalProperty = CastField<FOptionalProperty>(Property))
		{
			ConvertToInstanceDataObjectProperty(AsOptionalProperty->GetValueProperty(), PropertyType.GetParameter(0), Outer, PropertyTree, EnumNames);
		}
	}

	// recursively sets NAME_ContainsLoosePropertiesMetadata on all properties that contain loose properties
	static void TrySetContainsLoosePropertyMetadata(FProperty* Property)
	{
		const auto Helper = [](FProperty* Property, const FFieldVariant& Inner)
		{
			if (Inner.HasMetaData(NAME_ContainsLoosePropertiesMetadata))
			{
				Property->SetMetaData(NAME_ContainsLoosePropertiesMetadata, TEXT("True"));
			}
		};

		if (FStructProperty* AsStructProperty = CastField<FStructProperty>(Property))
		{
			Helper(AsStructProperty, AsStructProperty->Struct);
		}
		else if (FArrayProperty* AsArrayProperty = CastField<FArrayProperty>(Property))
		{
			TrySetContainsLoosePropertyMetadata(AsArrayProperty->Inner);
			Helper(AsArrayProperty, AsArrayProperty->Inner);
		}
		else if (FSetProperty* AsSetProperty = CastField<FSetProperty>(Property))
		{
			TrySetContainsLoosePropertyMetadata(AsSetProperty->ElementProp);
			Helper(AsSetProperty, AsSetProperty->ElementProp);
		}
		else if (FMapProperty* AsMapProperty = CastField<FMapProperty>(Property))
		{
			TrySetContainsLoosePropertyMetadata(AsMapProperty->KeyProp);
			Helper(AsMapProperty, AsMapProperty->KeyProp);
			TrySetContainsLoosePropertyMetadata(AsMapProperty->ValueProp);
			Helper(AsMapProperty, AsMapProperty->ValueProp);
		}
		else if (FOptionalProperty* AsOptionalProperty = CastField<FOptionalProperty>(Property))
		{
			TrySetContainsLoosePropertyMetadata(AsOptionalProperty->GetValueProperty());
			Helper(AsOptionalProperty, AsOptionalProperty->GetValueProperty());
		}

		if (Property->GetBoolMetaData(NAME_IsLooseMetadata) || Property->GetBoolMetaData(NAME_ContainsLoosePropertiesMetadata))
		{
			Property->GetOwnerStruct()->SetMetaData(NAME_ContainsLoosePropertiesMetadata, TEXT("True"));
		}
	}

	// recursively gives a property the metadata and flags of a loose property
	static void MarkPropertyAsLoose(FProperty* Property)
	{
		Property->SetMetaData(NAME_IsLooseMetadata, TEXT("True"));
		Property->SetPropertyFlags(CPF_Edit | CPF_EditConst);
		if (const FArrayProperty* AsArrayProperty = CastField<FArrayProperty>(Property))
		{
			MarkPropertyAsLoose(AsArrayProperty->Inner);
		}
		else if (const FSetProperty* AsSetProperty = CastField<FSetProperty>(Property))
{
			MarkPropertyAsLoose(AsSetProperty->ElementProp);
		}
		else if (const FMapProperty* AsMapProperty = CastField<FMapProperty>(Property))
		{
			MarkPropertyAsLoose(AsMapProperty->KeyProp);
			MarkPropertyAsLoose(AsMapProperty->ValueProp);
		}
		else if (const FOptionalProperty* AsOptionalProperty = CastField<FOptionalProperty>(Property))
		{
			MarkPropertyAsLoose(AsOptionalProperty->GetValueProperty());
		}
		else if (const FStructProperty* AsStructProperty = CastField<FStructProperty>(Property))
		{
			for (FProperty* InnerProperty : TFieldRange<FProperty>(AsStructProperty->Struct))
			{
				MarkPropertyAsLoose(InnerProperty);
			}
		}
		else if (const FObjectProperty* AsObjectProperty = CastField<FObjectProperty>(Property))
		{
			// Hack for now - the assumption is that IDOs are generated only for class types that impose this flag on all object properties.
			// There is currently an implicit assumption in the serialization logic that all inner properties have this flag set for containers.
			// Since this is a "loose" property, the underlying type will not explicitly tell us this, and there is no way to know from the tagged
			// property data stream if this flag was set when it was last serialized for the instance in question. So for now we just always set it.
			// 
			// Note that we are not currently including other related flags such as CPF_InstancedReference, CPF_ContainsInstancedReference, etc.
			// For the most part those have been relegated to object construction and loading paths. We are not instancing IDO types explicitly;
			// they are instead serving as an editable data archetype for the actual instance, whose type may impose some post-initialization
			// effects on that data as part of the construction/serialization path.
			// 
			// @todo - Remove if/when this flag is no longer required to signal whether this value is to be resolved via a subobject instancing graph.
			Property->SetPropertyFlags(CPF_PersistentInstance| CPF_InstancedReference);
		}
	}

	// constructs an InstanceDataObject struct by merging the properties in 
	static UStruct* CreateInstanceDataObjectStructRec(const UClass* StructClass, UStruct* TemplateStruct, UObject* Outer, const FPropertyPathNameTree* PropertyTree, const FUnknownEnumNames* EnumNames)
	{
		TSet<FPropertyPathName> SuperPropertyPathsFromTree;

		// UClass is required to inherit from UObject
		UStruct* Super = StructClass->IsChildOf<UClass>() ? UObject::StaticClass() : nullptr;

		if (TemplateStruct)
		{
			{
				const FName SuperName(WriteToString<128>(TemplateStruct->GetName(), TEXTVIEW("_Super")));
				const UClass* SuperStructClass = StructClass->GetSuperClass();
				UStruct* NewSuper = NewObject<UStruct>(Outer, SuperStructClass, MakeUniqueObjectName(nullptr, SuperStructClass, SuperName));
				NewSuper->SetSuperStruct(Super);
				Super = NewSuper;
			}

			// Gather properties for Super Struct
			TArray<FProperty*> SuperProperties;
			for (const FProperty* TemplateProperty : TFieldRange<FProperty>(TemplateStruct))
			{
				FProperty* SuperProperty = CastFieldChecked<FProperty>(FField::Duplicate(TemplateProperty, Super));
				SuperProperties.Add(SuperProperty);

				FField::CopyMetaData(TemplateProperty, SuperProperty);

				FPropertyTypeName Type;
				{
					FPropertyTypeNameBuilder TypeBuilder;
					TemplateProperty->SaveTypeName(TypeBuilder);
					Type = TypeBuilder.Build();
				}

				// Find the sub-tree containing unknown properties for this template property.
				const FPropertyPathNameTree* SubTree = nullptr;
				if (PropertyTree)
				{
					FPropertyPathName Path;
					Path.Push({TemplateProperty->GetFName(), Type});
					if (FPropertyPathNameTree::FConstNode Node = PropertyTree->Find(Path))
					{
						SubTree = Node.GetSubTree();
						SuperPropertyPathsFromTree.Add(MoveTemp(Path));
					}
				}

				ConvertToInstanceDataObjectProperty(SuperProperty, Type, Super, SubTree, EnumNames);
				TrySetContainsLoosePropertyMetadata(SuperProperty);
			}

			// AddCppProperty expects reverse property order for StaticLink to work correctly
			for (FProperty* Property : ReverseIterate(SuperProperties))
			{
				Super->AddCppProperty(Property);
			}
			Super->Bind();
			Super->StaticLink(/*bRelinkExistingProperties*/true);
		}

		const FName InstanceDataObjectName = (TemplateStruct) ? FName(WriteToString<128>(TemplateStruct->GetName(), TEXTVIEW("_InstanceDataObject"))) : FName(TEXTVIEW("InstanceDataObject"));
		UStruct* Result = NewObject<UStruct>(Outer, StructClass, MakeUniqueObjectName(Outer, StructClass, InstanceDataObjectName));
		Result->SetSuperStruct(Super);

		// inherit ContainsLooseProperties metadata
		if (Super && Super->GetBoolMetaData(NAME_ContainsLoosePropertiesMetadata))
		{
			Result->SetMetaData(NAME_ContainsLoosePropertiesMetadata, TEXT("True"));
		}

		TSet<FString> ExcludedLoosePropertyTypes = GetExcludedLoosePropertyTypes();

		// Gather "loose" properties for child Struct
		TArray<FProperty*> LooseInstanceDataObjectProperties;
		if (PropertyTree)
		{
			for (FPropertyPathNameTree::FConstIterator It = PropertyTree->CreateConstIterator(); It; ++It)
			{
				FName Name = It.GetName();
				if (Name == NAME_InitializedValues || Name == NAME_SerializedValues)
				{
					// In rare cases, these hidden properties will get serialized even though they are transient.
					// Ignore them here since they are generated below.
					continue;
				}
				FPropertyTypeName Type = It.GetType();
				FPropertyPathName Path;
				Path.Push({Name, Type});
				if (!SuperPropertyPathsFromTree.Contains(Path))
				{
					// Construct a property from the type and try to use it to serialize the value.
					FField* Field = FField::TryConstruct(Type.GetName(), Result, Name, RF_NoFlags);
					if (FProperty* Property = CastField<FProperty>(Field); Property && Property->LoadTypeName(Type, It.GetNode().GetTag()))
					{
						if (ExcludedLoosePropertyTypes.Contains(Property->GetClass()->GetName()))
						{
							// skip loose types that have been explicitly excluded from IDOs
							continue;
						}
						ConvertToInstanceDataObjectProperty(Property, Type, Result, It.GetNode().GetSubTree(), EnumNames);
						MarkPropertyAsLoose(Property);	// note: make sure not to mark until AFTER conversion, as this can mutate property flags on nested struct fields
						TrySetContainsLoosePropertyMetadata(Property);
						LooseInstanceDataObjectProperties.Add(Property);
						continue;
					}
					delete Field;
				}
			}
		}

		// Add hidden byte array properties to record whether its sibling properties were initialized or set by serialization.
		FByteProperty* InitializedValuesProperty = CastFieldChecked<FByteProperty>(FByteProperty::Construct(Result, NAME_InitializedValues, RF_Transient | RF_MarkAsNative));
		FByteProperty* SerializedValuesProperty = CastFieldChecked<FByteProperty>(FByteProperty::Construct(Result, NAME_SerializedValues, RF_Transient | RF_MarkAsNative));
		{
			InitializedValuesProperty->SetPropertyFlags(CPF_Transient | CPF_EditorOnly | CPF_SkipSerialization | CPF_NativeAccessSpecifierPrivate);
			SerializedValuesProperty->SetPropertyFlags(CPF_Transient | CPF_EditorOnly | CPF_SkipSerialization | CPF_NativeAccessSpecifierPrivate);
			Result->AddCppProperty(InitializedValuesProperty);
			Result->AddCppProperty(SerializedValuesProperty);
		}

		// Store generated properties to avoid scanning every property to find it when it is needed.
		if (UInstanceDataObjectClass* IdoClass = Cast<UInstanceDataObjectClass>(Result))
		{
			IdoClass->InitializedValuesProperty = InitializedValuesProperty;
			IdoClass->SerializedValuesProperty = SerializedValuesProperty;
		}
		else if (UInstanceDataObjectStruct* IdoStruct = Cast<UInstanceDataObjectStruct>(Result))
		{
			IdoStruct->InitializedValuesProperty = InitializedValuesProperty;
			IdoStruct->SerializedValuesProperty = SerializedValuesProperty;
		}

		// AddCppProperty expects reverse property order for StaticLink to work correctly
		for (FProperty* Property : ReverseIterate(LooseInstanceDataObjectProperties))
		{
			Result->AddCppProperty(Property);
		}

		// Count properties and set the size of the array of flags.
		int32 PropertyCount = -2; // Start at -2 to exclude the two hidden properties.
		for (TFieldIterator<FProperty> It(Result); It; ++It)
		{
			PropertyCount += It->ArrayDim;
		}
		const int32 PropertyCountBytes = FMath::Max(1, FMath::DivideAndRoundUp(PropertyCount, 8));
		InitializedValuesProperty->ArrayDim = PropertyCountBytes;
		SerializedValuesProperty->ArrayDim = PropertyCountBytes;

		Result->Bind();
		Result->StaticLink(/*bRelinkExistingProperties*/true);
		return Result;
	}

	struct FSerializingDefaultsScope
	{
		UE_NONCOPYABLE(FSerializingDefaultsScope);

		inline FSerializingDefaultsScope(FArchive& Ar, const UObject* Object)
		{
			if (Object->HasAnyFlags(RF_ClassDefaultObject))
			{
				Archive = &Ar;
				Archive->StartSerializingDefaults();
			}
		}

		inline ~FSerializingDefaultsScope()
		{
			if (Archive)
			{
				Archive->StopSerializingDefaults();
			}
		}

		FArchive* Archive = nullptr;
	};

	void CopyTaggedProperties(const UObject* Source, UObject* Dest)
	{
		FUObjectSerializeContext* SerializeContext = FUObjectThreadContext::Get().GetSerializeContext();
		TGuardValue<bool> ImpersonatePropertiesScope(SerializeContext->bImpersonateProperties, true);
		// don't mark properties as set by serialization when performing copy
		TGuardValue<bool> ScopedTrackSerializedProperties(SerializeContext->bTrackSerializedProperties, false);
		TGuardValue<bool> ScopedTrackUnknownProperties(SerializeContext->bTrackUnknownProperties, false);

		TArray<uint8> Buffer;
		Buffer.Reserve(Source->GetClass()->GetStructureSize());

		FObjectWriter Writer(Buffer);
		FSerializingDefaultsScope WriterDefaultsScope(Writer, Source);
		Writer.ArNoDelta = true;
		Source->GetClass()->SerializeTaggedProperties(Writer, (uint8*)Source, Source->GetClass(), nullptr);

		FObjectReader Reader(Buffer);
		FSerializingDefaultsScope ReaderDefaultsScope(Reader, Dest);
		Reader.ArMergeOverrides = true;
		Dest->GetClass()->SerializeTaggedProperties(Reader, (uint8*)Dest, Dest->GetClass(), nullptr);
	}

	static void SetClassFlags(UClass* IDOClass, const UClass* OwnerClass)
	{
		// always set
		IDOClass->AssembleReferenceTokenStream();
		IDOClass->ClassFlags |= CLASS_NotPlaceable | CLASS_Hidden | CLASS_HideDropDown;
		
		// copy flags from OwnerClass
		IDOClass->ClassFlags |= OwnerClass->ClassFlags & (
			CLASS_EditInlineNew | CLASS_CollapseCategories | CLASS_Const | CLASS_CompiledFromBlueprint | CLASS_HasInstancedReference);
	}

	UClass* CreateInstanceDataObjectClass(const FPropertyPathNameTree* PropertyTree, const FUnknownEnumNames* EnumNames, UClass* OwnerClass, UObject* Outer)
	{
		UClass* Result = CreateInstanceDataObjectStructRec<UInstanceDataObjectClass>(OwnerClass, Outer, PropertyTree, EnumNames);
		if (const FString& DisplayName = OwnerClass->GetMetaData(NAME_DisplayName); !DisplayName.IsEmpty())
		{
			Result->SetMetaData(NAME_DisplayName, *DisplayName);
		}

		SetClassFlags(Result, OwnerClass);

		const UObject* OwnerCDO = OwnerClass->GetDefaultObject();
		UObject* ResultCDO = Result->GetDefaultObject();
		if (ensure(OwnerCDO && ResultCDO))
		{
			CopyTaggedProperties(OwnerCDO, ResultCDO);
		}
		return Result;
	}

	static const FByteProperty* FindSerializedValuesProperty(const UStruct* Struct)
	{
		if (const UInstanceDataObjectClass* IdoClass = Cast<UInstanceDataObjectClass>(Struct))
		{
			return IdoClass->SerializedValuesProperty;
		}
		if (const UInstanceDataObjectStruct* IdoStruct = Cast<UInstanceDataObjectStruct>(Struct))
		{
			return IdoStruct->SerializedValuesProperty;
		}
		return CastField<FByteProperty>(Struct->FindPropertyByName(NAME_SerializedValues));
	}

	void MarkPropertyValueSerialized(const UStruct* Struct, void* StructData, const FProperty* Property, int32 ArrayIndex)
	{
		if (const FByteProperty* SerializedValuesProperty = FindSerializedValuesProperty(Struct))
		{
			const int32 PropertyIndex = Property->GetIndexInOwner() + ArrayIndex;
			const int32 ByteIndex = PropertyIndex / 8;
			const int32 BitOffset = PropertyIndex % 8;
			if (ByteIndex < SerializedValuesProperty->ArrayDim)
			{
				uint8* PropertyDataPtr = SerializedValuesProperty->ContainerPtrToValuePtr<uint8>(StructData, ByteIndex);
				*PropertyDataPtr |= (1 << BitOffset);
			}
		}
	}

	bool WasPropertyValueSerialized(const UStruct* Struct, const void* StructData, const FProperty* Property, int32 ArrayIndex)
	{
		if (const FByteProperty* SerializedValuesProperty = FindSerializedValuesProperty(Struct))
		{
			const int32 PropertyIndex = Property->GetIndexInOwner() + ArrayIndex;
			const int32 ByteIndex = PropertyIndex / 8;
			const int32 BitOffset = PropertyIndex % 8;
			if (ByteIndex < SerializedValuesProperty->ArrayDim)
			{
				const uint8* PropertyDataPtr = SerializedValuesProperty->ContainerPtrToValuePtr<uint8>(StructData, ByteIndex);
				return (*PropertyDataPtr & (1 << BitOffset)) != 0;
			}
		}
		return false;
	}

	void CopyPropertyValueSerializedData(const FFieldVariant& OldField, void* OldDataPtr, const FFieldVariant& NewField, void* NewDataPtr)
	{
		if (const FStructProperty* OldAsStructProperty = OldField.Get<FStructProperty>())
		{
			const FStructProperty* NewAsStructProperty = NewField.Get<FStructProperty>();
			checkf(NewAsStructProperty, TEXT("Type mismatch between OldField and NewField. Expected FStructProperty"));
			CopyPropertyValueSerializedData(OldAsStructProperty->Struct, OldDataPtr, NewAsStructProperty->Struct, NewDataPtr);
		}
		else if (const FArrayProperty* OldAsArrayProperty = OldField.Get<FArrayProperty>())
		{
			const FArrayProperty* NewAsArrayProperty = NewField.Get<FArrayProperty>();
			checkf(NewAsArrayProperty, TEXT("Type mismatch between OldField and NewField. Expected FArrayProperty"));
			
			FScriptArrayHelper OldArrayHelper(OldAsArrayProperty, OldDataPtr);
			FScriptArrayHelper NewArrayHelper(NewAsArrayProperty, NewDataPtr);
			for (int32 ArrayIndex = 0; ArrayIndex < OldArrayHelper.Num(); ++ArrayIndex)
			{
				if (NewArrayHelper.IsValidIndex(ArrayIndex))
				{
					CopyPropertyValueSerializedData(
						OldAsArrayProperty->Inner, OldArrayHelper.GetElementPtr(ArrayIndex),
						NewAsArrayProperty->Inner, NewArrayHelper.GetElementPtr(ArrayIndex));
				}
			}
		}
		else if (const FSetProperty* OldAsSetProperty = OldField.Get<FSetProperty>())
		{
			const FSetProperty* NewAsSetProperty = NewField.Get<FSetProperty>();
			checkf(NewAsSetProperty, TEXT("Type mismatch between OldField and NewField. Expected FSetProperty"));
			
			FScriptSetHelper OldSetHelper(OldAsSetProperty, OldDataPtr);
			FScriptSetHelper NewSetHelper(NewAsSetProperty, NewDataPtr);
			FScriptSetHelper::FIterator OldItr = OldSetHelper.CreateIterator();
			FScriptSetHelper::FIterator NewItr = NewSetHelper.CreateIterator();
			
			for (; OldItr && NewItr; ++OldItr, ++NewItr)
			{
				CopyPropertyValueSerializedData(
					OldAsSetProperty->ElementProp, OldSetHelper.GetElementPtr(OldItr),
					NewAsSetProperty->ElementProp, NewSetHelper.GetElementPtr(NewItr));
			}
		}
		else if (const FMapProperty* OldAsMapProperty = OldField.Get<FMapProperty>())
		{
			const FMapProperty* NewAsMapProperty = NewField.Get<FMapProperty>();
			checkf(NewAsMapProperty, TEXT("Type mismatch between OldField and NewField. Expected FMapProperty"));
			
			FScriptMapHelper OldMapHelper(OldAsMapProperty, OldDataPtr);
			FScriptMapHelper NewMapHelper(NewAsMapProperty, NewDataPtr);
			FScriptMapHelper::FIterator OldItr = OldMapHelper.CreateIterator();
			FScriptMapHelper::FIterator NewItr = NewMapHelper.CreateIterator();
			
			for (; OldItr && NewItr; ++OldItr, ++NewItr)
			{
				CopyPropertyValueSerializedData(
					OldAsMapProperty->KeyProp, OldMapHelper.GetKeyPtr(OldItr),
					NewAsMapProperty->KeyProp, NewMapHelper.GetKeyPtr(NewItr));
				CopyPropertyValueSerializedData(
					OldAsMapProperty->ValueProp, OldMapHelper.GetValuePtr(OldItr),
					NewAsMapProperty->ValueProp, NewMapHelper.GetValuePtr(NewItr));
			}
		}
		else if (UStruct* OldAsStruct = OldField.Get<UStruct>())
		{
			const UStruct* NewAsStruct = NewField.Get<UStruct>();
			checkf(NewAsStruct, TEXT("Type mismatch between OldField and NewField. Expected UStruct"));

			auto FindMatchingProperty = [](const UStruct* Struct, const FProperty* Property) -> const FProperty*
			{
				for (const FProperty* StructProperty : TFieldRange<FProperty>(Struct))
				{
					if (StructProperty->GetFName() == Property->GetFName() && StructProperty->GetID() == Property->GetID())
					{
						return StructProperty;
					}
				}
				return nullptr;
			};

			// clear existing set-flags first
			if (const FByteProperty* SerializedValuesProperty = FindSerializedValuesProperty(NewAsStruct))
			{
				SerializedValuesProperty->InitializeValue_InContainer(NewDataPtr);
			}
			
			for (const FProperty* OldSubProperty : TFieldRange<FProperty>(OldAsStruct))
			{
				if (const FProperty* NewSubProperty = FindMatchingProperty(NewAsStruct, OldSubProperty))
				{
					for (int32 ArrayIndex = 0; ArrayIndex < FMath::Min(OldSubProperty->ArrayDim, NewSubProperty->ArrayDim); ++ArrayIndex)
					{
						// copy set flags to new struct instance
						if (WasPropertyValueSerialized(OldAsStruct, OldDataPtr, OldSubProperty, ArrayIndex))
						{
							MarkPropertyValueSerialized(NewAsStruct, NewDataPtr, NewSubProperty, ArrayIndex);
						}
						else if (NewSubProperty->GetBoolMetaData(NAME_IsLooseMetadata))
						{
							// loose properties should be marked as serialized regardless of whether the old struct marked them as such
							MarkPropertyValueSerialized(NewAsStruct, NewDataPtr, NewSubProperty, ArrayIndex);
						}
					
						// recurse
						CopyPropertyValueSerializedData(
							OldSubProperty, OldSubProperty->ContainerPtrToValuePtr<void>(OldDataPtr, ArrayIndex),
							NewSubProperty, NewSubProperty->ContainerPtrToValuePtr<void>(NewDataPtr, ArrayIndex));
					}
				}
			}
		}
	}

	static const FByteProperty* FindInitializedValuesProperty(const UStruct* Struct)
	{
		if (const UInstanceDataObjectClass* IdoClass = Cast<UInstanceDataObjectClass>(Struct))
		{
			return IdoClass->InitializedValuesProperty;
		}
		if (const UInstanceDataObjectStruct* IdoStruct = Cast<UInstanceDataObjectStruct>(Struct))
		{
			return IdoStruct->InitializedValuesProperty;
		}
		return CastField<FByteProperty>(Struct->FindPropertyByName(NAME_InitializedValues));
	}

	static void SetPropertyValueInitializedFlag(const UStruct* Struct, void* StructData, const FProperty* Property, int32 ArrayIndex, bool bValue)
	{
		if (const FByteProperty* InitializedValuesProperty = FindInitializedValuesProperty(Struct))
		{
			const int32 PropertyIndex = Property->GetIndexInOwner() + ArrayIndex;
			const int32 ByteIndex = PropertyIndex / 8;
			const int32 BitOffset = PropertyIndex % 8;
			if (ByteIndex < InitializedValuesProperty->ArrayDim)
			{
				uint8* PropertyDataPtr = InitializedValuesProperty->ContainerPtrToValuePtr<uint8>(StructData, ByteIndex);
				if (bValue)
				{
					*PropertyDataPtr |= (1 << BitOffset);
				}
				else
				{
					*PropertyDataPtr &= ~(1 << BitOffset);
				}
			}
		}
	}

	bool IsPropertyValueInitialized(const UStruct* Struct, void* StructData, const FProperty* Property, int32 ArrayIndex)
	{
		if (const FByteProperty* InitializedValuesProperty = FindInitializedValuesProperty(Struct))
		{
			const int32 PropertyIndex = Property->GetIndexInOwner() + ArrayIndex;
			const int32 ByteIndex = PropertyIndex / 8;
			const int32 BitOffset = PropertyIndex % 8;
			if (ByteIndex < InitializedValuesProperty->ArrayDim)
			{
				const uint8* PropertyDataPtr = InitializedValuesProperty->ContainerPtrToValuePtr<uint8>(StructData, ByteIndex);
				return (*PropertyDataPtr & (1 << BitOffset)) != 0;
			}
		}
		return false;
	}

	void SetPropertyValueInitialized(const UStruct* Struct, void* StructData, const FProperty* Property, int32 ArrayIndex)
	{
		SetPropertyValueInitializedFlag(Struct, StructData, Property, ArrayIndex, /*bValue*/ true);
	}

	void ClearPropertyValueInitialized(const UStruct* Struct, void* StructData, const FProperty* Property, int32 ArrayIndex)
	{
		SetPropertyValueInitializedFlag(Struct, StructData, Property, ArrayIndex, /*bValue*/ false);
	}

	void ResetPropertyValueInitialized(const UStruct* Struct, void* StructData)
	{
		if (const FByteProperty* InitializedValuesProperty = FindInitializedValuesProperty(Struct))
		{
			uint8* PropertyDataPtr = InitializedValuesProperty->ContainerPtrToValuePtr<uint8>(StructData);
			FMemory::Memzero(PropertyDataPtr, InitializedValuesProperty->ArrayDim);
		}
	}

} // UE

#endif // WITH_EDITORONLY_DATA

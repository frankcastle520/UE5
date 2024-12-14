// Copyright Epic Games, Inc. All Rights Reserved.

#include "InstanceDataObjectUtilsTest.h"

#if WITH_TESTS && WITH_EDITORONLY_DATA

#include "Logging/LogScopedVerbosityOverride.h"
#include "Misc/ScopeExit.h"
#include "Serialization/Formatters/BinaryArchiveFormatter.h"
#include "Serialization/Formatters/JsonArchiveInputFormatter.h"
#include "Serialization/Formatters/JsonArchiveOutputFormatter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchive.h"
#include "Tests/TestHarnessAdapter.h"
#include "UObject/InstanceDataObjectUtils.h"
#include "UObject/Package.h"
#include "UObject/PropertyBagRepository.h"
#include "UObject/PropertyHelper.h"
#include "UObject/PropertyPathNameTree.h"
#include "UObject/UObjectThreadContext.h"

namespace UE
{

TEST_CASE_NAMED(FInstanceDataObjectUtilsTest, "CoreUObject::Serialization::InstanceDataObjectUtils", "[CoreUObject][EngineFilter]")
{
	UTestInstanceDataObjectClass* BaseObject = NewObject<UTestInstanceDataObjectClass>();

	FPropertyBagRepository& Repo = FPropertyBagRepository::Get();
	Repo.AddUnknownEnumName(BaseObject, StaticEnum<ETestInstanceDataObjectBird>(), {}, "TIDOB_Pigeon");
	Repo.AddUnknownEnumName(BaseObject, StaticEnum<ETestInstanceDataObjectGrain::Type>(), {}, "Rye");
	Repo.AddUnknownEnumName(BaseObject, StaticEnum<ETestInstanceDataObjectFruit>(), {}, "Cherry");
	Repo.AddUnknownEnumName(BaseObject, StaticEnum<ETestInstanceDataObjectDirection>(), {}, "Up");
	Repo.AddUnknownEnumName(BaseObject, StaticEnum<ETestInstanceDataObjectFullFlags>(), {}, "Flag3");
	Repo.AddUnknownEnumName(BaseObject, StaticEnum<ETestInstanceDataObjectFullFlags>(), {}, "Flag8");
	Repo.AddUnknownEnumName(BaseObject, StaticEnum<ETestInstanceDataObjectFullFlags>(), {}, "Flag9");

	UClass* TestClass = CreateInstanceDataObjectClass(nullptr, Repo.FindUnknownEnumNames(BaseObject), BaseObject->GetClass(), BaseObject->GetOuter());

	FIntProperty* Int32Property = FindFProperty<FIntProperty>(TestClass, TEXT("Int32"));
	FStructProperty* StructProperty = FindFProperty<FStructProperty>(TestClass, TEXT("Struct"));
	REQUIRE(Int32Property);
	REQUIRE(StructProperty);
	REQUIRE(StructProperty->Struct);

	FIntProperty* AProperty = FindFProperty<FIntProperty>(StructProperty->Struct, TEXT("A"));
	FIntProperty* BProperty = FindFProperty<FIntProperty>(StructProperty->Struct, TEXT("B"));
	FIntProperty* CProperty = FindFProperty<FIntProperty>(StructProperty->Struct, TEXT("C"));
	FIntProperty* DProperty = FindFProperty<FIntProperty>(StructProperty->Struct, TEXT("D"));
	REQUIRE(AProperty);
	REQUIRE(BProperty);
	REQUIRE(CProperty);
	REQUIRE(DProperty);

	FByteProperty* BirdProperty = FindFProperty<FByteProperty>(StructProperty->Struct, TEXT("Bird"));
	FByteProperty* GrainProperty = FindFProperty<FByteProperty>(StructProperty->Struct, TEXT("Grain"));
	FEnumProperty* FruitProperty = FindFProperty<FEnumProperty>(StructProperty->Struct, TEXT("Fruit"));
	FEnumProperty* DirectionProperty = FindFProperty<FEnumProperty>(StructProperty->Struct, TEXT("Direction"));
	FEnumProperty* FullFlagsProperty = FindFProperty<FEnumProperty>(StructProperty->Struct, TEXT("FullFlags"));
	REQUIRE(BirdProperty);
	REQUIRE(GrainProperty);
	REQUIRE(FruitProperty);
	REQUIRE(DirectionProperty);
	REQUIRE(FullFlagsProperty);

	CHECK(BirdProperty->Enum->GetIndexByName("TIDOB_Pigeon") != INDEX_NONE);
	CHECK(GrainProperty->Enum->GetIndexByName("ETestInstanceDataObjectGrain::Rye") != INDEX_NONE);
	CHECK(FruitProperty->GetEnum()->GetIndexByName("ETestInstanceDataObjectFruit::Cherry") != INDEX_NONE);
	CHECK(DirectionProperty->GetEnum()->GetIndexByName("ETestInstanceDataObjectDirection::Up") != INDEX_NONE);
	CHECK(FullFlagsProperty->GetEnum()->GetIndexByName("ETestInstanceDataObjectFullFlags::Flag3") != INDEX_NONE);
	CHECK(FullFlagsProperty->GetEnum()->GetIndexByName("ETestInstanceDataObjectFullFlags::Flag8") != INDEX_NONE);
	CHECK(FullFlagsProperty->GetEnum()->GetIndexByName("ETestInstanceDataObjectFullFlags::Flag9") != INDEX_NONE);
	CHECK(FullFlagsProperty->GetEnum()->GetMaxEnumValue() == 0b11'1111'1111);

	FName TestObjectName = MakeUniqueObjectName(nullptr, TestClass, FName(WriteToString<128>(TestClass->GetFName(), TEXT("_Instance"))));
	UObject* Owner = NewObject<UObject>(GetTransientPackage(), TestClass, TestObjectName);
	void* StructData = StructProperty->ContainerPtrToValuePtr<void>(Owner);

	CHECK_FALSE(WasPropertyValueSerialized(TestClass, Owner, StructProperty));
	MarkPropertyValueSerialized(TestClass, Owner, StructProperty);
	CHECK(WasPropertyValueSerialized(TestClass, Owner, StructProperty));
	CHECK_FALSE(WasPropertyValueSerialized(TestClass, Owner, Int32Property));

	CHECK_FALSE(IsPropertyValueInitialized(TestClass, Owner, StructProperty));
	SetPropertyValueInitialized(TestClass, Owner, StructProperty);
	CHECK(IsPropertyValueInitialized(TestClass, Owner, StructProperty));
	ClearPropertyValueInitialized(TestClass, Owner, StructProperty);
	CHECK_FALSE(IsPropertyValueInitialized(TestClass, Owner, StructProperty));

	CHECK_FALSE(WasPropertyValueSerialized(StructProperty->Struct, StructData, AProperty));
	MarkPropertyValueSerialized(StructProperty->Struct, StructData, AProperty);
	CHECK(WasPropertyValueSerialized(StructProperty->Struct, StructData, AProperty));
	CHECK_FALSE(WasPropertyValueSerialized(StructProperty->Struct, StructData, BProperty));
	MarkPropertyValueSerialized(StructProperty->Struct, StructData, BProperty);
	CHECK(WasPropertyValueSerialized(StructProperty->Struct, StructData, BProperty));
	CHECK_FALSE(WasPropertyValueSerialized(StructProperty->Struct, StructData, CProperty));
	CHECK_FALSE(WasPropertyValueSerialized(StructProperty->Struct, StructData, DProperty));

	CHECK_FALSE(IsPropertyValueInitialized(StructProperty->Struct, StructData, AProperty));
	SetPropertyValueInitialized(StructProperty->Struct, StructData, AProperty);
	CHECK(IsPropertyValueInitialized(StructProperty->Struct, StructData, AProperty));
	CHECK_FALSE(IsPropertyValueInitialized(StructProperty->Struct, StructData, BProperty));
	SetPropertyValueInitialized(StructProperty->Struct, StructData, BProperty);
	CHECK(IsPropertyValueInitialized(StructProperty->Struct, StructData, BProperty));
	CHECK_FALSE(IsPropertyValueInitialized(StructProperty->Struct, StructData, CProperty));
	CHECK_FALSE(IsPropertyValueInitialized(StructProperty->Struct, StructData, DProperty));
	ClearPropertyValueInitialized(StructProperty->Struct, StructData, AProperty);
	ClearPropertyValueInitialized(StructProperty->Struct, StructData, BProperty);
	CHECK_FALSE(IsPropertyValueInitialized(StructProperty->Struct, StructData, AProperty));
	CHECK_FALSE(IsPropertyValueInitialized(StructProperty->Struct, StructData, BProperty));

	SetPropertyValueInitialized(StructProperty->Struct, StructData, AProperty);
	SetPropertyValueInitialized(StructProperty->Struct, StructData, DProperty);
	ResetPropertyValueInitialized(StructProperty->Struct, StructData);
	CHECK_FALSE(IsPropertyValueInitialized(StructProperty->Struct, StructData, AProperty));
	CHECK_FALSE(IsPropertyValueInitialized(StructProperty->Struct, StructData, BProperty));
	CHECK_FALSE(IsPropertyValueInitialized(StructProperty->Struct, StructData, CProperty));
	CHECK_FALSE(IsPropertyValueInitialized(StructProperty->Struct, StructData, DProperty));
}

TEST_CASE_NAMED(FTrackInitializedPropertiesTest, "CoreUObject::Serialization::TrackInitializedProperties", "[CoreUObject][EngineFilter]")
{
	UTestInstanceDataObjectClass* BaseObject = NewObject<UTestInstanceDataObjectClass>();
	UClass* TestClass = CreateInstanceDataObjectClass(nullptr, nullptr, BaseObject->GetClass(), BaseObject->GetOuter());

	FStructProperty* StructProperty = FindFProperty<FStructProperty>(TestClass, TEXT("Struct"));
	REQUIRE(StructProperty);
	REQUIRE(StructProperty->Struct);

	FIntProperty* AProperty = FindFProperty<FIntProperty>(StructProperty->Struct, TEXT("A"));
	FIntProperty* BProperty = FindFProperty<FIntProperty>(StructProperty->Struct, TEXT("B"));
	FIntProperty* CProperty = FindFProperty<FIntProperty>(StructProperty->Struct, TEXT("C"));
	FIntProperty* DProperty = FindFProperty<FIntProperty>(StructProperty->Struct, TEXT("D"));
	REQUIRE(AProperty);
	REQUIRE(BProperty);
	REQUIRE(CProperty);
	REQUIRE(DProperty);

	void* DefaultStructData = StructProperty->AllocateAndInitializeValue();
	void* StructData = StructProperty->AllocateAndInitializeValue();
	ON_SCOPE_EXIT
	{
		StructProperty->DestroyAndFreeValue(StructData);
		StructProperty->DestroyAndFreeValue(DefaultStructData);
	};

	AProperty->SetPropertyValue_InContainer(DefaultStructData, -1);
	BProperty->SetPropertyValue_InContainer(DefaultStructData, -1);
	CProperty->SetPropertyValue_InContainer(DefaultStructData, -1);
	DProperty->SetPropertyValue_InContainer(DefaultStructData, -1);

	AProperty->SetPropertyValue_InContainer(StructData, 1);
	BProperty->SetPropertyValue_InContainer(StructData, 2);
	CProperty->SetPropertyValue_InContainer(StructData, 3);
	DProperty->SetPropertyValue_InContainer(StructData, -1);

	SetPropertyValueInitialized(StructProperty->Struct, StructData, AProperty);
	SetPropertyValueInitialized(StructProperty->Struct, StructData, BProperty);
	SetPropertyValueInitialized(StructProperty->Struct, StructData, DProperty);

	FUObjectSerializeContext* SerializeContext = FUObjectThreadContext::Get().GetSerializeContext();
	TGuardValue<bool> TrackInitializedPropertiesScope(SerializeContext->bTrackInitializedProperties, true);

	TArray<uint8> BinaryData;
	{
		FMemoryWriter Ar(BinaryData, /*bIsPersistent*/ true);
		FBinaryArchiveFormatter Formatter(Ar);
		FStructuredArchive StructuredAr(Formatter);
		StructProperty->Struct->SerializeTaggedProperties(StructuredAr.Open(), (uint8*)StructData, StructProperty->Struct, (uint8*)DefaultStructData);
	}

#if WITH_TEXT_ARCHIVE_SUPPORT
	TArray<uint8> JsonData;
	{
		FMemoryWriter Ar(JsonData, /*bIsPersistent*/ true);
		FJsonArchiveOutputFormatter Formatter(Ar);
		FStructuredArchive StructuredAr(Formatter);
		StructProperty->Struct->SerializeTaggedProperties(StructuredAr.Open(), (uint8*)StructData, StructProperty->Struct, (uint8*)DefaultStructData);
	}
#endif // WITH_TEXT_ARCHIVE_SUPPORT

	AProperty->SetPropertyValue_InContainer(StructData, 4);
	BProperty->SetPropertyValue_InContainer(StructData, 4);
	CProperty->SetPropertyValue_InContainer(StructData, 4);
	DProperty->SetPropertyValue_InContainer(StructData, 4);

	ResetPropertyValueInitialized(StructProperty->Struct, StructData);

	{
		FMemoryReader Ar(BinaryData, /*bIsPersistent*/ true);
		FBinaryArchiveFormatter Formatter(Ar);
		FStructuredArchive StructuredAr(Formatter);
		StructProperty->Struct->SerializeTaggedProperties(StructuredAr.Open(), (uint8*)StructData, StructProperty->Struct, (uint8*)DefaultStructData);
	}

	CHECK(AProperty->GetPropertyValue_InContainer(StructData) == 1);
	CHECK(BProperty->GetPropertyValue_InContainer(StructData) == 2);
	CHECK(CProperty->GetPropertyValue_InContainer(StructData) == 4); // C unchanged because it is not initialized
	CHECK(DProperty->GetPropertyValue_InContainer(StructData) == 4); // D unchanged because it was serialized without its value

	CHECK(IsPropertyValueInitialized(StructProperty->Struct, StructData, AProperty));
	CHECK(IsPropertyValueInitialized(StructProperty->Struct, StructData, BProperty));
	CHECK_FALSE(IsPropertyValueInitialized(StructProperty->Struct, StructData, CProperty));
	CHECK(IsPropertyValueInitialized(StructProperty->Struct, StructData, DProperty));

#if WITH_TEXT_ARCHIVE_SUPPORT
	AProperty->SetPropertyValue_InContainer(StructData, 4);
	BProperty->SetPropertyValue_InContainer(StructData, 4);
	CProperty->SetPropertyValue_InContainer(StructData, 4);
	DProperty->SetPropertyValue_InContainer(StructData, 4);

	ResetPropertyValueInitialized(StructProperty->Struct, StructData);

	{
		FMemoryReader Ar(JsonData, /*bIsPersistent*/ true);
		FJsonArchiveInputFormatter Formatter(Ar);
		FStructuredArchive StructuredAr(Formatter);
		StructProperty->Struct->SerializeTaggedProperties(StructuredAr.Open(), (uint8*)StructData, StructProperty->Struct, (uint8*)DefaultStructData);
	}

	CHECK(AProperty->GetPropertyValue_InContainer(StructData) == 1);
	CHECK(BProperty->GetPropertyValue_InContainer(StructData) == 2);
	CHECK(CProperty->GetPropertyValue_InContainer(StructData) == 4); // C unchanged because it is not initialized
	CHECK(DProperty->GetPropertyValue_InContainer(StructData) == 4); // D unchanged because it was serialized without its value

	CHECK(IsPropertyValueInitialized(StructProperty->Struct, StructData, AProperty));
	CHECK(IsPropertyValueInitialized(StructProperty->Struct, StructData, BProperty));
	CHECK_FALSE(IsPropertyValueInitialized(StructProperty->Struct, StructData, CProperty));
	CHECK(IsPropertyValueInitialized(StructProperty->Struct, StructData, DProperty));
#endif // WITH_TEXT_ARCHIVE_SUPPORT
}

TEST_CASE_NAMED(FTrackUnknownPropertiesTest, "CoreUObject::Serialization::TrackUnknownProperties", "[CoreUObject][EngineFilter]")
{
	const auto MakePropertyTypeName = [](FName Name)
	{
		FPropertyTypeNameBuilder Builder;
		Builder.AddName(Name);
		return Builder.Build();
	};

	const auto SavePropertyTypeName = [](const FProperty* Property)
	{
		FPropertyTypeNameBuilder Builder;
		Property->SaveTypeName(Builder);
		return Builder.Build();
	};

	UObject* Owner = NewObject<UTestInstanceDataObjectClass>();

	FUObjectSerializeContext* SerializeContext = FUObjectThreadContext::Get().GetSerializeContext();
	TGuardValue<UObject*> SerializedObjectScope(SerializeContext->SerializedObject, Owner);
	TGuardValue<bool> TrackSerializedPropertyPathScope(SerializeContext->bTrackSerializedPropertyPath, true);
	TGuardValue<bool> TrackUnknownPropertiesScope(SerializeContext->bTrackUnknownProperties, true);
	TGuardValue<bool> TrackImpersonatePropertiesScope(SerializeContext->bImpersonateProperties, true);
	FSerializedPropertyPathScope SerializedObjectPath(SerializeContext, {"Struct"});

	FTestInstanceDataObjectStructAlternate AltStructData;
	AltStructData.B = 2.5f;
	AltStructData.C = 3;
	AltStructData.D = 4;
	AltStructData.E = 5;
	AltStructData.Bird = TIDOB_Raven;
	AltStructData.Grain = ETestInstanceDataObjectGrainAlternate::Corn;
	AltStructData.Fruit = ETestInstanceDataObjectFruitAlternate::Orange;
	AltStructData.Direction = ETestInstanceDataObjectDirectionAlternate::North | ETestInstanceDataObjectDirectionAlternate::West;
	AltStructData.GrainTypeChange = ETestInstanceDataObjectGrainAlternate::Corn;
	AltStructData.FruitTypeChange = ETestInstanceDataObjectFruitAlternate::Orange;
	AltStructData.GrainTypeAndPropertyChange = ETestInstanceDataObjectGrainAlternateEnumClass::Corn;
	AltStructData.FruitTypeAndPropertyChange = ETestInstanceDataObjectFruitAlternateNamespace::Orange;
	AltStructData.Point.U = 1;
	AltStructData.Point.V = 2;
	AltStructData.Point.W = 3;

	TArray<uint8> BinaryData;
	{
		FMemoryWriter Ar(BinaryData, /*bIsPersistent*/ true);
		FBinaryArchiveFormatter Formatter(Ar);
		FStructuredArchive StructuredAr(Formatter);
		FTestInstanceDataObjectStructAlternate::StaticStruct()->SerializeTaggedProperties(StructuredAr.Open(), (uint8*)&AltStructData, nullptr, nullptr);
	}

#if WITH_TEXT_ARCHIVE_SUPPORT
	TArray<uint8> JsonData;
	{
		FMemoryWriter Ar(JsonData, /*bIsPersistent*/ true);
		FJsonArchiveOutputFormatter Formatter(Ar);
		FStructuredArchive StructuredAr(Formatter);
		FTestInstanceDataObjectStructAlternate::StaticStruct()->SerializeTaggedProperties(StructuredAr.Open(), (uint8*)&AltStructData, nullptr, nullptr);
	}
#endif // WITH_TEXT_ARCHIVE_SUPPORT

	LOG_SCOPE_VERBOSITY_OVERRIDE(LogClass, ELogVerbosity::Error);
	LOG_SCOPE_VERBOSITY_OVERRIDE(LogEnum, ELogVerbosity::Error);

	FTestInstanceDataObjectStruct StructData;

	{
		FMemoryReader Ar(BinaryData, /*bIsPersistent*/ true);
		FBinaryArchiveFormatter Formatter(Ar);
		FStructuredArchive StructuredAr(Formatter);
		FTestInstanceDataObjectStruct::StaticStruct()->SerializeTaggedProperties(StructuredAr.Open(), (uint8*)&StructData, nullptr, nullptr);
	}

	CHECK(StructData.A == -1);
	CHECK(StructData.B == 2);
	CHECK(StructData.C == 3);
	CHECK(StructData.D == 4);
	CHECK(StructData.Bird == TIDOB_Raven);
	CHECK(StructData.Grain == ETestInstanceDataObjectGrain::Corn);
	CHECK(StructData.Fruit == ETestInstanceDataObjectFruit::Orange);
	CHECK(StructData.Direction == (ETestInstanceDataObjectDirection::North | ETestInstanceDataObjectDirection::West));
	CHECK(StructData.GrainTypeChange == ETestInstanceDataObjectGrain::Corn);
	CHECK(StructData.FruitTypeChange == ETestInstanceDataObjectFruit::Orange);
	CHECK(StructData.GrainTypeAndPropertyChange == ETestInstanceDataObjectGrain::Corn);
	CHECK(StructData.FruitTypeAndPropertyChange == ETestInstanceDataObjectFruit::Orange);
	CHECK(StructData.Point.X == 0);
	CHECK(StructData.Point.Y == 0);
	CHECK(StructData.Point.Z == 0);
#if WITH_METADATA
	CHECK(StructData.Point.W == 3);
#endif

	FPropertyPathNameTree* Tree = FPropertyBagRepository::Get().FindOrCreateUnknownPropertyTree(Owner);
	{
		FSerializedPropertyPathScope Path(SerializeContext, {"B", MakePropertyTypeName(NAME_FloatProperty)});
		CHECK(Tree->Find(SerializeContext->SerializedPropertyPath));
	}
	{
		FSerializedPropertyPathScope Path(SerializeContext, {"C", MakePropertyTypeName(NAME_Int64Property)});
		CHECK(Tree->Find(SerializeContext->SerializedPropertyPath));
	}
	{
		FSerializedPropertyPathScope Path(SerializeContext, {"E", MakePropertyTypeName(NAME_IntProperty)});
		CHECK(Tree->Find(SerializeContext->SerializedPropertyPath));
	}
	{
		FProperty* PointProperty = FTestInstanceDataObjectStructAlternate::StaticStruct()->FindPropertyByName("Point");
		CHECKED_IF(PointProperty)
		{
			FPropertyTypeNameBuilder Builder;
			PointProperty->SaveTypeName(Builder);
			FSerializedPropertyPathScope Path(SerializeContext, {"Point", Builder.Build()});
			CHECK(Tree->Find(SerializeContext->SerializedPropertyPath));
		#if WITH_METADATA
			{
				FSerializedPropertyPathScope SubPath(SerializeContext, {"U", MakePropertyTypeName(NAME_IntProperty)});
				CHECK(Tree->Find(SerializeContext->SerializedPropertyPath));
			}
			{
				FSerializedPropertyPathScope SubPath(SerializeContext, {"V", MakePropertyTypeName(NAME_IntProperty)});
				CHECK(Tree->Find(SerializeContext->SerializedPropertyPath));
			}
			{
				FSerializedPropertyPathScope SubPath(SerializeContext, {"W", MakePropertyTypeName(NAME_IntProperty)});
				CHECK_FALSE(Tree->Find(SerializeContext->SerializedPropertyPath));
			}
		#endif
		}
	}
	{
		FSerializedPropertyPathScope Path(SerializeContext, {"GrainTypeChange", SavePropertyTypeName(FindFProperty<FProperty>(StaticStruct<FTestInstanceDataObjectStructAlternate>(), TEXT("GrainTypeChange")))});
		CHECK(Tree->Find(SerializeContext->SerializedPropertyPath));
	}
	{
		FSerializedPropertyPathScope Path(SerializeContext, {"FruitTypeChange", SavePropertyTypeName(FindFProperty<FProperty>(StaticStruct<FTestInstanceDataObjectStructAlternate>(), TEXT("FruitTypeChange")))});
		CHECK(Tree->Find(SerializeContext->SerializedPropertyPath));
	}
	{
		FSerializedPropertyPathScope Path(SerializeContext, {"GrainTypeAndPropertyChange", SavePropertyTypeName(FindFProperty<FProperty>(StaticStruct<FTestInstanceDataObjectStructAlternate>(), TEXT("GrainTypeAndPropertyChange")))});
		CHECK(Tree->Find(SerializeContext->SerializedPropertyPath));
	}
	{
		FSerializedPropertyPathScope Path(SerializeContext, {"FruitTypeAndPropertyChange", SavePropertyTypeName(FindFProperty<FProperty>(StaticStruct<FTestInstanceDataObjectStructAlternate>(), TEXT("FruitTypeAndPropertyChange")))});
		CHECK(Tree->Find(SerializeContext->SerializedPropertyPath));
	}
	FPropertyBagRepository::Get().DestroyOuterBag(Owner);

#if WITH_TEXT_ARCHIVE_SUPPORT
	StructData = {};

	{
		FMemoryReader Ar(JsonData, /*bIsPersistent*/ true);
		FJsonArchiveInputFormatter Formatter(Ar);
		FStructuredArchive StructuredAr(Formatter);
		FTestInstanceDataObjectStruct::StaticStruct()->SerializeTaggedProperties(StructuredAr.Open(), (uint8*)&StructData, nullptr, nullptr);
	}

	CHECK(StructData.A == -1);
	CHECK(StructData.B == 2);
	CHECK(StructData.C == 3);
	CHECK(StructData.D == 4);
	CHECK(StructData.Bird == TIDOB_Raven);
	CHECK(StructData.Grain == ETestInstanceDataObjectGrain::Corn);
	CHECK(StructData.Fruit == ETestInstanceDataObjectFruit::Orange);
	CHECK(StructData.Direction == (ETestInstanceDataObjectDirection::North | ETestInstanceDataObjectDirection::West));
	CHECK(StructData.GrainTypeChange == ETestInstanceDataObjectGrain::Corn);
	CHECK(StructData.FruitTypeChange == ETestInstanceDataObjectFruit::Orange);
	CHECK(StructData.GrainTypeAndPropertyChange == ETestInstanceDataObjectGrain::Corn);
	CHECK(StructData.FruitTypeAndPropertyChange == ETestInstanceDataObjectFruit::Orange);
	CHECK(StructData.Point.X == 0);
	CHECK(StructData.Point.Y == 0);
	CHECK(StructData.Point.Z == 0);
#if WITH_METADATA
	CHECK(StructData.Point.W == 3);
#endif

	// Testing of the unknown property tree is skipped because it is not supported by the text format.
#endif // WITH_TEXT_ARCHIVE_SUPPORT
}

TEST_CASE_NAMED(FTrackUnknownEnumNamesTest, "CoreUObject::Serialization::TrackUnknownEnumNames", "[CoreUObject][EngineFilter]")
{
	const auto MakePropertyTypeName = [](const UEnum* Enum)
	{
		FPropertyTypeNameBuilder Builder;
		Builder.AddPath(Enum);
		return Builder.Build();
	};

	const auto ParsePropertyTypeName = [](const TCHAR* Name) -> FPropertyTypeName
	{
		FPropertyTypeNameBuilder Builder;
		CHECK(Builder.TryParse(Name));
		return Builder.Build();
	};

	UObject* Owner = NewObject<UTestInstanceDataObjectClass>();

	FUObjectSerializeContext* SerializeContext = FUObjectThreadContext::Get().GetSerializeContext();
	TGuardValue<UObject*> SerializedObjectScope(SerializeContext->SerializedObject, Owner);
	TGuardValue<bool> TrackSerializedPropertyPathScope(SerializeContext->bTrackSerializedPropertyPath, true);
	TGuardValue<bool> TrackUnknownPropertiesScope(SerializeContext->bTrackUnknownProperties, true);
	TGuardValue<bool> TrackUnknownEnumNamesScope(SerializeContext->bTrackUnknownEnumNames, true);
	TGuardValue<bool> TrackImpersonatePropertiesScope(SerializeContext->bImpersonateProperties, true);
	FSerializedPropertyPathScope SerializedObjectPath(SerializeContext, {"Struct"});

	FTestInstanceDataObjectStructAlternate AltStructData;
	AltStructData.Grain = ETestInstanceDataObjectGrainAlternate::Rye;
	AltStructData.Fruit = ETestInstanceDataObjectFruitAlternate::Cherry;
	AltStructData.Direction = ETestInstanceDataObjectDirectionAlternate::North | ETestInstanceDataObjectDirectionAlternate::West |
		ETestInstanceDataObjectDirectionAlternate::Up | ETestInstanceDataObjectDirectionAlternate::Down;
	AltStructData.GrainFromEnumClass = ETestInstanceDataObjectGrainAlternateEnumClass::Corn;
	AltStructData.FruitFromNamespace = ETestInstanceDataObjectFruitAlternateNamespace::Orange;
	AltStructData.GrainTypeChange = ETestInstanceDataObjectGrainAlternate::Corn;
	AltStructData.FruitTypeChange = ETestInstanceDataObjectFruitAlternate::Orange;
	AltStructData.DeletedGrain = ETestInstanceDataObjectGrainAlternate::Rice;
	AltStructData.DeletedFruit = ETestInstanceDataObjectFruitAlternate::Apple;
	AltStructData.DeletedDirection = ETestInstanceDataObjectDirectionAlternate::South | ETestInstanceDataObjectDirectionAlternate::Up;

	TArray<uint8> BinaryData;
	{
		FMemoryWriter Ar(BinaryData, /*bIsPersistent*/ true);
		FBinaryArchiveFormatter Formatter(Ar);
		FStructuredArchive StructuredAr(Formatter);
		FTestInstanceDataObjectStructAlternate::StaticStruct()->SerializeTaggedProperties(StructuredAr.Open(), (uint8*)&AltStructData, nullptr, nullptr);
	}

#if WITH_TEXT_ARCHIVE_SUPPORT
	TArray<uint8> JsonData;
	{
		FMemoryWriter Ar(JsonData, /*bIsPersistent*/ true);
		FJsonArchiveOutputFormatter Formatter(Ar);
		FStructuredArchive StructuredAr(Formatter);
		FTestInstanceDataObjectStructAlternate::StaticStruct()->SerializeTaggedProperties(StructuredAr.Open(), (uint8*)&AltStructData, nullptr, nullptr);
	}
#endif // WITH_TEXT_ARCHIVE_SUPPORT

	LOG_SCOPE_VERBOSITY_OVERRIDE(LogClass, ELogVerbosity::Error);
	LOG_SCOPE_VERBOSITY_OVERRIDE(LogEnum, ELogVerbosity::Error);

	const FPropertyTypeName GrainTypeName = MakePropertyTypeName(StaticEnum<ETestInstanceDataObjectGrain::Type>());
	const FPropertyTypeName FruitTypeName = MakePropertyTypeName(StaticEnum<ETestInstanceDataObjectFruit>());
	const FPropertyTypeName DirectionTypeName = MakePropertyTypeName(StaticEnum<ETestInstanceDataObjectDirection>());

	FPropertyBagRepository& Repo = FPropertyBagRepository::Get();
	TArray<FName> Names{NAME_None};
	bool bHasFlags = false;

	FTestInstanceDataObjectStruct StructData;

	{
		FMemoryReader Ar(BinaryData, /*bIsPersistent*/ true);
		FBinaryArchiveFormatter Formatter(Ar);
		FStructuredArchive StructuredAr(Formatter);
		FTestInstanceDataObjectStruct::StaticStruct()->SerializeTaggedProperties(StructuredAr.Open(), (uint8*)&StructData, nullptr, nullptr);
	}

	CHECK(StructData.Grain == (ETestInstanceDataObjectGrain::Type)((uint8)ETestInstanceDataObjectGrain::Wheat + 1));
	CHECK(StructData.Fruit == (ETestInstanceDataObjectFruit)((uint8)ETestInstanceDataObjectFruit::Orange + 1));
	CHECK(StructData.Direction == (ETestInstanceDataObjectDirection)MAX_uint16);
	CHECK(StructData.GrainFromEnumClass == ETestInstanceDataObjectGrain::Corn);
	CHECK(StructData.FruitFromNamespace == ETestInstanceDataObjectFruit::Orange);
	CHECK(StructData.GrainTypeChange == ETestInstanceDataObjectGrain::Corn);
	CHECK(StructData.FruitTypeChange == ETestInstanceDataObjectFruit::Orange);

#if WITH_METADATA
	Repo.FindUnknownEnumNames(Owner, GrainTypeName, Names, bHasFlags);
	CHECKED_IF(Names.Num() == 1)
	{
		CHECK(Names[0] == "Rye");
	}
	CHECK_FALSE(bHasFlags);

	Repo.FindUnknownEnumNames(Owner, FruitTypeName, Names, bHasFlags);
	CHECKED_IF(Names.Num() == 1)
	{
		CHECK(Names[0] == "Cherry");
	}
	CHECK_FALSE(bHasFlags);

	Repo.FindUnknownEnumNames(Owner, DirectionTypeName, Names, bHasFlags);
	CHECKED_IF(Names.Num() == 2)
	{
		CHECK(Names[0] == "Up");
		CHECK(Names[1] == "Down");
	}
	CHECK(bHasFlags);

	Repo.FindUnknownEnumNames(Owner, ParsePropertyTypeName(TEXT("ETestInstanceDataObjectDeletedGrain(/Script/CoreUObject)")), Names, bHasFlags);
	CHECKED_IF(Names.Num() == 1)
	{
		CHECK(Names[0] == "Rice");
	}
	CHECK_FALSE(bHasFlags);

	Repo.FindUnknownEnumNames(Owner, ParsePropertyTypeName(TEXT("ETestInstanceDataObjectDeletedFruit(/Script/CoreUObject)")), Names, bHasFlags);
	CHECKED_IF(Names.Num() == 1)
	{
		CHECK(Names[0] == "Apple");
	}
	CHECK_FALSE(bHasFlags);

	Repo.FindUnknownEnumNames(Owner, ParsePropertyTypeName(TEXT("ETestInstanceDataObjectDeletedDirection(/Script/CoreUObject)")), Names, bHasFlags);
	CHECKED_IF(Names.Num() == 2)
	{
		CHECK(Names[0] == "Up");
		CHECK(Names[1] == "South");
	}
	CHECK(bHasFlags);
#endif // WITH_METADATA

	FPropertyBagRepository::Get().DestroyOuterBag(Owner);

#if WITH_TEXT_ARCHIVE_SUPPORT
	StructData = {};

	{
		FMemoryReader Ar(JsonData, /*bIsPersistent*/ true);
		FJsonArchiveInputFormatter Formatter(Ar);
		FStructuredArchive StructuredAr(Formatter);
		FTestInstanceDataObjectStruct::StaticStruct()->SerializeTaggedProperties(StructuredAr.Open(), (uint8*)&StructData, nullptr, nullptr);
	}

	CHECK(StructData.Grain == (ETestInstanceDataObjectGrain::Type)((uint8)ETestInstanceDataObjectGrain::Wheat + 1));
	CHECK(StructData.Fruit == (ETestInstanceDataObjectFruit)((uint8)ETestInstanceDataObjectFruit::Orange + 1));
	CHECK(StructData.Direction == (ETestInstanceDataObjectDirection)MAX_uint16);
	CHECK(StructData.GrainFromEnumClass == ETestInstanceDataObjectGrain::Corn);
	CHECK(StructData.FruitFromNamespace == ETestInstanceDataObjectFruit::Orange);
	CHECK(StructData.GrainTypeChange == ETestInstanceDataObjectGrain::Corn);
	CHECK(StructData.FruitTypeChange == ETestInstanceDataObjectFruit::Orange);

#if WITH_METADATA
	Repo.FindUnknownEnumNames(Owner, GrainTypeName, Names, bHasFlags);
	CHECKED_IF(Names.Num() == 1)
	{
		CHECK(Names[0] == "Rye");
	}
	CHECK_FALSE(bHasFlags);

	Repo.FindUnknownEnumNames(Owner, FruitTypeName, Names, bHasFlags);
	CHECKED_IF(Names.Num() == 1)
	{
		CHECK(Names[0] == "Cherry");
	}
	CHECK_FALSE(bHasFlags);

	Repo.FindUnknownEnumNames(Owner, DirectionTypeName, Names, bHasFlags);
	CHECKED_IF(Names.Num() == 2)
	{
		CHECK(Names[0] == "Up");
		CHECK(Names[1] == "Down");
	}
	CHECK(bHasFlags);

	// Testing of the unknown property tree is skipped because it is not supported by the text format.
#endif // WITH_METADATA

	FPropertyBagRepository::Get().DestroyOuterBag(Owner);
#endif // WITH_TEXT_ARCHIVE_SUPPORT
}

TEST_CASE_NAMED(FUnknownEnumNamesTest, "CoreUObject::Serialization::UnknownEnumNames", "[CoreUObject][EngineFilter]")
{
	UObject* Owner = NewObject<UTestInstanceDataObjectClass>();

	FPropertyBagRepository& Repo = FPropertyBagRepository::Get();

	TArray<FName> Names{NAME_None};
	bool bHasFlags = true;

	// Test a non-flags enum...

	FPropertyTypeName FruitTypeName = []
	{
		FPropertyTypeNameBuilder Builder;
		Builder.AddPath(StaticEnum<ETestInstanceDataObjectFruit>());
		return Builder.Build();
	}();

	Repo.FindUnknownEnumNames(Owner, FruitTypeName, Names, bHasFlags);
	CHECK(Names.IsEmpty());
	CHECK_FALSE(bHasFlags);

	const FName NAME_Cherry = "Cherry";
	const FName NAME_Pear = "Pear";

	Repo.AddUnknownEnumName(Owner, nullptr, FruitTypeName, NAME_Pear);
	Repo.AddUnknownEnumName(Owner, StaticEnum<ETestInstanceDataObjectFruit>(), {}, NAME_Cherry);
	Repo.AddUnknownEnumName(Owner, StaticEnum<ETestInstanceDataObjectFruit>(), {}, NAME_Pear);
	Repo.AddUnknownEnumName(Owner, nullptr, FruitTypeName, NAME_Cherry);

	Repo.FindUnknownEnumNames(Owner, FruitTypeName, Names, bHasFlags);
	CHECKED_IF(Names.Num() == 2)
	{
		CHECK(Names[0] == NAME_Pear);
		CHECK(Names[1] == NAME_Cherry);
	}
	CHECK_FALSE(bHasFlags);

	// Test a flags enum by name only...

	FPropertyTypeName FlagsTypeName = []
	{
		FPropertyTypeNameBuilder Builder;
		Builder.AddPath(StaticEnum<ETestInstanceDataObjectDirection>());
		return Builder.Build();
	}();

	const FName NAME_South = "South";
	const FName NAME_Down = "Down";
	const FName NAME_Up = "Up";

	TStringBuilder<128> FlagsString;
	FlagsString.Join(MakeArrayView({NAME_Up, NAME_Down, NAME_South}), TEXTVIEW(" | "));

	Repo.AddUnknownEnumName(Owner, nullptr, FlagsTypeName, NAME_Down);
	Repo.AddUnknownEnumName(Owner, nullptr, FlagsTypeName, *FlagsString);

	Repo.FindUnknownEnumNames(Owner, FlagsTypeName, Names, bHasFlags);
	CHECKED_IF(Names.Num() == 3)
	{
		CHECK(Names[0] == NAME_Down);
		CHECK(Names[1] == NAME_Up);
		CHECK(Names[2] == NAME_South);
	}
	CHECK(bHasFlags);

	// Test resetting unknown enum names for an owner...

	Repo.ResetUnknownEnumNames(Owner);

	Repo.FindUnknownEnumNames(Owner, FlagsTypeName, Names, bHasFlags);
	CHECK(Names.IsEmpty());
	CHECK_FALSE(bHasFlags);

	// Test a flags enum by enum...

	Repo.AddUnknownEnumName(Owner, StaticEnum<ETestInstanceDataObjectDirection>(), {}, NAME_Up);

	Repo.FindUnknownEnumNames(Owner, FlagsTypeName, Names, bHasFlags);
	CHECK(Names.Num() == 1);
	CHECK(bHasFlags);

	Repo.AddUnknownEnumName(Owner, StaticEnum<ETestInstanceDataObjectDirection>(), FlagsTypeName, *FlagsString);

	Repo.FindUnknownEnumNames(Owner, FlagsTypeName, Names, bHasFlags);
	CHECKED_IF(Names.Num() == 2)
	{
		CHECK(Names[0] == NAME_Up);
		CHECK(Names[1] == NAME_Down);
	}
	CHECK(bHasFlags);
}

} // UE

#endif // WITH_TESTS && WITH_EDITORONLY_DATA

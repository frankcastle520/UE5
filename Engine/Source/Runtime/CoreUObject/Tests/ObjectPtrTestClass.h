// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#if WITH_LOW_LEVEL_TESTS

#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"
#include "Templates/SubclassOf.h"

//simple test class for testing TObjectPtr resolve behavior
class UObjectPtrTestClass : public UObject
{
	DECLARE_CLASS_INTRINSIC(UObjectPtrTestClass, UObject, CLASS_MatchedSerializers, TEXT("/Script/CoreUObject"))

public:

};

//abstract test class for testing TObjectPtr resolve behavior
class UObjectPtrAbstractTestClass : public UObject
{
	DECLARE_CLASS_INTRINSIC(UObjectPtrAbstractTestClass, UObject, CLASS_MatchedSerializers | CLASS_Abstract, TEXT("/Script/CoreUObject"))
};

//derived-from-abstract test class for testing TObjectPtr resolve behavior
class UObjectPtrAbstractDerivedTestClass : public UObjectPtrAbstractTestClass
{
	DECLARE_CLASS_INTRINSIC(UObjectPtrAbstractDerivedTestClass, UObjectPtrAbstractTestClass, CLASS_MatchedSerializers, TEXT("/Script/CoreUObject"))
};

//test class with typed reference to another class
class UObjectPtrTestClassWithRef : public UObject
{
	DECLARE_CLASS_INTRINSIC(UObjectPtrTestClassWithRef, UObject, CLASS_MatchedSerializers, TEXT("/Script/CoreUObject"))

public:
	TObjectPtr<UObjectPtrTestClass> ObjectPtr;
	TObjectPtr<UObjectPtrTestClass> ObjectPtrNonNullable;
	TObjectPtr<UObjectPtrAbstractTestClass> ObjectPtrAbstractNonNullable;
	TArray<TObjectPtr<UObjectPtrTestClass>> ArrayObjPtr;
};


//test class with typed reference to another class
class UObjectWithClassProperty : public UObject
{
	DECLARE_CLASS_INTRINSIC(UObjectWithClassProperty, UObject, CLASS_MatchedSerializers, TEXT("/Script/CoreUObject"))

public:
	TObjectPtr<UClass> ClassPtr;
	TSubclassOf<UObjectPtrTestClass> SubClass;
	UClass* ClassRaw;
};

//test class with raw pointer
class UObjectWithRawProperty : public UObject
{
	DECLARE_CLASS_INTRINSIC(UObjectWithRawProperty, UObject, CLASS_MatchedSerializers, TEXT("/Script/CoreUObject"))

public:
	UObjectPtrTestClass* ObjectPtr;
	UObjectPtrTestClass* ObjectPtrNonNullable;
};


//derived test class
class UObjectPtrDerrivedTestClass : public UObjectPtrTestClass
{
	DECLARE_CLASS_INTRINSIC(UObjectPtrDerrivedTestClass, UObjectPtrTestClass, CLASS_MatchedSerializers, TEXT("/Script/CoreUObject"))

public:

};


//non lazy test class
class UObjectPtrNotLazyTestClass : public UObject
{
	DECLARE_CLASS_INTRINSIC(UObjectPtrNotLazyTestClass, UObject, CLASS_MatchedSerializers, TEXT("/Script/CoreUObject"))

public:

};


//stress testing class
class UObjectPtrStressTestClass : public UObject
{
	DECLARE_CLASS_INTRINSIC(UObjectPtrStressTestClass, UObject, CLASS_MatchedSerializers, TEXT("/Script/CoreUObject"))

public:
	uint8 Data[PLATFORM_CACHE_LINE_SIZE];
};


#endif
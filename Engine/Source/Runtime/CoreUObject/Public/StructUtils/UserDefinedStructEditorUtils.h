// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_EDITOR
#include "StructUtils/UserDefinedStruct.h"
#endif // WITH_EDITOR
#include "UserDefinedStructEditorUtils.generated.h"

UCLASS(MinimalAPI, Abstract)
class UUserDefinedStructEditorDataBase : public UObject
{
	GENERATED_BODY()

#if WITH_EDITOR
public:
	COREUOBJECT_API virtual void RecreateDefaultInstance(FString* OutLog = nullptr)
	{		
	}
	
	COREUOBJECT_API virtual void ReinitializeDefaultInstance(FString* OutLog = nullptr)
	{		
	}
	
	COREUOBJECT_API virtual FProperty* FindProperty(const UUserDefinedStruct* Struct, FName Name) const
	{
		return nullptr;
	}

	COREUOBJECT_API virtual FString GetFriendlyNameForProperty(const UUserDefinedStruct* Struct, const FProperty* Property) const
	{
		return {};
	}

	COREUOBJECT_API virtual FString GetTooltip() const
	{
		return {};
	}
#endif // WITH_EDITOR
};

#if WITH_EDITOR
struct FUserDefinedStructEditorUtils
{
	// NOTIFICATION
	DECLARE_DELEGATE_OneParam(FOnUserDefinedStructChanged, UUserDefinedStruct*);
	static COREUOBJECT_API FOnUserDefinedStructChanged OnUserDefinedStructChanged;

	/** called after UDS was changed by editor*/
	static COREUOBJECT_API void OnStructureChanged(UUserDefinedStruct* Struct);
	
	// VALIDATION
	enum EStructureError
	{
		Ok, 
		Recursion,
		FallbackStruct,
		NotCompiled,
		NotBlueprintType,
		NotSupportedType,
		EmptyStructure
	};

	/** Can the structure be a member variable for a BPGClass or BPGStruct */
	static COREUOBJECT_API EStructureError IsStructureValid(const UScriptStruct* Struct, const UStruct* RecursionParent = nullptr, FString* OutMsg = nullptr);
};
#endif // WITH_EDITOR

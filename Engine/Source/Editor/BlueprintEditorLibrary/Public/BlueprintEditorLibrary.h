// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "HAL/Platform.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Logging/LogMacros.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "BlueprintEditorLibrary.generated.h"

class FProperty;
class UBlueprint;
class UClass;
class UEdGraph;
class UObject;
struct FFrame;

DECLARE_LOG_CATEGORY_EXTERN(LogBlueprintEditorLib, Warning, All);

UCLASS()
class BLUEPRINTEDITORLIBRARY_API UBlueprintEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()
public:

	/**
	* Replace any references of variables with the OldVarName to references of those with the NewVarName if possible
	*
	* @param Blueprint		Blueprint to replace the variable references on
	* @param OldVarName		The variable you want replaced
	* @param NewVarName		The new variable that will be used in the old one's place
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static void ReplaceVariableReferences(UBlueprint* Blueprint, const FName OldVarName, const FName NewVarName);

	/**
	* Finds the event graph of the given blueprint. Null if it doesn't have one. This will only return
	* the primary event graph of the blueprint (the graph named "EventGraph").
	*
	* @param Blueprint		Blueprint to search for the event graph on
	*
	* @return UEdGraph*		Event graph of the blueprint if it has one, null if it doesn't have one
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static UEdGraph* FindEventGraph(UBlueprint* Blueprint);

	/**
	* Finds the graph with the given name on the blueprint. Null if it doesn't have one. 
	*
	* @param Blueprint		Blueprint to search
	* @param GraphName		The name of the graph to search for 
	*
	* @return UEdGraph*		Pointer to the graph with the given name, null if not found
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static UEdGraph* FindGraph(UBlueprint* Blueprint, FName GraphName);

	/**
	* Replace any old operator nodes (float + float, vector + float, int + vector, etc)
	* with the newer Promotable Operator version of the node. Preserve any connections the
	* original node had to the newer version of the node. 
	*
	* @param Blueprint	Blueprint to upgrade
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static void UpgradeOperatorNodes(UBlueprint* Blueprint);

	/**
	* Compiles the given blueprint. 
	*
	* @param Blueprint	Blueprint to compile
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static void CompileBlueprint(UBlueprint* Blueprint);

	/**
	* Adds a function to the given blueprint
	*
	* @param Blueprint	The blueprint to add the function to
	* @param FuncName	Name of the function to add
	*
	* @return UEdGraph*
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static UEdGraph* AddFunctionGraph(UBlueprint* Blueprint, const FString& FuncName = FString(TEXT("NewFunction")));

	/** 
	* Deletes the function of the given name on this blueprint. Does NOT replace function call sites. 
	*
	* @param Blueprint		The blueprint to remove the function from
	* @param FuncName		The name of the function to remove
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static void RemoveFunctionGraph(UBlueprint* Blueprint, FName FuncName);

	/**
	* Remove any nodes in this blueprint that have no connections made to them.
	*
	* @param Blueprint		The blueprint to remove the nodes from
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static void RemoveUnusedNodes(UBlueprint* Blueprint);

	/** 
	* Removes the given graph from the blueprint if possible 
	* 
	* @param Blueprint	The blueprint the graph will be removed from
	* @param Graph		The graph to remove
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static void RemoveGraph(UBlueprint* Blueprint, UEdGraph* Graph);

	/**
	* Attempts to rename the given graph with a new name
	*
	* @param Graph			The graph to rename
	* @param NewNameStr		The new name of the graph
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static void RenameGraph(UEdGraph* Graph, const FString& NewNameStr = FString(TEXT("NewGraph")));

	/**
	* Casts the provided Object to a Blueprint - the root asset type of a blueprint asset. Note
	* that the blueprint asset itself is editor only and not present in cooked assets.
	*
	* @param Object			The object we need to get the UBlueprint from
	*
	* @return UBlueprint*	The blueprint type of the given object, nullptr if the object is not a blueprint.
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools", meta = (Keywords = "cast"))
	static UBlueprint* GetBlueprintAsset(UObject* Object);

	/** Attempt to refresh any open blueprint editors for the given asset */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Editor")
	static void RefreshOpenEditorsForBlueprint(const UBlueprint* BP);

	/** Refresh any open blueprint editors */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Editor")
	static void RefreshAllOpenBlueprintEditors();

	/**
	* Attempts to reparent the given blueprint to the new chosen parent class. 
	*
	* @param Blueprint			Blueprint that you would like to reparent
	* @param NewParentClass		The new parent class to use
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static void ReparentBlueprint(UBlueprint* Blueprint, UClass* NewParentClass);

	/**
	* Gathers any unused blueprint variables and populates the given array of FPropertys
	*
	* @param Blueprint			The blueprint to check
	* @param OutProperties		Out array of unused FProperty*'s
	*
	* @return					True if variables were checked on this blueprint, false otherwise.
	*/
	static bool GatherUnusedVariables(const UBlueprint* Blueprint, TArray<FProperty*>& OutProperties);

	/**
	* Deletes any unused blueprint created variables the given blueprint.
	* An Unused variable is any BP variable that is not referenced in any 
	* blueprint graphs
	* 
	* @param Blueprint			Blueprint that you would like to remove variables from
	*
	* @return					Number of variables removed
	*/
	UFUNCTION(BlueprintCallable, Category = "Blueprint Upgrade Tools")
	static int32 RemoveUnusedVariables(UBlueprint* Blueprint);

	/**
	 * Gets the class generated when this blueprint is compiled
	 *
	 * @param BlueprintObj		The blueprint object
	 * @return UClass*			The generated class
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting", meta = (ScriptMethod))
	static UClass* GeneratedClass(UBlueprint* BlueprintObj);

	/**
	 * Sets "Expose On Spawn" to true/false on a Blueprint variable
	 *
	 * @param Blueprint			The blueprint object
	 * @param VariableName		The variable name
	 * @param bExposeOnSpawn	Set to true to expose on spawn
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting", meta = (ScriptMethod))
	static void SetBlueprintVariableExposeOnSpawn(UBlueprint* Blueprint, const FName& VariableName, bool bExposeOnSpawn);

	/**
	 * Sets "Expose To Cinematics" to true/false on a Blueprint variable
	 *
	 * @param Blueprint				The blueprint object
	 * @param VariableName			The variable name
	 * @param bExposeToCinematics	Set to true to expose to cinematics
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting", meta = (ScriptMethod))
	static void SetBlueprintVariableExposeToCinematics(UBlueprint* Blueprint, const FName& VariableName, bool bExposeToCinematics);

	/**
	 * Sets "Instance Editable" to true/false on a Blueprint variable
	 *
	 * @param Blueprint				The blueprint object
	 * @param VariableName			The variable name
	 * @param bInstanceEditable		Toggle InstanceEditable
	 */
	UFUNCTION(BlueprintCallable, Category = "Editor Scripting", meta = (ScriptMethod))
	static void SetBlueprintVariableInstanceEditable(UBlueprint* Blueprint, const FName& VariableName, bool bInstanceEditable);

	/**
	 * Creates a blueprint based on a specific parent, honoring registered custom blueprint types
	 * 
	 * @param AssetPath				The full path that the asset should be created with
	 * @param ParentClass			The parent class that the blueprint should be based on
	 */
	 UFUNCTION(BlueprintCallable, Category = "Blueprint Editor")
	 static UBlueprint* CreateBlueprintAssetWithParent(const FString& AssetPath, UClass* ParentClass);

	/**
	  * Adds a member variable to the specified blueprint inferring the type from a provided value.
	  * 
	  * @return	true if it succeeds, false if it fails.
	  */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Blueprint Editor", meta=(CustomStructureParam="DefaultValue"))
	static bool AddMemberVariableWithValue(UBlueprint* Blueprint, FName MemberName, const int32& DefaultValue);
	static bool Generic_AddMemberVariableWithValue(UBlueprint* Blueprint, FName MemberName, const uint8* DefaultValuePtr, const FProperty* DefaultValueProp);
	DECLARE_FUNCTION(execAddMemberVariableWithValue);
	
	/**
	  * Adds a member variable to the specified blueprint with the specified type.
	  * 
	  * @return	true if it succeeds, false if it fails.
	  */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Editor")
	static bool AddMemberVariable(UBlueprint* Blueprint, FName MemberName, const FEdGraphPinType& VariableType);
	
	/** @return a pintype for 'int', 'byte', 'bool', 'real', 'name', 'string' or 'text' - returns 'int' type if invalid type is provided */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Editor")
	static FEdGraphPinType GetBasicTypeByName(FName TypeName);
	
	/** @return a pintype for the provided struct - returns 'int' type if invalid struct is provided */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Editor")
	static FEdGraphPinType GetStructType(const UScriptStruct* StructType);
	
	/** @return a class reference pintype for the provided class - returns 'int' type if invalid class is provided */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Editor")
	static FEdGraphPinType GetClassReferenceType(const UClass* ClassType);
	
	/** @return a object reference pintype for the provided class - returns 'int' type if invalid object type is provided */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Editor")
	static FEdGraphPinType GetObjectReferenceType(const UClass* ObjectType);
	
	/** @return a array of ContainedType type - returns 'int' type if invalid type is provided */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Editor")
	static FEdGraphPinType GetArrayType(const FEdGraphPinType& ContainedType);
	
	/** @return a set of ContainedType type - returns 'int' type if invalid type is provided */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Editor")
	static FEdGraphPinType GetSetType(const FEdGraphPinType& ContainedType);
	
	/** @return a map of KeyType to ValueType type - returns 'int' type if invalid type is provided */
	UFUNCTION(BlueprintCallable, Category = "Blueprint Editor")
	static FEdGraphPinType GetMapType(const FEdGraphPinType& KeyType,const FEdGraphPinType& ValueType);
};
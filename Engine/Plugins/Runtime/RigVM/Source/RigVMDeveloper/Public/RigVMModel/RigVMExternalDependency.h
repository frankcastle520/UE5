// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMClient.h"
#include "RigVMCore/RigVMGraphFunctionHost.h"
#include "RigVMExternalDependency.generated.h"

USTRUCT()
struct RIGVMDEVELOPER_API FRigVMExternalDependency
{
	GENERATED_BODY()

public:

	FRigVMExternalDependency()
		: ExternalPath()
		, InternalPath()
		, Category(NAME_None)
	{}

	FRigVMExternalDependency(const FString& InPath, const FName& InCategory)
		: ExternalPath(InPath)
		, InternalPath()
		, Category(InCategory)
	{}

	bool IsExternal() const { return InternalPath.IsEmpty(); }
	bool IsInternal() const { return !IsExternal(); }

	const FString& GetExternalPath() const { return ExternalPath; }
	const FString& GetInternalPath() const { return InternalPath; }
	const FName& GetCategory() const { return Category; }

	bool operator==(const FRigVMExternalDependency& InOther) const
	{
		return ExternalPath == InOther.ExternalPath &&
			InternalPath == InOther.InternalPath &&
			Category == InOther.Category;
	}

private:

	UPROPERTY()
	FString ExternalPath;

	UPROPERTY()
	FString InternalPath;

	UPROPERTY()
	FName Category;
};

UINTERFACE()
class RIGVMDEVELOPER_API URigVMExternalDependencyManager : public UInterface
{
	GENERATED_BODY()
};

// Interface to deal with mapping external dependencies
class RIGVMDEVELOPER_API IRigVMExternalDependencyManager
{
	GENERATED_BODY()

public:
	
	virtual const TArray<FName>& GetExternalDependencyCategories() const;
	virtual TArray<FRigVMExternalDependency> GetExternalDependenciesForCategory(const FName& InCategory) const = 0;
	TArray<FRigVMExternalDependency> GetAllExternalDependencies() const;

	static inline const FLazyName UserDefinedEnumCategory = FLazyName(TEXT("UserDefinedEnum"));
	static inline const FLazyName UserDefinedStructCategory = FLazyName(TEXT("UserDefinedStruct"));
	static inline const FLazyName RigVMGraphFunctionCategory = FLazyName(TEXT("RigVMGraphFunction"));

protected:

	virtual void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const FRigVMClient* InClient) const;
	virtual void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const FRigVMGraphFunctionStore* InFunctionStore) const;
	virtual void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const FRigVMGraphFunctionData* InFunction) const;
	virtual void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const FRigVMGraphFunctionHeader* InHeader) const;
	virtual void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const FRigVMFunctionCompilationData* InCompilationData) const;
	virtual void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const URigVMGraph* InGraph) const;
	virtual void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const URigVMNode* InNode) const;
	virtual void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const URigVMPin* InPin) const;
	virtual void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const UStruct* InStruct) const;
	virtual void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const UEnum* InEnum) const;
	virtual void CollectExternalDependenciesForCPPTypeObject(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const UObject* InObject) const;

	static TArray<FName> DependencyCategories;
};


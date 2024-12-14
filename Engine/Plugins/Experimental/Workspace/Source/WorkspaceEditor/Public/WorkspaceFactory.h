// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "WorkspaceFactory.generated.h"

class UWorkspaceSchema;

namespace UE::Workspace
{
	struct FUtils;
	class FWorkspaceEditorModule;
}

UCLASS(BlueprintType)
class WORKSPACEEDITOR_API UWorkspaceFactory : public UFactory
{
	GENERATED_BODY()

protected:
	UWorkspaceFactory();

	// Set the schema class for workspaces produced with this factory
	void SetSchemaClass(TSubclassOf<UWorkspaceSchema> InSchemaClass) { SchemaClass = InSchemaClass; }

private:
	virtual bool ConfigureProperties() override;
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext) override;
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override
	{
		return FactoryCreateNew(Class, InParent, Name, Flags, Context, Warn, NAME_None);
	}

	UPROPERTY()
	TSubclassOf<UWorkspaceSchema> SchemaClass = nullptr;

	friend class UE::Workspace::FWorkspaceEditorModule;
};
// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class IPlugin;

/** Interface to manage project references to external content */
class IProjectExternalContentInterface
{
public:
	/** Return whether the project can reference external content */
	virtual bool IsEnabled() const = 0;

	/**
     * Return whether the specified external content is referenced by the project
	 * @param VersePath External content Verse path
	 */
	virtual bool HasExternalContent(const FString& VersePath) const = 0;

	/** 
	 * Return whether the specified external content is loaded (and referenced by the project)
	 * @param VersePath External content Verse path
	 */
	virtual bool IsExternalContentLoaded(const FString& VersePath) const = 0;

	/** Return the list of external content Verse paths referenced by the project */
	virtual TArray<FString> GetExternalContentVersePaths() const = 0;

	UE_DEPRECATED(5.6, "GetExternalContentIds is deprecated, use GetExternalContentVersePaths instead")
	TArray<FString> GetExternalContentIds() const { return GetExternalContentVersePaths(); };

	/**
	 * Called upon AddExternalContent completion
	 * @param bSuccess Whether the external content was successfully added to the project
	 * @param Plugins List of loaded plugins hosting the external content
	 */
	DECLARE_DELEGATE_TwoParams(FAddExternalContentComplete, bool /*bSuccess*/, const TArray<TSharedRef<IPlugin>>& /*Plugins*/);

	/**
	 * Add a reference to external content to the project and asynchronously downloads/loads the external content
	 * @param VersePath External content Verse path
	 * @param CompleteCallback See FAddExternalContentComplete
	 */
	virtual void AddExternalContent(const FString& VersePath, FAddExternalContentComplete CompleteCallback = FAddExternalContentComplete()) = 0;

	/**
	 * Called upon RemoveExternalContent completion
	 * @param bSuccess Whether the external content was successfully removed from the project (could be canceled by the user)
	 */
	DECLARE_DELEGATE_OneParam(FRemoveExternalContentComplete, bool /*bSuccess*/);

	/**
	 * Remove references to external content from the project and unloads the external content
	 * @param VersePaths External content Verse paths
	 * @param CompleteCallback See FRemoveExternalContentComplete
	 */
	virtual void RemoveExternalContent(TConstArrayView<FString> VersePaths, FRemoveExternalContentComplete CompleteCallback = FRemoveExternalContentComplete()) = 0;

	void RemoveExternalContent(const FString& VersePath, FRemoveExternalContentComplete CompleteCallback = FRemoveExternalContentComplete())
	{
		TArray<FString, TInlineAllocator<1>> VersePaths = { VersePath };
		RemoveExternalContent(VersePaths, MoveTemp(CompleteCallback));
	}
};

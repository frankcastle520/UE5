// Copyright Epic Games, Inc. All Rights Reserved.

#pragma  once

#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Templates/SharedPointer.h"
#include "Delegates/IDelegateInstance.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SubclassOf.h"

class AActor;
class FExtender;
class FUICommandList;
class AInstancedActorsManager;
class UInstancedActorsSubsystem;

/**
* The public interface to this module
* This module contains logic for converting between AActors and Instanced Actors. 
* @todo move to a dedicated class
*/
class INSTANCEDACTORSEDITOR_API FInstancedActorsEditorModule : public IModuleInterface
{
public:
	DECLARE_DELEGATE_OneParam(FOnConvert, TConstArrayView<AActor*>);

	/** Convert selected actors to Instance Actors (IAs) while using the IASubsystemClass */
	void CustomizedConvertActorsToIAsUIAction(TConstArrayView<AActor*> InActors, TSubclassOf<UInstancedActorsSubsystem> IASubsystemClass) const;
	
	/** 
	 * Sets InDelegate as the delegate that will be executed as "Convert Instanced Actors to regular Actors" action.
	 * @param ActionFormatLabelOverride if supplied will be used to override the default action label. Note that 
	 *	this is a format string and needs to require one parameter, the Actor's name
	 */
	void SetIAToActorDelegate(const FOnConvert& InDelegate, const FTextFormat& ActionFormatLabelOverride = FTextFormat());

	/**
	 * Sets InDelegate as the delegate that will be executed as "Convert Actors to Instanced Actors" action.
	 * @param ActionFormatLabelOverride if supplied will be used to override the default action label. Note that
	 *	this is a format string and needs to require one parameter, the Actor's name
	 */
	void SetActorToIADelegate(const FOnConvert& InDelegate, const FTextFormat& ActionFormatLabelOverride = FTextFormat());

	/** Resets FOnConvert delegates and action labels to their defaults */
	void ResetConversionDelegates();

protected:
	// Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Convert selected actors to Instance Actors (IAs). */
	void ConvertActorsToIAsUIAction(TConstArrayView<AActor*> InActors) const;

	/** Convert selected Instance Actors to Actors. */
	void ConvertIAsToActorsUIAction(TConstArrayView<AActor*> InActors) const;

	TSharedRef<FExtender> CreateLevelViewportContextMenuExtender(const TSharedRef<FUICommandList> CommandList, const TArray<AActor*> InActors);
	void AddLevelViewportMenuExtender();
	void RemoveLevelViewportMenuExtender();

	FDelegateHandle LevelViewportExtenderHandle;
	FOnConvert ActorToIADelegate;
	FOnConvert IAToActorDelegate;
	FText CustomizedLabelPrefix;
	FTextFormat ActorToIAFormatLabel;
	FTextFormat IAToActorFormatLabel;
};

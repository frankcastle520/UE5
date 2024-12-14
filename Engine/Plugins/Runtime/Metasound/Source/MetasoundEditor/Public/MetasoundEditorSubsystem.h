// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AssetRegistry/AssetData.h"
#include "EditorSubsystem.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "MetasoundBuilderBase.h"
#include "MetasoundDocumentInterface.h"
#include "UObject/ScriptInterface.h"

#include "MetasoundEditorSubsystem.generated.h"


// Forward Declarations
class UMetaSoundBuilderBase;
class UMetasoundEditorGraphMember;
class UMetasoundEditorGraphMemberDefaultLiteral;

struct FMetaSoundPageSettings;


/** The subsystem in charge of editor MetaSound functionality */
UCLASS(meta = (DisplayName = "MetaSound Editor Subsystem"))
class METASOUNDEDITOR_API UMetaSoundEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	// Binds literal editor Metadata to the given member.  If the literal already exists, adds literal
	// reference to given member (asserts that existing literal is of similar subclass provided).  If 
	// it does not exist, or an optional template object is provided, metadata is generated then bound.
	// Returns true if new literal metadata was generated, false if not. Asserts if bind failed.
	bool BindMemberMetadata(
		FMetaSoundFrontendDocumentBuilder& Builder,
		UMetasoundEditorGraphMember& InMember,
		TSubclassOf<UMetasoundEditorGraphMemberDefaultLiteral> LiteralClass,
		UMetasoundEditorGraphMemberDefaultLiteral* TemplateObject = nullptr);

	// Build the given builder to a MetaSound asset
	// @param Author - Sets the author on the given builder's document.
	// @param AssetName - Name of the asset to build.
	// @param PackagePath - Path of package to build asset to.
	// @param TemplateSoundWave - SoundWave settings such as attenuation, modulation, and sound class will be copied from the optional TemplateSoundWave.
	// For preset builders, TemplateSoundWave will override the template values from the referenced asset.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder|Editor", meta = (WorldContext = "Parent", ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "MetaSound Asset") TScriptInterface<IMetaSoundDocumentInterface> BuildToAsset(
		UPARAM(DisplayName = "Builder") UMetaSoundBuilderBase* InBuilder,
		const FString& Author,
		const FString& AssetName,
		const FString& PackagePath,
		EMetaSoundBuilderResult& OutResult,
		UPARAM(DisplayName = "Template SoundWave") const USoundWave* TemplateSoundWave = nullptr);

	// Creates new member metadata for a member of a given builder, copying data from the referenced asset in the case of preset inherited inputs
	UMetasoundEditorGraphMemberDefaultLiteral* CreateMemberMetadata(
		FMetaSoundFrontendDocumentBuilder& Builder,
		FName InMemberName,
		TSubclassOf<UMetasoundEditorGraphMemberDefaultLiteral> LiteralClass) const;

	// Returns a builder for the given MetaSound asset. Returns null if provided a transient MetaSound. For finding builders for transient
	// MetaSounds, use the UMetaSoundBuilderSubsystem's API (FindPatchBuilder, FindSourceBuilder, FindBuilderByName etc.)
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder|Editor", meta = (DisplayName = "Find Or Begin Building MetaSound Asset", ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Builder") UMetaSoundBuilderBase* FindOrBeginBuilding(TScriptInterface<IMetaSoundDocumentInterface> MetaSound, EMetaSoundBuilderResult& OutResult) const;
	
	// Find graph input metadata (which includes editor only range information for floats) for a given input. If the metadata does not exist, create it. 
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Input Metadata") UMetaSoundFrontendMemberMetadata* FindOrCreateGraphInputMetadata(UPARAM(DisplayName = "Builder") UMetaSoundBuilderBase* InBuilder, FName InputName, EMetaSoundBuilderResult& OutResult);

	// Returns the corresponding literal class for a given type
	TSubclassOf<UMetasoundEditorGraphMemberDefaultLiteral> GetLiteralClassForType(FName TypeName) const;

	// Sets the visual location to InLocation of a given node InNode of a given builder's document.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder|Editor", meta = (ExpandEnumAsExecs = "OutResult"))
	void SetNodeLocation(
		UPARAM(DisplayName = "Builder") UMetaSoundBuilderBase* InBuilder,
		UPARAM(DisplayName = "Node Handle") const FMetaSoundNodeHandle& InNode,
		UPARAM(DisplayName = "Location") const FVector2D& InLocation,
		EMetaSoundBuilderResult& OutResult);

	// Initialize the UObject asset, with an optional MetaSound to be referenced if the asset is a preset
	void InitAsset(UObject& InNewMetaSound, UObject* InReferencedMetaSound = nullptr);

	UE_DEPRECATED(5.5, "EdGraph is now transiently generated and privately managed for asset editor use only.")
	void InitEdGraph(UObject& InMetaSound);

	// Returns whether or not a page with the given name both exists and is set as
	// a valid, cooked target for the currently set audition platform in editor.
	bool IsPageAuditionPlatformCookTarget(FName InPageName) const;
	bool IsPageAuditionPlatformCookTarget(const FGuid& InPageID) const;

	// Wraps RegisterGraphWithFrontend logic in Frontend with any additional logic required to refresh editor & respective editor object state.
	// @param InMetaSound - MetaSound to register
	// @param bInForceSynchronize - Forces the synchronize flag for all open graphs being registered by this call (all referenced graphs and
	// referencing graphs open in editors)
	void RegisterGraphWithFrontend(UObject& InMetaSound, bool bInForceViewSynchronization = false) const;

	// Register toolbar extender that will be displayed in the MetaSound Asset Editor.
	void RegisterToolbarExtender(TSharedRef<FExtender> InExtender);

	// If the given page name is implemented on the provided builder, sets the focused page of
	// the provided builder to the associated page and sets the audition page to
	// the provided name. If the given builder has an asset editor open, optionally opens or brings
	// that editor's associated PageID into user focus.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder|Editor", meta = (ExpandEnumAsExecs = "OutResult"))
	void SetFocusedPage(UMetaSoundBuilderBase* Builder, FName PageName, bool bOpenEditor, EMetaSoundBuilderResult& OutResult) const;

	// If the given PageID is implemented on the provided builder, sets the focused page of
	// the provided builder to the associated page and sets the audition target page to
	// the provided ID. If the given builder has an asset editor open, optionally opens or brings
	// that editor's associated PageID into user focus. Returns whether or not the audition page
	// was set to the provided focus page.
	bool SetFocusedPage(UMetaSoundBuilderBase& Builder, const FGuid& InPageID, bool bOpenEditor, bool bPostTransaction = true) const;

	// Unregisters toolbar extender that is displayed in the MetaSound Asset Editor.
	bool UnregisterToolbarExtender(TSharedRef<FExtender> InExtender);

	// Get the default author for a MetaSound asset
	const FString GetDefaultAuthor();

	// Returns all currently toolbar extenders registered to be displayed within the MetaSound Asset Editor.
	const TArray<TSharedRef<FExtender>>& GetToolbarExtenders() const;

	static UMetaSoundEditorSubsystem& GetChecked();
	static const UMetaSoundEditorSubsystem& GetConstChecked();

private:
	bool SetFocusedPageInternal(FName PageName, const FGuid& InPageID, UMetaSoundBuilderBase& Builder, bool bOpenEditor, bool bPostTransaction) const;

	// Copy over sound wave settings such as attenuation, modulation, and sound class from the template sound wave to the MetaSound
	void SetSoundWaveSettingsFromTemplate(USoundWave& NewMetasound, const USoundWave& TemplateSoundWave) const;

	// Editor Toolbar Extenders
	TArray<TSharedRef<FExtender>> EditorToolbarExtenders;
};

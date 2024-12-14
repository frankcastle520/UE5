// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Map.h"
#include "Delegates/IDelegateInstance.h"
#include "Engine/Engine.h"
#include "Interfaces/MetasoundOutputFormatInterfaces.h"
#include "MetasoundBuilderBase.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundDynamicOperatorTransactor.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentBuilder.h"
#include "MetasoundFrontendDocumentCacheInterface.h"
#include "MetasoundFrontendDocumentModifyDelegates.h"
#include "Subsystems/EngineSubsystem.h"
#include "Templates/Function.h"
#include "UObject/NoExportTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "MetasoundBuilderSubsystem.generated.h"


// Forward Declarations
class FMetasoundAssetBase;
class UAudioComponent;
class UMetaSound;
class UMetaSoundPatch;
class UMetaSoundSource;

struct FMetasoundFrontendClassName;
struct FMetasoundFrontendVersion;
struct FPerPlatformFloat;
struct FPerPlatformInt;

enum class EMetaSoundOutputAudioFormat : uint8;

namespace Metasound::Engine
{
	// Forward Declarations
	struct FOutputAudioFormatInfo;
	class FMetaSoundAssetManager;
} // namespace Metasound::Engine


DECLARE_DYNAMIC_DELEGATE_OneParam(FOnCreateAuditionGeneratorHandleDelegate, UMetasoundGeneratorHandle*, GeneratorHandle);

/** Builder in charge of building a MetaSound Patch */
UCLASS(Transient, BlueprintType, meta = (DisplayName = "MetaSound Patch Builder"))
class METASOUNDENGINE_API UMetaSoundPatchBuilder : public UMetaSoundBuilderBase
{
	GENERATED_BODY()

public:
	virtual TScriptInterface<IMetaSoundDocumentInterface> BuildNewMetaSound(FName NameBase) const override;
	virtual const UClass& GetBaseMetaSoundUClass() const override;

protected:
	virtual void BuildAndOverwriteMetaSoundInternal(TScriptInterface<IMetaSoundDocumentInterface> ExistingMetaSound, bool bForceUniqueClassName) const override;
	virtual void OnAssetReferenceAdded(TScriptInterface<IMetaSoundDocumentInterface> DocInterface) override;
	virtual void OnRemovingAssetReference(TScriptInterface<IMetaSoundDocumentInterface> DocInterface) override;

	friend class UMetaSoundBuilderSubsystem;
};

/** Builder in charge of building a MetaSound Source */
UCLASS(Transient, BlueprintType, meta = (DisplayName = "MetaSound Source Builder"))
class METASOUNDENGINE_API UMetaSoundSourceBuilder : public UMetaSoundBuilderBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (WorldContext = "Parent", AdvancedDisplay = "2"))
	void Audition(UObject* Parent, UAudioComponent* AudioComponent, FOnCreateAuditionGeneratorHandleDelegate OnCreateGenerator, bool bLiveUpdatesEnabled = false);

	// Returns whether or not live updates are both globally enabled (via cvar) and are enabled on this builder's last built sound, which may or may not still be playing.
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	bool GetLiveUpdatesEnabled() const;

	// Sets the MetaSound's BlockRate override
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	void SetBlockRateOverride(float BlockRate);

	// Sets the output audio format of the source
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	void SetFormat(EMetaSoundOutputAudioFormat OutputFormat, EMetaSoundBuilderResult& OutResult);

	// Sets the MetaSound's SampleRate override
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	void SetSampleRateOverride(int32 SampleRate);

	const Metasound::Engine::FOutputAudioFormatInfoPair* FindOutputAudioFormatInfo() const;

	virtual TScriptInterface<IMetaSoundDocumentInterface> BuildNewMetaSound(FName NameBase) const override;
	virtual const UClass& GetBaseMetaSoundUClass() const override;

#if WITH_EDITORONLY_DATA
	// Sets the MetaSound's BlockRate override (editor only, to allow setting per-platform values)
	void SetPlatformBlockRateOverride(const FPerPlatformFloat& PlatformFloat);

	// Sets the MetaSound's BlockRate override (editor only, to allow setting per-platform values)
	void SetPlatformSampleRateOverride(const FPerPlatformInt& PlatformInt);
#endif // WITH_EDITORONLY_DATA

	// Sets the MetaSound's Quality level
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	void SetQuality(FName Quality);

protected:
	virtual void BuildAndOverwriteMetaSoundInternal(TScriptInterface<IMetaSoundDocumentInterface> ExistingMetaSound, bool bForceUniqueClassName) const override;
	virtual void InitDelegates(Metasound::Frontend::FDocumentModifyDelegates& OutDocumentDelegates) override;
	virtual void OnAssetReferenceAdded(TScriptInterface<IMetaSoundDocumentInterface> DocInterface) override;
	virtual void OnRemovingAssetReference(TScriptInterface<IMetaSoundDocumentInterface> DocInterface) override;

private:
	static TOptional<Metasound::FAnyDataReference> CreateDataReference(const Metasound::FOperatorSettings& InOperatorSettings, FName DataType, const Metasound::FLiteral& InLiteral, Metasound::EDataReferenceAccessType AccessType);

	const FMetasoundFrontendGraph& GetConstTargetPageGraphChecked() const;

	const UMetaSoundSource& GetMetaSoundSource() const;
	UMetaSoundSource& GetMetaSoundSource();

	void InitTargetPageDelegates(Metasound::Frontend::FDocumentModifyDelegates& OutDocumentDelegates);

	void OnEdgeAdded(int32 EdgeIndex) const;
	void OnInputAdded(int32 InputIndex);
	void OnLiveComponentFinished(UAudioComponent* AudioComponent);
	void OnNodeAdded(int32 NodeIndex) const;
	void OnNodeInputLiteralSet(int32 NodeIndex, int32 VertexIndex, int32 LiteralIndex) const;
	void OnOutputAdded(int32 OutputIndex) const;
	void OnPageAdded(const Metasound::Frontend::FDocumentMutatePageArgs& InArgs);
	void OnRemoveSwappingEdge(int32 SwapIndex, int32 LastIndex) const;
	void OnRemovingInput(int32 InputIndex);
	void OnRemoveSwappingNode(int32 SwapIndex, int32 LastIndex) const;
	void OnRemovingNodeInputLiteral(int32 NodeIndex, int32 VertexIndex, int32 LiteralIndex) const;
	void OnRemovingOutput(int32 OutputIndex) const;
	void OnRemovingPage(const Metasound::Frontend::FDocumentMutatePageArgs& InArgs);

	using FAuditionableTransaction = TFunctionRef<bool(Metasound::DynamicGraph::FDynamicOperatorTransactor&)>;
	bool ExecuteAuditionableTransaction(FAuditionableTransaction Transaction) const;

	TArray<uint64> LiveComponentIDs;
	FDelegateHandle LiveComponentHandle;
	FGuid TargetPageID = Metasound::Frontend::DefaultPageID;

	friend class UMetaSoundBuilderSubsystem;
};

/** The subsystem in charge of tracking MetaSound builders */
UCLASS(meta = (DisplayName = "MetaSound Builder Subsystem"))
class METASOUNDENGINE_API UMetaSoundBuilderSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

private:
	UPROPERTY()
	TMap<FName, TObjectPtr<UMetaSoundBuilderBase>> NamedBuilders;

public:	
	UE_DEPRECATED(5.5, "Call 'ReloadBuilder' in IDocumentBuilderRegistry instead ")
	virtual void InvalidateDocumentCache(const FMetasoundFrontendClassName& InClassName) const;

	static UMetaSoundBuilderSubsystem* Get();
	static UMetaSoundBuilderSubsystem& GetChecked();
	static const UMetaSoundBuilderSubsystem* GetConst();
	static const UMetaSoundBuilderSubsystem& GetConstChecked();

	UE_DEPRECATED(5.5, "Use FDocumentBuilderRegistry::FindOrBeginBuilding, which is now only supported in builds loading editor-only data.")
	UMetaSoundBuilderBase& AttachBuilderToAssetChecked(UObject& InObject) const;

	UE_DEPRECATED(5.5, "Use FDocumentBuilderRegistry::FindOrBeginBuilding (when editor only data is loaded) or MetaSoundEditorSubsystem::FindOrBeginBuilding call")
	UMetaSoundPatchBuilder* AttachPatchBuilderToAsset(UMetaSoundPatch* InPatch) const;

	UE_DEPRECATED(5.5, "Use FDocumentBuilderRegistry::FindOrBeginBuilding (when editor only data is loaded) or MetaSoundEditorSubsystem::FindOrBeginBuilding call")
	UMetaSoundSourceBuilder* AttachSourceBuilderToAsset(UMetaSoundSource* InSource) const;

	UE_DEPRECATED(5.5, "Moved to IDocumentBuilderRegistry::RemoveBuilderFromAsset")
	bool DetachBuilderFromAsset(const FMetasoundFrontendClassName& InClassName) const;

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder",  meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Patch Builder") UMetaSoundPatchBuilder* CreatePatchBuilder(FName BuilderName, EMetaSoundBuilderResult& OutResult);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Source Builder") UMetaSoundSourceBuilder* CreateSourceBuilder(
		FName BuilderName,
		FMetaSoundBuilderNodeOutputHandle& OnPlayNodeOutput,
		FMetaSoundBuilderNodeInputHandle& OnFinishedNodeInput,
		TArray<FMetaSoundBuilderNodeInputHandle>& AudioOutNodeInputs,
		EMetaSoundBuilderResult& OutResult,
		EMetaSoundOutputAudioFormat OutputFormat = EMetaSoundOutputAudioFormat::Mono,
		bool bIsOneShot = true);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder",  meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Patch Preset Builder") UMetaSoundPatchBuilder* CreatePatchPresetBuilder(FName BuilderName, const TScriptInterface<IMetaSoundDocumentInterface>& ReferencedPatchClass, EMetaSoundBuilderResult& OutResult);

	UMetaSoundBuilderBase& CreatePresetBuilder(FName BuilderName, const TScriptInterface<IMetaSoundDocumentInterface>& ReferencedPatchClass, EMetaSoundBuilderResult& OutResult);
	
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (ExpandEnumAsExecs = "OutResult"))
	UPARAM(DisplayName = "Source Preset Builder") UMetaSoundSourceBuilder* CreateSourcePresetBuilder(FName BuilderName, const TScriptInterface<IMetaSoundDocumentInterface>& ReferencedSourceClass, EMetaSoundBuilderResult& OutResult);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound Bool Literal"))
	UPARAM(DisplayName = "Bool Literal") FMetasoundFrontendLiteral CreateBoolMetaSoundLiteral(bool Value, FName& DataType);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound Bool Array Literal"))
	UPARAM(DisplayName = "Bool Array Literal") FMetasoundFrontendLiteral CreateBoolArrayMetaSoundLiteral(const TArray<bool>& Value, FName& DataType);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound Float Literal"))
	UPARAM(DisplayName = "Float Literal") FMetasoundFrontendLiteral CreateFloatMetaSoundLiteral(float Value, FName& DataType);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound Float Array Literal"))
	UPARAM(DisplayName = "Float Array Literal") FMetasoundFrontendLiteral CreateFloatArrayMetaSoundLiteral(const TArray<float>& Value, FName& DataType);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound Int Literal"))
	UPARAM(DisplayName = "Int32 Literal") FMetasoundFrontendLiteral CreateIntMetaSoundLiteral(int32 Value, FName& DataType);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound Int Array Literal"))
	UPARAM(DisplayName = "Int32 Array Literal") FMetasoundFrontendLiteral CreateIntArrayMetaSoundLiteral(const TArray<int32>& Value, FName& DataType);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound Object Literal"))
	UPARAM(DisplayName = "Object Literal") FMetasoundFrontendLiteral CreateObjectMetaSoundLiteral(UObject* Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound Object Array Literal"))
	UPARAM(DisplayName = "Object Array Literal") FMetasoundFrontendLiteral CreateObjectArrayMetaSoundLiteral(const TArray<UObject*>& Value);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound String Literal"))
	UPARAM(DisplayName = "String Literal") FMetasoundFrontendLiteral CreateStringMetaSoundLiteral(const FString& Value, FName& DataType);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound String Array Literal"))
	UPARAM(DisplayName = "String Array Literal") FMetasoundFrontendLiteral CreateStringArrayMetaSoundLiteral(const TArray<FString>& Value, FName& DataType);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Create MetaSound Literal From AudioParameter"))
	UPARAM(DisplayName = "Param Literal") FMetasoundFrontendLiteral CreateMetaSoundLiteralFromParam(const FAudioParameter& Param);

	// Returns the builder manually registered with the MetaSound Builder Subsystem with the provided custom name (if previously registered)
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Find Builder By Name"))
	UPARAM(DisplayName = "Builder") UMetaSoundBuilderBase* FindBuilder(FName BuilderName);

	// Returns the builder associated with the given MetaSound (if one exists, transient or asset).
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder", meta = (DisplayName = "Find Builder By MetaSound"))
	UPARAM(DisplayName = "Builder") UMetaSoundBuilderBase* FindBuilderOfDocument(TScriptInterface<const IMetaSoundDocumentInterface> InMetaSound) const;

	// Returns the patch builder manually registered with the MetaSound Builder Subsystem with the provided custom name (if previously registered)
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	UPARAM(DisplayName = "Patch Builder") UMetaSoundPatchBuilder* FindPatchBuilder(FName BuilderName);

	// Returns the source builder manually registered with the MetaSound Builder Subsystem with the provided custom name (if previously registered)
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	UPARAM(DisplayName = "Source Builder") UMetaSoundSourceBuilder* FindSourceBuilder(FName BuilderName);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	UPARAM(DisplayName = "Is Registered") bool IsInterfaceRegistered(FName InInterfaceName) const;

#if WITH_EDITOR
	UE_DEPRECATED(5.5, "No longer required as reload is now just directly called on a given builder.")
	void PostBuilderAssetTransaction(const FMetasoundFrontendClassName& InClassName) { }
#endif // WITH_EDITOR

	// Adds builder to subsystem's registry to make it persistent and easily accessible by multiple systems or Blueprints
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	void RegisterBuilder(FName BuilderName, UMetaSoundBuilderBase* Builder);

	// Adds builder to subsystem's registry to make it persistent and easily accessible by multiple systems or Blueprints
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	void RegisterPatchBuilder(FName BuilderName, UMetaSoundPatchBuilder* Builder);

	// Adds builder to subsystem's registry to make it persistent and easily accessible by multiple systems or Blueprints
	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	void RegisterSourceBuilder(FName BuilderName, UMetaSoundSourceBuilder* Builder);

	// Sets the targeted page for all MetaSound graph & input default to resolve against.
	// If target page is not implemented (or cooked in a runtime build) for the active platform,
	// uses order of cooked pages(see 'Page Settings' for order) falling back to lower index - ordered page
	// implemented in MetaSound asset. If no fallback is found, uses default graph/input default.
	UFUNCTION(BlueprintCallable, Category = "MetaSounds|Pages")
	UPARAM(DisplayName = "TargetPageChanged") bool SetTargetPage(FName PageName);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	UPARAM(DisplayName = "Unregistered") bool UnregisterBuilder(FName BuilderName);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	UPARAM(DisplayName = "Unregistered") bool UnregisterPatchBuilder(FName BuilderName);

	UFUNCTION(BlueprintCallable, Category = "Audio|MetaSound|Builder")
	UPARAM(DisplayName = "Unregistered") bool UnregisterSourceBuilder(FName BuilderName);

private:
	friend class UMetaSoundBuilderBase;
};

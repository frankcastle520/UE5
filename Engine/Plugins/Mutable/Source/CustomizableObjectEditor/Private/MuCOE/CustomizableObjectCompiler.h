// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetRegistry/AssetData.h"
#include "MuCO/CustomizableObjectPrivate.h"
#include "MuCO/CustomizableObjectCompilerTypes.h"
#include "MuCO/UnrealToMutableTextureConversionUtils.h"
#include "MuCOE/CompilationMessageCache.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSource.h"
#include "MuCOE/CustomizableObjectEditorLogger.h"
#include "UObject/GCObject.h"
#include "TickableEditorObject.h"
#include "MuR/Ptr.h"

#include "Framework/Notifications/NotificationManager.h"

class FCustomizableObjectCompileRunnable;
class FCustomizableObjectSaveDDRunnable;
class FReferenceCollector;
class FRunnableThread;
class FText;
class UCustomizableObject;
class UCustomizableObjectNode;
struct FMutableGraphGenerationContext;

namespace mu
{
	class NodeObject;
	class Node;
}


mu::Ptr<mu::NodeObject> GenerateMutableRoot(const UCustomizableObject* Object, FMutableGraphGenerationContext& GenerationContext);


class FCustomizableObjectCompiler : public FTickableEditorObject, public FTickableCookObject, public FGCObject
{
public:
	virtual ~FCustomizableObjectCompiler() override {}

	/** Check for pending compilation process. Returns true if an object has been updated. */
	bool Tick(bool bBlocking = false);
	int32 GetNumRemainingWork() const;

	// FTickableGameObject interface
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; };
	virtual bool IsTickable() const override;
	virtual void Tick( float InDeltaTime ) override;
	virtual TStatId GetStatId() const override;

	// FTickableCookObject interface
	virtual void TickCook(float DeltaTime, bool bCookCompete) override;
	
	/** Generate the Mutable Graph from the Unreal Graph. */
	mu::Ptr<mu::Node> Export(UCustomizableObject* Object, const FCompilationOptions& Options, TArray<TSoftObjectPtr<const UTexture>>& OutRuntimeReferencedTextures, TArray<FMutableSourceTextureData>& OutCompilerReferencedTextures);

	void CompilerLog(const FText& Message, const TArray<const UObject*>& UObject, const EMessageSeverity::Type MessageSeverity = EMessageSeverity::Warning, const bool bAddBaseObjectInfo = true, const ELoggerSpamBin SpamBin = ELoggerSpamBin::ShowAll);
	void CompilerLog(const FText& Message, const UObject* Context = nullptr, const EMessageSeverity::Type MessageSeverity = EMessageSeverity::Warning, const bool bAddBaseObjectInfo = true, const ELoggerSpamBin SpamBin = ELoggerSpamBin::ShowAll);
	void NotifyCompilationErrors() const;

	void FinishCompilationTask();
	void FinishSavingDerivedDataTask();

	void ForceFinishCompilation();
	void ClearCompileRequests();

	void AddCompileNotification(const FText& CompilationStep) const;
	static void RemoveCompileNotification();

	/** Load required assets and compile.
	 *
	 * Loads assets which reference Object's package asynchronously before calling ProcessChildObjectsRecursively. */
	void Compile(const TSharedRef<FCompilationRequest>& InCompileRequest);
	void Compile(const TArray<TSharedRef<FCompilationRequest>>& InCompileRequests);
	
	bool IsRequestQueued(const TSharedRef<FCompilationRequest>& InCompileRequest) const;

	/** FSerializableObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FCustomizableObjectCompiler");
	}

private:

	// Object containing all error and warning logs raised during compilation.
	FCompilationMessageCache CompilationLogsContainer;

	void SetCompilationState(ECompilationStatePrivate State, ECompilationResultPrivate Result) const;
	
	void CompileInternal(bool bAsync = false);

	void CompleteRequest(ECompilationStatePrivate State, ECompilationResultPrivate Result);
	bool TryPopCompileRequest();

	bool TryLoadCompiledDataFromDDC(UCustomizableObject& CustomizableObject);

	void PreloadingReferencerAssetsCallback(bool bAsync);
	
	// Will output to Mutable Log the warning and error messages generated during the CO compilation
	// and update the values of NumWarnings and NumErrors
	void UpdateCompilerLogData();
	
	/** Launches the compile task in another thread when compiling a CO in the editor
	* @param bShowNotification [in] whether to show the compiling CO notification or not
	* @return nothing */
	void LaunchMutableCompile();

	/** Launches the save derived data task in another thread after compiling a CO in the
	* editor
	* @param bShowNotification [in] whether to show the saving DD notification or not
	* @return nothing */
	void SaveCODerivedData();
	
	ECompilationResultPrivate GetCompilationResult() const;

	/** Pointer to the Asynchronous Preloading process call back */
	TSharedPtr<FStreamableHandle> AsynchronousStreamableHandlePtr;
	TArray<FSoftObjectPath> ArrayAssetToStream;

	/** Compile task and thread. */
	TSharedPtr<FCustomizableObjectCompileRunnable> CompileTask;
	TSharedPtr<FRunnableThread> CompileThread;

	/** SaveDD task and thread. */
	TSharedPtr<FCustomizableObjectSaveDDRunnable> SaveDDTask;
	TSharedPtr<FRunnableThread> SaveDDThread;
	
	/** Array used to protect from garbage collection those COs loaded asynchronously */
	TArray<TObjectPtr<UObject>> ArrayGCProtect;

	TSharedPtr<mu::Model, ESPMode::ThreadSafe> Model;
	TSharedPtr<FModelStreamableBulkData> ModelStreamableBulkData;

	// Protected from GC with FCustomizableObjectCompiler::AddReferencedObjects
	TObjectPtr<UCustomizableObject> CurrentObject = nullptr;

	FCompilationOptions CurrentOptions;

	/** Current Compilation request. */
	TSharedPtr<FCompilationRequest> CurrentRequest;

	/** Pending requests. */
	TArray<TSharedRef<FCompilationRequest>> CompileRequests;

	uint32 NumCompilationRequests = 0;

	/** Compilation progress bar handle */
	FProgressNotificationHandle CompileNotificationHandle;

	/** Compilation start time in seconds. */
	double CompilationStartTime = 0;

};

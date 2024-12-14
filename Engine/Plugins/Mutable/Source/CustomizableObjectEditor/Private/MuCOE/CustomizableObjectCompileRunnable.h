// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Runnable.h"
#include "MuCO/CustomizableObjectPrivate.h"
#include "MuCO/CustomizableObjectCompilerTypes.h"
#include "MuCO/UnrealToMutableTextureConversionUtils.h"
#include "MuR/Ptr.h"
#include "MuT/Node.h"
#include "MuCOE/CustomizableObjectEditorLogger.h"

#include "DerivedDataCacheKey.h"
#include "DerivedDataCachePolicy.h"

#include <atomic>

class ITargetPlatform;

class FCustomizableObjectCompileRunnable : public FRunnable
{
public:

	struct FErrorAttachedData
	{
		TArray<float> UnassignedUVs;
	};

	struct FError
	{
		EMessageSeverity::Type Severity = EMessageSeverity::Error;
		ELoggerSpamBin SpamBin = ELoggerSpamBin::ShowAll;
		FText Message;
		TSharedPtr<FErrorAttachedData> AttachedData;
		TObjectPtr<const UObject> Context = nullptr;
		TObjectPtr<const UObject> Context2 = nullptr;

		FError(const EMessageSeverity::Type InSeverity, const FText& InMessage, const UObject* InContext, const UObject* InContext2=nullptr, const ELoggerSpamBin InSpamBin = ELoggerSpamBin::ShowAll )
			: Severity(InSeverity), SpamBin(InSpamBin), Message(InMessage), Context(InContext), Context2(InContext2) {}
		FError(const EMessageSeverity::Type InSeverity, const FText& InMessage, const TSharedPtr<FErrorAttachedData>& InAttachedData, const UObject* InContext, const ELoggerSpamBin InSpamBin = ELoggerSpamBin::ShowAll)
			: Severity(InSeverity), SpamBin(InSpamBin), Message(InMessage), AttachedData(InAttachedData), Context(InContext) {}
	};

private:

	mu::Ptr<mu::Node> MutableRoot;
	TArray<FError> ArrayErrors;

	/** */
	struct FReferenceResourceRequest
	{
		int32 ID = -1;
		TSharedPtr<mu::Ptr<mu::Image>> ResolvedImage;
		TSharedPtr<UE::Tasks::FTaskEvent> CompletionEvent;
	};
	TQueue<FReferenceResourceRequest, EQueueMode::Mpsc> PendingResourceReferenceRequests;

	mu::Ptr<mu::Image> LoadResourceReferenced(int32 ID);


public:

	FCustomizableObjectCompileRunnable(mu::Ptr<mu::Node> Root);

	// FRunnable interface
	uint32 Run() override;

	// Own interface

	//
	bool IsCompleted() const;

	//
	const TArray<FError>& GetArrayErrors() const;

	void Tick();

	TSharedPtr<mu::Model, ESPMode::ThreadSafe> Model;

	FCompilationOptions Options;
	
	TArray<FMutableSourceTextureData> ReferencedTextures;

	FString ErrorMsg;

	// Whether the thread has finished running
	std::atomic<bool> bThreadCompleted;
};


class FCustomizableObjectSaveDDRunnable : public FRunnable
{
public:

	FCustomizableObjectSaveDDRunnable(const TSharedPtr<FCompilationRequest>& InRequest, TSharedPtr<mu::Model> InModel, FModelResources& ModelResources, TSharedPtr<FModelStreamableBulkData> ModelStreamables);

	// FRunnable interface
	uint32 Run() override;

	//
	bool IsCompleted() const;


	const ITargetPlatform* GetTargetPlatform() const;

private:

	void CachePlatfromData();

	void StoreCachedPlatformDataInDDC(bool& bStoredSuccessfully);

	void StoreCachedPlatformDataToDisk(bool& bStoredSuccessfully);

	FCompilationOptions Options;

	MutableCompiledDataStreamHeader CustomizableObjectHeader;

	FString CustomizableObjectName;

	// Paths used to save files to disk
	FString FolderPath;
	FString CompileDataFullFileName;
	FString StreamableDataFullFileName;

	UE::DerivedData::FCacheKey DDCKey;
	UE::DerivedData::ECachePolicy DefaultDDCPolicy;

	// Whether the thread has finished running
	std::atomic<bool> bThreadCompleted = false;

public:

	TSharedPtr<mu::Model, ESPMode::ThreadSafe> Model;
	TSharedPtr<FModelStreamableBulkData> ModelStreamables;

	// Cached platform data
	MutablePrivate::FMutableCachedPlatformData PlatformData;

	// DDC Helpers
	TArray<MutablePrivate::FFile> BulkDataFilesDDC;
};

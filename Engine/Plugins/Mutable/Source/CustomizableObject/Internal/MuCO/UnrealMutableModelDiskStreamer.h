// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCO/CustomizableObjectPrivate.h"
#include "MuR/Serialisation.h"

#if WITH_EDITOR
#include "DerivedDataRequestOwner.h"
#endif

class FArchive;
class IAsyncReadFileHandle;
class IAsyncReadRequest;
class IBulkDataIORequest;
class UCustomizableObject;
namespace mu { class Model; }
struct FMutableStreamableBlock;

class UnrealMutableInputStream : public mu::InputStream
{
public:

	UnrealMutableInputStream(FArchive& ar);

	// mu::InputStream interface
	void Read(void* pData, uint64 size) override;

private:
	FArchive& m_ar;
};


// Implementation of a mutable streamer using bulk storage.
class CUSTOMIZABLEOBJECT_API FUnrealMutableModelBulkReader : public mu::ModelReader
{
public:
	// 
	~FUnrealMutableModelBulkReader();

	// Own interface

	/** Make sure that the provided object can stream data. */
	bool PrepareStreamingForObject(UCustomizableObject* Object);

#if WITH_EDITOR
	/** Cancel any further streaming operations for the given object. This is necessary if the object compiled data is
	 * going to be modified. This can only happen in the editor, when recompiling.
	 * Any additional streaming requests for this object will fail.
	 */
	void CancelStreamingForObject(const UCustomizableObject* CustomizableObject);

	/** Checks if there are any streaming operations for the parameter object.
	* @return true if there are streaming operations in flight
	*/
	bool AreTherePendingStreamingOperationsForObject(const UCustomizableObject* CustomizableObject) const;
#endif

	/** Release all the pending resources. This disables treamings for all objects. */
	void EndStreaming();

	// mu::ModelReader interface
	OPERATION_ID BeginReadBlock(const mu::Model*, uint32 key0, void* pBuffer, uint64 size, TFunction<void(bool bSuccess)>* CompletionCallback) override;
	bool IsReadCompleted(OPERATION_ID) override;
	bool EndRead(OPERATION_ID) override;

protected:

	struct FReadRequest
	{
		TSharedPtr<IBulkDataIORequest> BulkReadRequest;
		TSharedPtr<IAsyncReadRequest> FileReadRequest;
		TSharedPtr<FAsyncFileCallBack> FileCallback;

#if WITH_EDITORONLY_DATA
		TSharedPtr<UE::DerivedData::FRequestOwner> DDCReadRequest;
#endif
	};
	
	/** Streaming data for one object. */
	struct FObjectData
	{
		TWeakPtr<const mu::Model> Model;

		FString BulkFilePrefix;
		TMap<OPERATION_ID, FReadRequest> CurrentReadRequests;
		TMap<uint32, TSharedPtr<IAsyncReadFileHandle>> ReadFileHandles;

		TSharedPtr<FModelStreamableBulkData> ModelStreamableBulkData;

#if WITH_EDITORONLY_DATA
		// DDC files streaming
		bool bIsStoredInDDC = false;
		UE::DerivedData::FCacheKey DDCKey;
		UE::DerivedData::FCacheRecordPolicy DDCPolicy;
#endif
	};

	TArray<FObjectData> Objects;

	FCriticalSection FileHandlesCritical;

	/** This is used to generate unique ids for read requests. */
	OPERATION_ID LastOperationID = 0;
};


#if WITH_EDITOR

class UnrealMutableOutputStream : public mu::OutputStream
{
public:

	UnrealMutableOutputStream(FArchive& ar);

	// mu::OutputStream interface
	void Write(const void* pData, uint64 size) override;

private:
	FArchive& m_ar;
};


// Implementation of a mutable streamer using bulk storage.
class CUSTOMIZABLEOBJECT_API FUnrealMutableModelBulkWriterEditor : public mu::ModelWriter
{
public:
	// 
	FUnrealMutableModelBulkWriterEditor(FArchive* InMainDataArchive = nullptr, FArchive* InStreamedDataArchive = nullptr);

	// mu::ModelWriter interface
	void OpenWriteFile(uint32 BlockKey) override;
	void Write(const void* pBuffer, uint64 size) override;
	void CloseWriteFile() override;

protected:

	// Non-owned pointer to an archive where we'll store the main model data (non-streamable)
	FArchive* MainDataArchive = nullptr;

	// Non-owned pointer to an archive where we'll store the resouces (streamable)
	FArchive* StreamedDataArchive = nullptr;

	FArchive* CurrentWriteFile = nullptr;

};


// Implementation of a mutable streamer using bulk storage.
class CUSTOMIZABLEOBJECT_API FUnrealMutableModelBulkWriterCook : public mu::ModelWriter
{
public:
	// 
	FUnrealMutableModelBulkWriterCook(FArchive* InMainDataArchive = nullptr, MutablePrivate::FModelStreamableData* InStreamedData = nullptr);

	// mu::ModelWriter interface
	void OpenWriteFile(uint32 BlockKey) override;
	void Write(const void* pBuffer, uint64 size) override;
	void CloseWriteFile() override;

protected:

	// Non-owned pointer to an archive where we'll store the main model data (non-streamable)
	FArchive* MainDataArchive = nullptr;

	// Non-owned pointer to an archive where we'll store the resouces (streamable)
	MutablePrivate::FModelStreamableData* StreamedData = nullptr;

	uint32 CurrentKey = 0;

};


#endif

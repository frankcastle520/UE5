// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataChannelPublic.h"
#include "NiagaraDataChannel.h"
#include "NiagaraDataChannelAccessor.generated.h"

class UNiagaraDataChannelHandler;
struct FNiagaraSpawnInfo;
struct FNiagaraVariableBase;

/** 
Initial simple API for reading and writing data in a data channel from game code / BP. 
Likely to be replaced in the near future with a custom BP node and a helper struct.
*/


UCLASS(BlueprintType, MinimalAPI)
class UNiagaraDataChannelReader : public UObject
{
	GENERATED_BODY()
private:

	FNiagaraDataChannelDataPtr Data = nullptr;
	bool bReadingPreviousFrame = false;

	template<typename T>
	bool ReadData(const FNiagaraVariableBase& Var, int32 Index, T& OutData)const;

public:

	void Cleanup();

	UPROPERTY()
	TObjectPtr<UNiagaraDataChannelHandler> Owner;
	
	/** Call before each access to the data channel to grab the correct data to read. */
	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "Niagara DataChannel"))
	NIAGARA_API bool InitAccess(FNiagaraDataChannelSearchParameters SearchParams, bool bReadPrevFrameData);

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "Niagara DataChannel"))
	NIAGARA_API int32 Num()const;

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "Niagara DataChannel"))
	NIAGARA_API double ReadFloat(FName VarName, int32 Index, bool& IsValid)const;

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "Niagara DataChannel"))
	NIAGARA_API FVector2D ReadVector2D(FName VarName, int32 Index, bool& IsValid)const;

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "Niagara DataChannel"))
	NIAGARA_API FVector ReadVector(FName VarName, int32 Index, bool& IsValid)const;

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "Niagara DataChannel"))
	NIAGARA_API FVector4 ReadVector4(FName VarName, int32 Index, bool& IsValid)const;

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "Niagara DataChannel"))
	NIAGARA_API FQuat ReadQuat(FName VarName, int32 Index, bool& IsValid)const;

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "Niagara DataChannel"))
	NIAGARA_API FLinearColor ReadLinearColor(FName VarName, int32 Index, bool& IsValid)const;

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "Niagara DataChannel"))
	NIAGARA_API int32 ReadInt(FName VarName, int32 Index, bool& IsValid)const;

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "Niagara DataChannel"))
	NIAGARA_API uint8 ReadEnum(FName VarName, int32 Index, bool& IsValid)const;

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "Niagara DataChannel"))
	NIAGARA_API bool ReadBool(FName VarName, int32 Index, bool& IsValid)const;

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "Niagara DataChannel"))
	NIAGARA_API FVector ReadPosition(FName VarName, int32 Index, bool& IsValid)const;

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "Niagara DataChannel"))
	NIAGARA_API FNiagaraID ReadID(FName VarName, int32 Index, bool& IsValid)const;

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "Niagara DataChannel"))
	NIAGARA_API FNiagaraSpawnInfo ReadSpawnInfo(FName VarName, int32 Index, bool& IsValid)const;
};

UCLASS(BlueprintType, MinimalAPI)
class UNiagaraDataChannelWriter : public UObject
{
	GENERATED_BODY()
private:

	/** Local data buffers we're writing into. */
	FNiagaraDataChannelGameDataPtr Data = nullptr;


public:
	template<typename T>
	void WriteData(const FNiagaraVariableBase& Var, int32 Index, const T& InData)
	{
		if (ensure(Data.IsValid()))
		{
			if (FNiagaraDataChannelVariableBuffer* VarBuffer = Data->FindVariableBuffer(Var))
			{
				VarBuffer->Write<T>(Index, InData);
			}
		}
	}

	void Cleanup();

	UPROPERTY()
	TObjectPtr<UNiagaraDataChannelHandler> Owner;
	
	/** Call before each batch of writes to allocate the data we'll be writing to. */
	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (bVisibleToGame="true", bVisibleToCPU="true", bVisibleToGPU="true", Keywords = "niagara DataChannel", AdvancedDisplay = "DebugSource", AutoCreateRefTerm="DebugSource"))
	NIAGARA_API bool InitWrite(FNiagaraDataChannelSearchParameters SearchParams, int32 Count, bool bVisibleToGame, bool bVisibleToCPU, bool bVisibleToGPU, const FString& DebugSource);

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "niagara DataChannel"))
	NIAGARA_API int32 Num()const;

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "niagara DataChannel"))
	NIAGARA_API void WriteFloat(FName VarName, int32 Index, double InData);
	
	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "niagara DataChannel"))
	NIAGARA_API void WriteVector2D(FName VarName, int32 Index, FVector2D InData);

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "niagara DataChannel"))
	NIAGARA_API void WriteVector(FName VarName, int32 Index, FVector InData);

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "niagara DataChannel"))
	NIAGARA_API void WriteVector4(FName VarName, int32 Index, FVector4 InData);

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "niagara DataChannel"))
	NIAGARA_API void WriteQuat(FName VarName, int32 Index, FQuat InData);

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "niagara DataChannel"))
	NIAGARA_API void WriteLinearColor(FName VarName, int32 Index, FLinearColor InData);
	
	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "niagara DataChannel"))
	NIAGARA_API void WriteInt(FName VarName, int32 Index, int32 InData);

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "niagara DataChannel"))
	NIAGARA_API void WriteEnum(FName VarName, int32 Index, uint8 InData);

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "niagara DataChannel"))
	NIAGARA_API void WriteBool(FName VarName, int32 Index, bool InData);

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "niagara DataChannel"))
	NIAGARA_API void WriteSpawnInfo(FName VarName, int32 Index, FNiagaraSpawnInfo InData);

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "niagara DataChannel"))
	NIAGARA_API void WritePosition(FName VarName, int32 Index, FVector InData);

	UFUNCTION(BlueprintCallable, Category = NiagaraDataChannel, meta = (Keywords = "niagara DataChannel"))
	NIAGARA_API void WriteID(FName VarName, int32 Index, FNiagaraID InData);


};


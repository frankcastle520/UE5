// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/ReplicationSystem/NetTokenStore.h"
#include "Iris/ReplicationSystem/ObjectReferenceCacheFwd.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetExportContext.h"
#include "Net/Core/Trace/NetTrace.h"
#include "UObject/CoreNet.h"

namespace UE::Net
{

class FNetTokenStoreState
{
public:
	using FNetTokenStoreKey = FNetTokenDataStore::FNetTokenStoreKey;

	// The size of the array is managed by FNetTokenDataStream
	FNetTokenStoreState()
	{
		Reset();
	}

	// Reserve space for tokens
	bool ReserveTokenCount(uint32 TypeIndex, uint32 NewCount)
	{
		if ((TypeIndex >= FNetToken::MaxTypeIdCount) || (NewCount >= FNetToken::MaxNetTokenCount))
		{
			return false;
		}

		const uint32 NewSize = FMath::Max<uint32>(TokenInfoArray[TypeIndex].Num(), NewCount);
		TokenInfoArray[TypeIndex].SetNumZeroed(NewSize);

		return true;
	}

	void Reset()
	{
		// We reserve the first token for each type as an invalid token
		for (TArray<FNetTokenStoreKey>& TokenInfos : TokenInfoArray)
		{
			TokenInfos.SetNum(1);
		}
	}

	// Map from NetTokenIndex -> NetTokenDataStoreKey (Index)
	TArray<FNetTokenDataStore::FNetTokenStoreKey> TokenInfoArray[FNetToken::MaxTypeIdCount];
};

FNetTokenDataStore::FNetTokenDataStore(FNetTokenStore& InTokenStore)
: TokenStore(InTokenStore)
, TypeId(FNetToken::InvalidTokenTypeId)
{
	// Reserve first index as invalid.
	StoredTokens.Add(FNetToken());
}

FNetTokenDataStore::~FNetTokenDataStore()
{
}

FNetTokenDataStore::FNetTokenStoreKey FNetTokenDataStore::GetTokenKey(FNetToken Token, const FNetTokenStoreState& TokenStoreState) const
{
	if (Token.GetTypeId() == GetTypeId())
	{
		const TArray<FNetTokenStoreKey>& TokenStoreKeysForType = TokenStoreState.TokenInfoArray[GetTypeId()];
		const int32 TokenIndex(Token.GetIndex());

		return TokenIndex < TokenStoreKeysForType.Num() ? TokenStoreKeysForType[TokenIndex] : FNetTokenStoreKey();
	}
	else
	{
		UE_LOG(LogNetToken, Error, TEXT("FNetTokenDataStore::GetTokenKey Invalid tokentype %s StoreTypeId: %u"), *Token.ToString(), GetTypeId());
		return FNetTokenStoreKey();
	}
}

FNetToken FNetTokenDataStore::CreateAndStoreTokenForKey(FNetTokenStoreKey Key)
{
	FNetTokenStoreState& LocalNetTokenStoreState = *TokenStore.GetLocalNetTokenStoreState();

	// The LocalNetTokenStoreState always maps locally assigned tokens -> Key (Index)
	const uint32 NextTokenIndex = LocalNetTokenStoreState.TokenInfoArray[GetTypeId()].Num();

	if (!ensure(NextTokenIndex < FNetToken::MaxNetTokenCount))
	{
		return FNetToken();
	}

	FNetToken NewToken = FNetTokenStore::MakeNetToken(TypeId, NextTokenIndex, TokenStore.IsAuthority() ? FNetToken::ENetTokenAuthority::Authority : FNetToken::ENetTokenAuthority::None);

	// Store token info
	LocalNetTokenStoreState.TokenInfoArray[GetTypeId()].Add(Key);

	// The stored tokens array contains the current NetToken associated with the Key (Index), but it can be updated to use an authoritative token instead.
	StoredTokens[Key.GetKeyIndex()] = NewToken;

	return NewToken;
}

void FNetTokenDataStore::StoreTokenForKey(FNetTokenStoreKey Key, FNetToken NetToken)
{
	// The stored tokens array contains the current NetToken associated with the Key (Index), but it can be updated to use an authoritative token instead.
	StoredTokens[Key.GetKeyIndex()] = NetToken;
}

FNetToken FNetTokenDataStore::GetNetTokenFromKey(FNetTokenStoreKey Key) const
{
	return StoredTokens[Key.GetKeyIndex()];
}

FNetToken FNetTokenStore::InternalReadNetToken(UE::Net::FNetSerializationContext& Context, FNetToken::FTypeId TokenTypeId)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	FNetToken ReadToken;

	UE_NET_TRACE_NAMED_DYNAMIC_NAME_SCOPE(TokenScope, FName(), *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
	const uint32 TokenIndex = ReadPackedUint32(Reader);
	if (const bool bIsValid = (TokenIndex != FNetToken::InvalidTokenIndex))
	{
		const bool bIsAssignedByAuthority = Reader->ReadBool();
		if (TokenTypeId == FNetToken::InvalidTokenTypeId)
		{
			TokenTypeId = Reader->ReadBits(FNetToken::TokenTypeIdBits);
		}

		if (!Reader->IsOverflown())
		{
			ReadToken = FNetTokenStore::MakeNetToken(TokenTypeId, TokenIndex, bIsAssignedByAuthority ? FNetToken::ENetTokenAuthority::Authority : FNetToken::ENetTokenAuthority::None);
			UE_NET_TRACE_SET_SCOPE_NAME(TokenScope, *ReadToken.ToString());
		}
	}

	return ReadToken;
}

void FNetTokenStore::InternalWriteNetToken(UE::Net::FNetSerializationContext& Context, FNetToken Token, bool bWriteTokenType)
{
	UE_NET_TRACE_DYNAMIC_NAME_SCOPE(*Token.ToString(), *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	const uint32 TokenIndex = Token.GetIndex();
	WritePackedUint32(Writer, TokenIndex);
	if (TokenIndex != FNetToken::InvalidTokenIndex)
	{
		Writer->WriteBool(Token.IsAssignedByAuthority());
		if (bWriteTokenType)
		{
			Writer->WriteBits(Token.GetTypeId(), FNetToken::TokenTypeIdBits);
		}
	}
}

FNetToken FNetTokenStore::InternalReadNetToken(FArchive& Ar, FNetToken::FTypeId TokenTypeId)
{
	FNetToken ReadToken;

	uint32 TokenIndex = 0;
	Ar.SerializeIntPacked(TokenIndex);
	if (const bool bIsValid = (TokenIndex != FNetToken::InvalidTokenIndex))
	{
		bool bIsAssignedByAuthority = false;
		Ar.SerializeBits(&bIsAssignedByAuthority, 1);

		if (TokenTypeId == FNetToken::InvalidTokenTypeId)
		{
			uint32 TempTokenTypeId = 0;
			Ar.SerializeBits(&TempTokenTypeId, FNetToken::TokenTypeIdBits);
			TokenTypeId = TempTokenTypeId;
		}

		if (!Ar.IsError())
		{
			ReadToken = FNetTokenStore::MakeNetToken(TokenTypeId, TokenIndex, bIsAssignedByAuthority ? FNetToken::ENetTokenAuthority::Authority : FNetToken::ENetTokenAuthority::None);
		}
	}

	return ReadToken;
}

// Note: Be careful when modifying this methods to not affect replay compatibility.
void FNetTokenStore::InternalWriteNetToken(FArchive& Ar, FNetToken Token, bool bWriteTokenType)
{
	UE_NET_TRACE_DYNAMIC_NAME_SCOPE(*Token.ToString(), static_cast<FNetBitWriter&>(Ar), GetTraceCollector(static_cast<FNetBitWriter&>(Ar)), ENetTraceVerbosity::VeryVerbose);
	uint32 TokenIndex = Token.GetIndex();
	Ar.SerializeIntPacked(TokenIndex);
	if (TokenIndex != FNetToken::InvalidTokenIndex)
	{
		bool bIsAssignedByAuthority = Token.IsAssignedByAuthority();
		Ar.SerializeBits(&bIsAssignedByAuthority, 1);

		if (bWriteTokenType)
		{
			uint32 TokenTypeId = Token.GetTypeId();
			Ar.SerializeBits(&TokenTypeId, FNetToken::TokenTypeIdBits);
		}
	}
}

FNetToken FNetTokenDataStore::ReadNetToken(UE::Net::FNetSerializationContext& Context)
{
	return FNetTokenStore::InternalReadNetToken(Context, GetTypeId());
}

void FNetTokenDataStore::WriteNetToken(UE::Net::FNetSerializationContext& Context, FNetToken Token)
{
	const bool bWriteTokenTypeId = false;
	return FNetTokenStore::InternalWriteNetToken(Context, Token, bWriteTokenTypeId);
}

FNetToken FNetTokenDataStore::ReadNetToken(FArchive& Ar)
{
	return FNetTokenStore::InternalReadNetToken(Ar, GetTypeId());
}

void FNetTokenDataStore::WriteNetToken(FArchive& Ar, FNetToken Token)
{
	const bool bWriteTokenTypeId = false;
	FNetTokenStore::InternalWriteNetToken(Ar, Token, bWriteTokenTypeId);
}

FNetTokenStore::FNetTokenStore()
: LocalNetTokenStoreState(new FNetTokenStoreState)
{
}

FNetTokenStore::~FNetTokenStore()
{
}

void FNetTokenStore::Init(FNetTokenStore::FInitParams& InParams)
{
	Params = InParams;
	RemoteNetTokenStoreStates.SetNum(InParams.MaxConnections);
}

void FNetTokenStore::InitRemoteNetTokenStoreState(uint32 ConnectionId)
{
	if (ensureMsgf((ConnectionId != InvalidConnectionId) && (ConnectionId < (uint32)RemoteNetTokenStoreStates.Num()), TEXT("Trying to init RemoteNetTokenStoreState for invalid connection %u"), ConnectionId))
	{
		if (FNetTokenStoreState* ExistingState = RemoteNetTokenStoreStates[ConnectionId].Get())
		{
			ExistingState->Reset();
		}
		else
		{
			RemoteNetTokenStoreStates[ConnectionId] = MakeUnique<FNetTokenStoreState>();
		}
	}
}

const FNetTokenStoreState* FNetTokenStore::GetRemoteNetTokenStoreState(uint32 ConnectionId) const
{
	if (!ensureMsgf((ConnectionId != InvalidConnectionId) && (ConnectionId < (uint32)RemoteNetTokenStoreStates.Num()), TEXT("Trying to access RemoteNetTokenStoreState for ConnectionID: %u"), ConnectionId))
	{
		return nullptr;
	}
	return RemoteNetTokenStoreStates[ConnectionId].Get();
}

FNetTokenStoreState* FNetTokenStore::GetRemoteNetTokenStoreState(uint32 ConnectionId)
{
	if (!ensureMsgf((ConnectionId != InvalidConnectionId) && (ConnectionId < (uint32)RemoteNetTokenStoreStates.Num()), TEXT("Trying to access non existing RemoteNetTokenStoreState for ConnectionId: %u"), ConnectionId))
	{
		return nullptr;
	}
	return RemoteNetTokenStoreStates[ConnectionId].Get();
}

const FNetTokenDataStore* FNetTokenStore::GetDataStore(FName Name) const
{
	const TTuple<FName, TUniquePtr<FNetTokenDataStore>>* Entry = TokenDataStores.FindByPredicate([Name](const TTuple<FName, TUniquePtr<FNetTokenDataStore>>& Entry){ return Name == Entry.Get<0>(); });
	return Entry ? Entry->Get<1>().Get() : nullptr;
}

 FNetTokenDataStore* FNetTokenStore::GetDataStore(FName Name)
{
	TTuple<FName, TUniquePtr<FNetTokenDataStore>>* Entry = TokenDataStores.FindByPredicate([Name](TTuple<FName, TUniquePtr<FNetTokenDataStore>>& Entry){ return Name == Entry.Get<0>(); });
	return Entry ? Entry->Get<1>().Get() : nullptr;
}

bool FNetTokenStore::RegisterDataStore(TUniquePtr<FNetTokenDataStore> DataStore, FName TokenStoreName)
{
	if (TokenDataStores.Num() >= FNetToken::MaxTypeIdCount)
	{
		return false;
	}

	if (!DataStore)
	{
		return false;
	}

	if (!ensure(GetDataStore(TokenStoreName) == nullptr))
	{
		// Already registered
		return false;
	}

	if (!ensure(DataStore->GetTypeId() == FNetToken::InvalidTokenTypeId))
	{
		// Already registered
		return false;
	}

	DataStore->TypeId = TokenDataStores.Num();
	TokenDataStores.Emplace(TokenStoreName, MoveTemp(DataStore));

	return true;
}

void FNetTokenStore::WriteTokenData(FNetSerializationContext& Context, const FNetToken NetToken) const
{
	if (NetToken.IsValid())
	{
		FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

		const FNetTokenStoreKey& TokenKey = LocalNetTokenStoreState->TokenInfoArray[NetToken.GetTypeId()][NetToken.GetIndex()];

		// Write token data
		TokenDataStores[NetToken.GetTypeId()].Get<1>().Get()->WriteTokenData(Context, TokenKey);
	}
}

void FNetTokenStore::WriteTokenData(FArchive& Ar, const FNetToken NetToken) const
{
	if (NetToken.IsValid())
	{
		const FNetToken::FTypeId TokenTypeId = NetToken.GetTypeId();

		// We cannot write token data for bad tokens
		if ((TokenTypeId >= FNetToken::MaxTypeIdCount) || (NetToken.GetIndex() >= (uint32)LocalNetTokenStoreState->TokenInfoArray[TokenTypeId].Num()))
		{
			UE_LOG(LogNetToken, Error, TEXT("Trying to write data for unknown NetToken %s"), *NetToken.ToString());
			return;
		}

		const FNetTokenStoreKey TokenKey = LocalNetTokenStoreState->TokenInfoArray[NetToken.GetTypeId()][NetToken.GetIndex()];

		// Write token data
		TokenDataStores[NetToken.GetTypeId()].Get<1>()->WriteTokenData(Ar, TokenKey);
	}
}

bool FNetTokenStore::ValidateAndStoreNetTokenData(FNetTokenDataStore& DataStore, FNetTokenStoreState& RemoteNetTokenStoreState, const FNetToken NetToken, const FNetTokenStoreKey StoreKey)
{
	if (!StoreKey.IsValid() || !RemoteNetTokenStoreState.ReserveTokenCount(NetToken.GetTypeId(), NetToken.GetIndex() + 1))
	{
		return false;
	}

	TArray<FNetTokenStoreKey>& TokenStoreKeysForType = RemoteNetTokenStoreState.TokenInfoArray[NetToken.GetTypeId()];

	// Since the same tokendata might be exported multiple times we better validate that it is the same data
	const FNetTokenStoreKey& ExistingStoreKey = TokenStoreKeysForType[NetToken.GetIndex()];
	if (!ensureAlways(!ExistingStoreKey.IsValid() || StoreKey == ExistingStoreKey))
	{
		return false;
	}

	// If this is an authtoken and we are not the authority, update the stored key so that we can use the authoriative key instead of the local key
	if (NetToken.IsAssignedByAuthority() && !IsAuthority())
	{
		UE_LOG(LogNetToken, Verbose, TEXT("FNetTokenStore::ReadTokenData - Replaced local key %s with %s"), *DataStore.StoredTokens[StoreKey.GetKeyIndex()].ToString(), *NetToken.ToString());	

		// This way the next time we lookup this key during assignment use the imported authoritative key which we do not have to export
		DataStore.StoredTokens[StoreKey.GetKeyIndex()] = NetToken;
	}
	
	// Store
	TokenStoreKeysForType[NetToken.GetIndex()] = StoreKey;

	return true;
}

void FNetTokenStore::ReadTokenData(FNetSerializationContext& Context, const FNetToken NetToken, FNetTokenStoreState& RemoteNetTokenStoreState)
{
	if (NetToken.IsValid())
	{
		FNetBitStreamReader* Reader = Context.GetBitStreamReader();

		FNetToken::FTypeId TokenTypeId = NetToken.GetTypeId();

		if (TokenTypeId >= (uint32)TokenDataStores.Num())
		{
			Reader->DoOverflow();
			UE_LOG(LogNetToken, Error, TEXT("Failed to read ReadTokenData for %s."), *NetToken.ToString());
			ensure(false);
			return;
		}

		FNetTokenDataStore* DataStore = TokenDataStores[TokenTypeId].Get<1>().Get();
		const FNetTokenStoreKey StoreKey = DataStore->ReadTokenData(Context, NetToken);

		if (!ValidateAndStoreNetTokenData(*DataStore, RemoteNetTokenStoreState, NetToken, StoreKey))
		{
			Reader->DoOverflow();
			UE_LOG(LogNetToken, Error, TEXT("Failed to read ReadTokenData for %s."), *NetToken.ToString());
			ensure(false);
			return;
		}
	}
}

void FNetTokenStore::ReadTokenData(FArchive& Ar, const FNetToken NetToken, FNetTokenStoreState& RemoteNetTokenStoreState)
{
	if (NetToken.IsValid())
	{
		// Read type
		FNetToken::FTypeId TokenTypeId = NetToken.GetTypeId();

		// Validate that we managed to read and verify the type
		if (Ar.IsError() || TokenTypeId >= (uint32)TokenDataStores.Num() )
		{
			Ar.SetError();
			UE_LOG(LogNetToken, Error, TEXT("Failed to read ReadTokenData for %s."), *NetToken.ToString());
			return;
		}

		FNetTokenDataStore* DataStore = TokenDataStores[TokenTypeId].Get<1>().Get();
		const FNetTokenStoreKey StoreKey = DataStore->ReadTokenData(Ar, NetToken);

		if (!ValidateAndStoreNetTokenData(*DataStore, RemoteNetTokenStoreState, NetToken, StoreKey))
		{
			Ar.SetError();
			UE_LOG(LogNetToken, Error, TEXT("Failed to read ReadTokenData for %s."), *NetToken.ToString());
			ensure(false);
			return;
		}
	}
}

void FNetTokenStore::ConditionalWriteNetTokenData(FNetSerializationContext& Context, Private::FNetExportContext* ExportContext, const FNetToken NetToken) const
{
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	// We should not try to export tokens received from remote
	if (IsAuthority() != NetToken.IsAssignedByAuthority())
	{
		Writer->WriteBool(false);
		return;
	}

	if (ExportContext)
	{
		if (Writer->WriteBool(!ExportContext->IsExported(NetToken)))
		{
			WriteTokenData(Context, NetToken);
			ExportContext->AddExported(NetToken);			
		}
	}
	else
	{
		Writer->WriteBool(true);
		WriteTokenData(Context, NetToken);
	}
}

void FNetTokenStore::ConditionalReadNetTokenData(FNetSerializationContext& Context, const FNetToken NetToken)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	const bool bIsExportToken = Reader->ReadBool();
	if (bIsExportToken)
	{
		if (Reader->IsOverflown())
		{
			return;
		}

		FNetObjectResolveContext& ResolveContext = Context.GetInternalContext()->ResolveContext;
	
		ReadTokenData(Context, NetToken, *ResolveContext.RemoteNetTokenStoreState);
	}
}

void FNetTokenStore::AppendExport(FNetSerializationContext& Context, FNetToken NetToken)
{
	using namespace UE::Net::Private;

	FNetExportContext* ExportContext = Context.GetExportContext();
	if (ensure(ExportContext))
	{
		ExportContext->AddPendingExport(NetToken);
	}
}

TArray<FNetToken> FNetTokenStore::GetAllNetTokens() const
{
	TArray<FNetToken> Result;

	const FNetTokenStoreState* TokenStoreState = GetLocalNetTokenStoreState();
	for (const TTuple<FName, TUniquePtr<FNetTokenDataStore>>& TokenDataStore : TokenDataStores)
	{
		const uint32 TypeId = TokenDataStore.Get<1>()->GetTypeId();
		const uint32 NumTokensForType = TokenStoreState->TokenInfoArray[TypeId].Num();
		if (NumTokensForType > 1)
		{
			Result.Append(MakeArrayView(&TokenDataStore.Get<1>()->StoredTokens[1], NumTokensForType - 1));
		}
	}

	return MoveTemp(Result);
}

}

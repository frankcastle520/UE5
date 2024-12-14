// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"
#include "Templates/UniquePtr.h"
#include "GenericPlatform/GenericPlatformCriticalSection.h"
#include "Serialization/Archive.h"

class  UDataflow;
struct FDataflowNode;
struct FDataflowOutput;
struct FDataflowConnection;

#define DATAFLOW_EDITOR_EVALUATION WITH_EDITOR

namespace UE::Dataflow
{
	class FContext;

	typedef uint32 FContextCacheKey;

	/** Trait used to select the UObject* or TObjectPtr cache element path code. */
	template <typename T>
	struct TIsUObjectPtrElement
	{
		typedef typename TDecay<T>::Type Type;
		static constexpr bool Value = (std::is_pointer_v<Type> && std::is_convertible_v<Type, const UObjectBase*>) || TIsTObjectPtr_V<Type>;
	};

	struct FTimestamp
	{
		typedef uint64 Type;
		Type Value = Type(0);

		FTimestamp(Type InValue) : Value(InValue) {}
		bool operator>=(const FTimestamp& InTimestamp) const { return Value >= InTimestamp.Value; }
		bool operator<(const FTimestamp& InTimestamp) const { return Value < InTimestamp.Value; }
		bool operator==(const FTimestamp& InTimestamp) const { return Value == InTimestamp.Value; }
		bool IsInvalid() { return Value == Invalid; }

		static DATAFLOWCORE_API Type Current();
		static DATAFLOWCORE_API Type Invalid; // 0
	};

	struct FRenderingParameter {
		FRenderingParameter() {}
		FRenderingParameter(FString InRenderName, FName InTypeName, const TArray<FName>& InOutputs)
			: Name(InRenderName), Type(InTypeName), Outputs(InOutputs) {}
		FRenderingParameter(FString InRenderName, FName InTypeName, TArray<FName>&& InOutputs)
			: Name(InRenderName), Type(InTypeName), Outputs(InOutputs) {}
		
		bool operator==(const FRenderingParameter& Other) const = default;

		FString Name = FString("");
		FName Type = FName("");
		TArray<FName> Outputs;
	};

	//--------------------------------------------------------------------
	// base class for all context cache entries 
	//--------------------------------------------------------------------
	struct FContextCacheElementBase 
	{
		enum EType
		{
			CacheElementTyped,
			CacheElementReference,
			CacheElementNull,
			CacheElementUObject
		};

		FContextCacheElementBase(EType CacheElementType, FGuid InNodeGuid = FGuid(), const FProperty* InProperty = nullptr, uint32 InNodeHash = 0, FTimestamp InTimestamp = FTimestamp::Invalid)
			: Type(CacheElementType)
			, NodeGuid(InNodeGuid)
			, Property(InProperty)
			, NodeHash(InNodeHash)
			, Timestamp(InTimestamp)
		{}
		virtual ~FContextCacheElementBase() {}

		// InReferenceDataKey is the key of the cache element this function is called on 
		virtual TUniquePtr<FContextCacheElementBase> CreateReference(FContextCacheKey InReferenceDataKey) const = 0;

		template<typename T>
		inline const T& GetTypedData(FContext& Context, const FProperty* PropertyIn, const T& Default) const;
		
		EType GetType() const {	return Type; }

		const FProperty* GetProperty() const { return Property; }
		const FTimestamp& GetTimestamp() const { return Timestamp; }
		void SetTimestamp(const FTimestamp& InTimestamp) { Timestamp = InTimestamp; }

		const FGuid& GetNodeGuid() const { return NodeGuid; }
		const uint32 GetNodeHash() const { return NodeHash; }

		// use this with caution: setting the property of a wrong type may cause problems
		void SetProperty(const FProperty* NewProperty) { Property = NewProperty; }

	private:
		friend struct FContextCache;

		EType Type;
		FGuid NodeGuid;
		const FProperty* Property = nullptr;
		uint32 NodeHash = 0;
		FTimestamp Timestamp = FTimestamp::Invalid;
	};

	//--------------------------------------------------------------------
	// Value storing context cache entry - strongly typed
	//--------------------------------------------------------------------
	template<class T>
	struct TContextCacheElement : public FContextCacheElementBase 
	{
		TContextCacheElement(FGuid InNodeGuid, const FProperty* InProperty, T&& InData, uint32 InNodeHash, FTimestamp Timestamp)
			: FContextCacheElementBase(EType::CacheElementTyped, InNodeGuid, InProperty, InNodeHash, Timestamp)
			, Data(Forward<T>(InData))
		{}
		
		inline const T& GetData(FContext& Context, const FProperty* PropertyIn, const T& Default) const;

		const T& GetDataDirect() const { return Data; }

		inline virtual TUniquePtr<FContextCacheElementBase> CreateReference(FContextCacheKey InReferenceDataKey) const override;

	private:
		typedef typename TDecay<T>::Type FDataType;  // Using universal references here means T could be either const& or an rvalue reference
		const FDataType Data;                        // Decaying T removes any reference and gets the correct underlying storage data type
	};

	//--------------------------------------------------------------------
	// Reference to another context cache entry 
	//--------------------------------------------------------------------
	template<class T>
	struct TContextCacheElementReference : public FContextCacheElementBase
	{
		TContextCacheElementReference(FGuid InNodeGuid, const FProperty* InProperty, FContextCacheKey InDataKey, uint32 InNodeHash, FTimestamp Timestamp)
			: FContextCacheElementBase(EType::CacheElementReference, InNodeGuid, InProperty, InNodeHash, Timestamp)
			, DataKey(InDataKey)
		{}

		inline const T& GetData(FContext& Context, const FProperty* PropertyIn, const T& Default) const;

		inline virtual TUniquePtr<FContextCacheElementBase> CreateReference(FContextCacheKey InReferenceDataKey) const override;

	private:
		const FContextCacheKey DataKey; // this is a key to another cache element
	};

	//--------------------------------------------------------------------
	// Null entry, this will always return a default value 
	//--------------------------------------------------------------------
	struct FContextCacheElementNull : public FContextCacheElementBase
	{
		FContextCacheElementNull(FGuid InNodeGuid, const FProperty* InProperty, FContextCacheKey InDataKey, uint32 InNodeHash, FTimestamp Timestamp)
			: FContextCacheElementBase(EType::CacheElementNull, InNodeGuid, InProperty, InNodeHash, Timestamp)
		{}

		inline virtual TUniquePtr<FContextCacheElementBase> CreateReference(FContextCacheKey InReferenceDataKey) const override;
	};

	//--------------------------------------------------------------------
	// UObject cache element, prevents the object from being garbage collected while in the cache
	//--------------------------------------------------------------------
	template<class T>
	struct TContextCacheElementUObject : public FContextCacheElementBase, public FGCObject
	{
		TContextCacheElementUObject(FGuid InNodeGuid, const FProperty* InProperty, T&& InObject, uint32 InNodeHash, FTimestamp Timestamp)
			: FContextCacheElementBase(EType::CacheElementUObject, InNodeGuid, InProperty, InNodeHash, Timestamp)
			, Object(InObject)
		{}

		const T& GetData(FContext& /*Context*/, const FProperty* /*PropertyIn*/, const T& /*Default*/) const { return Object; }

		inline virtual TUniquePtr<FContextCacheElementBase> CreateReference(FContextCacheKey InReferenceDataKey) const override;

		//~ Begin FGCObject interface
		virtual void AddReferencedObjects(FReferenceCollector& Collector) override { Collector.AddReferencedObject(Object); }
		virtual FString GetReferencerName() const override { return TEXT("TContextCacheElementUObject"); }
		//~ End FGCObject interface

	private:
		typedef typename TDecay<T>::Type FDataType;  // Using universal references here means T could be either const& or an rvalue reference
		FDataType Object;                            // Decaying T removes any reference and gets the correct underlying storage data type
	};

	// cache element method implementation 
	template<class T>
	const T& FContextCacheElementBase::GetTypedData(FContext& Context, const FProperty* PropertyIn, const T& Default) const
	{
		// check(PropertyIn->IsA<T>()); // @todo(dataflow) compile error for non-class T; find alternatives
		if (Type == EType::CacheElementTyped)
		{
			if constexpr (!TIsUObjectPtrElement<T>::Value)
			{
				return static_cast<const TContextCacheElement<T>&>(*this).GetData(Context, PropertyIn, Default);
			}
		}
		if (Type == EType::CacheElementReference)
		{
			return static_cast<const TContextCacheElementReference<T>&>(*this).GetData(Context, PropertyIn, Default);
		}
		if (Type == EType::CacheElementNull)
		{
			return Default; 
		}
		if (Type == EType::CacheElementUObject)
		{
			if constexpr (TIsUObjectPtrElement<T>::Value)
			{
				return static_cast<const TContextCacheElementUObject<T>&>(*this).GetData(Context, PropertyIn, Default);
			}
		}
		check(false); // should never happen
		return Default;
	}

	struct FContextCache : public TMap<FContextCacheKey, TUniquePtr<FContextCacheElementBase>>
	{
		DATAFLOWCORE_API void Serialize(FArchive& Ar);
	};
};

inline FArchive& operator<<(FArchive& Ar, UE::Dataflow::FTimestamp& ValueIn)
{
	Ar << ValueIn.Value;
	Ar << ValueIn.Invalid;
	return Ar;
}

inline FArchive& operator<<(FArchive& Ar, UE::Dataflow::FContextCache& ValueIn)
{
	ValueIn.Serialize(Ar);
	return Ar;
}


namespace UE::Dataflow
{

	class FContext
	{
	protected:
		FContext(FContext&&) = default;
		FContext& operator=(FContext&&) = default;
		
		FContext(const FContext&) = delete;
		FContext& operator=(const FContext&) = delete;

		FContextCache DataStore;

	public:
		FContext() = default;
		virtual ~FContext() = default;

		static FName StaticType() { return FName("FContext"); }

		virtual bool IsA(FName InType) const { return InType==StaticType(); }

		virtual FName GetType() const { return FContext::StaticType(); }

		virtual int32 GetKeys(TSet<FContextCacheKey>& InKeys) const { return DataStore.GetKeys(InKeys); }

		template<class T>
		const T* AsType() const
		{
			if (IsA(T::StaticType()))
			{
				return (T*)this;
			}
			return nullptr;
		}

		virtual void SetDataImpl(FContextCacheKey Key, TUniquePtr<FContextCacheElementBase>&& DataStoreEntry) = 0;
		
		template<typename T>
		void SetData(FContextCacheKey InKey, const FProperty* InProperty, T&& InValue, const FGuid& InNodeGuid, uint32 InNodeHash, const FTimestamp& InTimestamp)
		{
			TUniquePtr<FContextCacheElementBase> DataStoreEntry;
			if constexpr (TIsUObjectPtrElement<T>::Value)
			{
				DataStoreEntry = MakeUnique<TContextCacheElementUObject<T>>(InNodeGuid, InProperty, Forward<T>(InValue), InNodeHash, InTimestamp);
			}
			else
			{
				DataStoreEntry = MakeUnique<TContextCacheElement<T>>(InNodeGuid, InProperty, Forward<T>(InValue), InNodeHash, InTimestamp);
			}

			SetDataImpl(InKey, MoveTemp(DataStoreEntry));
		}

		void SetDataReference(FContextCacheKey Key, const FProperty* Property, FContextCacheKey ReferenceKey)
		{
			// find the reference key to get 
			if (TUniquePtr<FContextCacheElementBase>* CacheElement = GetDataImpl(ReferenceKey))
			{
				TUniquePtr<FContextCacheElementBase> CacheReferenceElement = (*CacheElement)->CreateReference(ReferenceKey);
				SetDataImpl(Key, MoveTemp(CacheReferenceElement));
			}
			else
			{
				ensure(false); // could not find the original cache element 
			}
		}

		// this is useful when there's a need to have to have cache entry but  the type is not known and there no connected output
		// ( like reroute nodes with unconnected input for example ) 
		// in that case posting an invalid reference, will allow the evaluatino to go through and the node reading it will get a default value instead
		void SetNullData(FContextCacheKey InKey, const FProperty* InProperty, const FGuid& InNodeGuid, uint32 InNodeHash, const FTimestamp& InTimestamp)
		{
			TUniquePtr<FContextCacheElementNull> CacheNullElement = MakeUnique<FContextCacheElementNull>(InNodeGuid, InProperty, InKey, InNodeHash, InTimestamp);
			SetDataImpl(InKey, MoveTemp(CacheNullElement));
		}

		virtual TUniquePtr<FContextCacheElementBase>* GetDataImpl(FContextCacheKey Key) = 0;

		template<class T>
		const T& GetData(FContextCacheKey Key, const FProperty* InProperty, const T& Default = T())
		{
			if (TUniquePtr<FContextCacheElementBase>* Cache = GetDataImpl(Key))
			{
				return (*Cache)->GetTypedData<T>(*this, InProperty, Default);
			}
			return Default;
		}

		virtual bool HasDataImpl(FContextCacheKey Key, FTimestamp InTimestamp = FTimestamp::Invalid) = 0;
		
		bool HasData(FContextCacheKey Key, FTimestamp InTimestamp = FTimestamp::Invalid)
		{
			FContextCacheKey IntKey = (FContextCacheKey)Key;
			return HasDataImpl(Key, InTimestamp);
		}

		virtual bool IsEmptyImpl() const = 0;

		bool IsEmpty() const
		{
			return IsEmptyImpl();
		}

		virtual void Serialize(FArchive& Ar)
		{
			FTimestamp Timestamp = FTimestamp::Invalid;
			Ar << Timestamp;
			Ar << DataStore;
		}

		DATAFLOWCORE_API FTimestamp GetTimestamp(FContextCacheKey Key) const;

		virtual void Evaluate(const FDataflowNode* Node, const FDataflowOutput* Output) = 0;
		virtual bool Evaluate(const FDataflowOutput& Connection) = 0;

		DATAFLOWCORE_API void PushToCallstack(const FDataflowConnection* Connection);
		DATAFLOWCORE_API void PopFromCallstack(const FDataflowConnection* Connection);
		DATAFLOWCORE_API bool IsInCallstack(const FDataflowConnection* Connection) const;

		DATAFLOWCORE_API bool IsCacheEntryAfterTimestamp(FContextCacheKey InKey, const FTimestamp InTimestamp);

	private:
#if DATAFLOW_EDITOR_EVALUATION
		TArray<const FDataflowConnection*> Callstack;
#endif
	};

	struct FContextScopedCallstack
	{
	public:
		DATAFLOWCORE_API FContextScopedCallstack(FContext& InContext, const FDataflowConnection* InConnection);
		DATAFLOWCORE_API ~FContextScopedCallstack();

		bool IsLoopDetected() const { return bLoopDetected; }

	private:
		bool bLoopDetected;
		FContext& Context;
		const FDataflowConnection* Connection;
	};

#define DATAFLOW_CONTEXT_INTERNAL(PARENTTYPE, TYPENAME)														\
	typedef PARENTTYPE Super;																				\
	static FName StaticType() { return FName(#TYPENAME); }													\
	virtual bool IsA(FName InType) const override { return InType==StaticType() || Super::IsA(InType); }	\
	virtual FName GetType() const override { return StaticType(); }

	class FContextSingle : public FContext
	{

	public:
		DATAFLOW_CONTEXT_INTERNAL(FContext, FContextSingle);

		FContextSingle() = default;

		virtual void SetDataImpl(FContextCacheKey Key, TUniquePtr<FContextCacheElementBase>&& DataStoreEntry) override
		{
			DataStore.Emplace(Key, MoveTemp(DataStoreEntry));
		}

		virtual TUniquePtr<FContextCacheElementBase>* GetDataImpl(FContextCacheKey Key) override
		{
			return DataStore.Find(Key);
		}

		virtual bool HasDataImpl(FContextCacheKey Key, FTimestamp InTimestamp = FTimestamp::Invalid) override
		{
			return DataStore.Contains(Key) && DataStore[Key]->GetTimestamp() >= InTimestamp;
		}

		virtual bool IsEmptyImpl() const override
		{
			return DataStore.IsEmpty();
		}

		DATAFLOWCORE_API virtual void Evaluate(const FDataflowNode* Node, const FDataflowOutput* Output) override;
		DATAFLOWCORE_API virtual bool Evaluate(const FDataflowOutput& Connection) override;

	};
	
	class FContextThreaded : public FContext
	{
		TSharedPtr<FCriticalSection> CacheLock;

	public:
		DATAFLOW_CONTEXT_INTERNAL(FContext, FContextThreaded);


		FContextThreaded()
			: FContext()
		{
			CacheLock = MakeShared<FCriticalSection>();
		}

		virtual void SetDataImpl(FContextCacheKey Key, TUniquePtr<FContextCacheElementBase>&& DataStoreEntry) override
		{
			CacheLock->Lock(); ON_SCOPE_EXIT { CacheLock->Unlock(); };
			

			// Threaded evaluation can only set an output once per context evaluation. Otherwise
			// downstream nodes that are extracting the data will get currupted store entries. 
			TUniquePtr<FContextCacheElementBase>* CurrentData = DataStore.Find(Key);
			if (!CurrentData || !(*CurrentData) || (*CurrentData)->GetTimestamp() < DataStoreEntry->GetTimestamp())
			{
				DataStore.Emplace(Key, MoveTemp(DataStoreEntry));
			}
		}

		virtual TUniquePtr<FContextCacheElementBase>* GetDataImpl(FContextCacheKey Key) override
		{
			CacheLock->Lock(); ON_SCOPE_EXIT { CacheLock->Unlock(); };
			
			return DataStore.Find(Key);
		}

		virtual bool HasDataImpl(FContextCacheKey Key, FTimestamp InTimestamp = FTimestamp::Invalid) override
		{
			CacheLock->Lock(); ON_SCOPE_EXIT { CacheLock->Unlock(); };
			
			return DataStore.Contains(Key) && DataStore[Key]->GetTimestamp() >= InTimestamp;
		}

		virtual bool IsEmptyImpl() const override
		{
			return DataStore.IsEmpty();
		}

		DATAFLOWCORE_API virtual void Evaluate(const FDataflowNode* Node, const FDataflowOutput* Output) override;
		DATAFLOWCORE_API virtual bool Evaluate(const FDataflowOutput& Connection) override;

	};

	// cache classes implemetation 
	// this needs to be after the FContext definition because they access its methods

	template<class T>
	const T& TContextCacheElement<T>::GetData(FContext& Context, const FProperty* PropertyIn, const T& Default) const
	{
		return Data;
	}

	template<class T>
	TUniquePtr<FContextCacheElementBase> TContextCacheElement<T>::CreateReference(FContextCacheKey InReferenceDataKey) const
	{
		return MakeUnique<TContextCacheElementReference<T>>(GetNodeGuid(), GetProperty(), InReferenceDataKey, GetNodeHash(), GetTimestamp());
	}

	template<class T>
	const T& TContextCacheElementReference<T>::GetData(FContext& Context, const FProperty* PropertyIn, const T& Default) const
	{
		return Context.GetData(DataKey, PropertyIn, Default);
	}

	template<class T>
	TUniquePtr<FContextCacheElementBase> TContextCacheElementReference<T>::CreateReference(FContextCacheKey InReferenceDataKey) const
	{
		return MakeUnique<TContextCacheElementReference<T>>(GetNodeGuid(), GetProperty(), InReferenceDataKey, GetNodeHash(), GetTimestamp());
	}

	inline TUniquePtr<FContextCacheElementBase> FContextCacheElementNull::CreateReference(FContextCacheKey InReferenceDataKey) const
	{
		// a null entry will always return a null entry as a reference
		return MakeUnique<FContextCacheElementNull>(GetNodeGuid(), GetProperty(), InReferenceDataKey, GetNodeHash(), GetTimestamp());
	}

	template<class T>
	TUniquePtr<FContextCacheElementBase> TContextCacheElementUObject<T>::CreateReference(FContextCacheKey InReferenceDataKey) const
	{
		return MakeUnique<TContextCacheElementReference<T>>(GetNodeGuid(), GetProperty(), InReferenceDataKey, GetNodeHash(), GetTimestamp());
	}
}



// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonSerializerReader.h"
#include "JsonSerializerWriter.h"
#include "Misc/TVariant.h"

/**
 * Base class for a JSON serializable object
 */
struct FJsonSerializable
{
	/**
	 *	Virtualize destructor as we provide overridable functions
	 */
	JSON_API virtual ~FJsonSerializable();

	/**
	 * Used to allow serialization of a const ref
	 *
	 * @return the corresponding json string
	 */
	JSON_API const FString ToJson(bool bPrettyPrint = true) const;

	/**
	 * Serializes this object to its JSON string form
	 *
	 * @param bPrettyPrint - If true, will use the pretty json formatter
	 * @return the corresponding json string
	 */
	JSON_API virtual const FString ToJson(bool bPrettyPrint=true);

	/**
	 * Serializes this object with a Json Writer
	 * 
	 * @param JsonWriter - The writer to use
	 * @param bFlatObject if true then no object wrapper is used
	 */
	template<class CharType, class PrintPolicy, ESPMode SPMode>
	void ToJson(TSharedRef<TJsonWriter<CharType, PrintPolicy>, SPMode> JsonWriter, bool bFlatObject = false) const;

	/**
	 * Serializes the contents of a JSON string into this object
	 *
	 * @param Json the JSON data to serialize from
	 */
	JSON_API virtual bool FromJson(const FString& Json);

	/**
	 * Serializes the contents of a JSON string into this object
	 *
	 * @param Json the JSON data to serialize from
	 */
	JSON_API virtual bool FromJson(FString&& Json);

	/**
	 * Serializes the contents of a JSON string into this object using FUtf8StringView
	 *
	 * @param JsonStringView the JSON data to serialize from
	 */
	JSON_API bool FromJsonStringView(FUtf8StringView JsonStringView);

	/**
	 * Serializes the contents of a JSON string into this object using FWideStringView
	 *
	 * @param JsonStringView the JSON data to serialize from
	 */
	JSON_API bool FromJsonStringView(FWideStringView JsonStringView);

	JSON_API virtual bool FromJson(TSharedPtr<FJsonObject> JsonObject);

	/**
	 * Abstract method that needs to be supplied using the macros
	 *
	 * @param Serializer the object that will perform serialization in/out of JSON
	 * @param bFlatObject if true then no object wrapper is used
	 */
	JSON_API virtual void Serialize(FJsonSerializerBase& Serializer, bool bFlatObject) = 0;
};

template<class CharType, class PrintPolicy, ESPMode SPMode>
inline void FJsonSerializable::ToJson(TSharedRef<TJsonWriter<CharType, PrintPolicy>, SPMode> JsonWriter, bool bFlatObject) const
{
	FJsonSerializerWriter<CharType, PrintPolicy> Serializer(MoveTemp(JsonWriter));
	const_cast<FJsonSerializable*>(this)->Serialize(Serializer, bFlatObject);
}

namespace UE::JsonArray
{

namespace Private
{

template<typename T, typename CharType>
inline bool FromJson(TArray<T>& OutArray, TStringView<CharType> JsonString)
{
	OutArray.Reset();

	TArray<TSharedPtr<FJsonValue>> ArrayValues;
	TSharedRef<TJsonReader<CharType>> JsonReader = TJsonReaderFactory<CharType>::CreateFromView(JsonString);
	if (FJsonSerializer::Deserialize(JsonReader, ArrayValues))
	{
		for (const TSharedPtr<FJsonValue>& Value : ArrayValues)
		{
			TSharedPtr<FJsonObject>* ArrayEntry;
			if (Value.IsValid() && Value->TryGetObject(ArrayEntry))
			{
				if (ArrayEntry && ArrayEntry->IsValid())
				{
					FJsonSerializerReader Serializer(*ArrayEntry);
					OutArray.Add_GetRef(T()).Serialize(Serializer, false);
				}
				else
				{
					UE_LOG(LogJson, Error, TEXT("Failed to parse Json from array"));
					return false;
				}
			}
		}

		return true;
	}

	return false;
}

using ReturnStringArgs = TTuple<FString* /*OutValue*/, bool /*bPrettyPrint*/>;

using PrettyWriter = TSharedRef<TJsonWriter<>>;
using CondensedWriter = TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>>;
using WriterVariants = TVariant<PrettyWriter, CondensedWriter>;

using ToJsonVariantArgs = TVariant<ReturnStringArgs, WriterVariants>;

using PrettySerializer = FJsonSerializerWriter<>;
using CondensedSerializer = FJsonSerializerWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

template<typename T, typename...SerializerArgsT>
inline void ToJson_SerializeArrayElements(TArray<T>& InArray, SerializerArgsT...Args)
{
	for (T& ArrayEntry : InArray)
	{
		ArrayEntry.Serialize(Args...);
	}
}

template<typename T, typename...SerializerArgsT>
inline void ToJson_SerializeArrayElements(TArray<T*>& InArray, SerializerArgsT...Args)
{
	for (T* ArrayEntry : InArray)
	{
		ArrayEntry->Serialize(Args...);
	}
}

template<typename T>
inline void ToJson(TArray<T>& InArray, const ToJsonVariantArgs& InArgs)
{
	using PrettySerializerAndWriter = TTuple<PrettySerializer, PrettyWriter>;
	using CondensedSerializerAndWriter = TTuple<CondensedSerializer, CondensedWriter>;

	using SerializerVariant = TVariant<PrettySerializerAndWriter, CondensedSerializerAndWriter>;

	SerializerVariant SerializerToUse = ::Visit([](auto& StoredValue)
		{
			using StoredValueType = std::decay_t<decltype(StoredValue)>;
			if constexpr (std::is_same_v<StoredValueType, ReturnStringArgs>)
			{
				if (StoredValue.template Get<1>())
				{
					PrettyWriter NewWriter = TJsonWriterFactory<>::Create(StoredValue.template Get<0>());
					return SerializerVariant(TInPlaceType<PrettySerializerAndWriter>(), PrettySerializer(NewWriter), NewWriter);
				}
				else
				{
					CondensedWriter NewWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(StoredValue.template Get<0>());
					return SerializerVariant(TInPlaceType<CondensedSerializerAndWriter>(), CondensedSerializer(NewWriter), NewWriter);
				}
			}
			else
			{
				return ::Visit([](auto& StoredWriter)
					{
						using StoredWriterType = std::decay_t<decltype(StoredWriter)>;
						if constexpr (std::is_same_v<StoredWriterType, PrettyWriter>)
						{
							return SerializerVariant(TInPlaceType<PrettySerializerAndWriter>(), PrettySerializer(StoredWriter), StoredWriter);
						}
						else if constexpr (std::is_same_v<StoredWriterType, CondensedWriter>)
						{
							return SerializerVariant(TInPlaceType<CondensedSerializerAndWriter>(), CondensedSerializer(StoredWriter), StoredWriter);
						}
					}, StoredValue);
			}
		}, InArgs);


	const bool bCloseWriter = InArgs.IsType<ReturnStringArgs>();

	::Visit([bCloseWriter, &InArray](auto& StoredSerializer)
		{
			StoredSerializer.template Get<0>().StartArray();

			ToJson_SerializeArrayElements(InArray, StoredSerializer.template Get<0>(), false);

			StoredSerializer.template Get<0>().EndArray();

			if (bCloseWriter)
			{
				StoredSerializer.template Get<1>()->Close();
			}
		}, SerializerToUse);
}
} // namespace Private

template<typename T>
static bool FromJson(TArray<T>& OutArray, const FString& JsonString)
{
	return Private::FromJson(OutArray, FStringView(JsonString));
}

template<typename T>
static bool FromJson(TArray<T>& OutArray, FString&& JsonString)
{
	return Private::FromJson(OutArray, FStringView(MoveTemp(JsonString)));
}

template<typename T>
static bool FromJson(TArray<T>& OutArray, FUtf8StringView JsonStringView)
{
	return Private::FromJson(OutArray, JsonStringView);
}

template<typename T>
static bool FromJson(TArray<T>& OutArray, FWideStringView JsonStringView)
{
	return Private::FromJson(OutArray, JsonStringView);
}

/* non-const due to T::Serialize being a non-const function */
template<typename T>
static const FString ToJson(TArray<T>& InArray, const bool bPrettyPrint = true)
{
	FString JsonStr;
	Private::ToJson(InArray, Private::ToJsonVariantArgs(TInPlaceType<Private::ReturnStringArgs>(), &JsonStr, bPrettyPrint));
	return JsonStr;
}

template<typename T>
static void ToJson(TArray<T>& InArray, Private::PrettyWriter& JsonWriter)
{
	Private::ToJson(InArray, Private::ToJsonVariantArgs(TInPlaceType<Private::WriterVariants>(), Private::WriterVariants(TInPlaceType<Private::PrettyWriter>(), JsonWriter)));
}

template<typename T>
static void ToJson(TArray<T>& InArray, Private::CondensedWriter& JsonWriter)
{
	Private::ToJson(InArray, Private::ToJsonVariantArgs(TInPlaceType<Private::WriterVariants>(), Private::WriterVariants(TInPlaceType<Private::CondensedWriter>(), JsonWriter)));
}

} // namespace UE::JsonArray

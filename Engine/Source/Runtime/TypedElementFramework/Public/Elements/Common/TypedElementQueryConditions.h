// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Common/TypedElementCommonTypes.h"
#include "Templates/SubclassOf.h"
#include "UObject/WeakObjectPtrTemplates.h"

class IEditorDataStorageProvider;
class UScriptStruct;

namespace UE::Editor::DataStorage::Queries
{
	struct FColumnBase
	{
		TWeakObjectPtr<const UScriptStruct> TypeInfo = nullptr;
		FName Identifier = NAME_None;

	protected:
		FColumnBase() = default;
		constexpr explicit FColumnBase(TWeakObjectPtr<const UScriptStruct> ColumnTypeInfo, const FName& InIdentifier)
			: TypeInfo(ColumnTypeInfo)
			, Identifier(InIdentifier)
		{}
	};

	template<typename T = void>
	struct TColumn final : public FColumnBase
	{
		template <typename U = T> requires (!std::is_same_v<U, void>)
			constexpr explicit TColumn(const FName& Identifier = NAME_None) : FColumnBase(T::StaticStruct(), Identifier) {}
		
		template <typename U = T> requires (std::is_same_v<U, void>)
			constexpr explicit TColumn(TWeakObjectPtr<const UScriptStruct> ColumnTypeInfo, const FName& Identifier = NAME_None)
				: FColumnBase(ColumnTypeInfo, Identifier)
		{}
	};

	/*
	 * Compile Context used to resolve dynamic columns when FConditions are compiled
	 */
	class IQueryConditionCompileContext 
	{
	public:
		TYPEDELEMENTFRAMEWORK_API virtual ~IQueryConditionCompileContext() = default;
		TYPEDELEMENTFRAMEWORK_API virtual const UScriptStruct* GenerateDynamicColumn(const FDynamicColumnDescription&) const = 0;
	};

	/*
	 * Specialized compile context that accepts an IEditorDataStorageProvider
	 */
	class FEditorStorageQueryConditionCompileContext : public IQueryConditionCompileContext 
	{
	public:
		TYPEDELEMENTFRAMEWORK_API explicit FEditorStorageQueryConditionCompileContext(IEditorDataStorageProvider* InDataStorage);
		TYPEDELEMENTFRAMEWORK_API virtual const UScriptStruct* GenerateDynamicColumn(const FDynamicColumnDescription& Description) const override;
		
	private:
		IEditorDataStorageProvider* DataStorage;
	};


	/**
	 * Product of boolean combination of multiple columns. This can be used to verify if a collection of columns match
	 * the stored columns.
	 * NOTE: You must call Compile() before you call any members accessing the conditions
	 */
	class FConditions final
	{
	public:
		using ContainsCallback = TFunctionRef<bool(uint8_t ColumnIndex, TWeakObjectPtr<const UScriptStruct> Column)>;

		TYPEDELEMENTFRAMEWORK_API friend FConditions operator&&(const FConditions& Lhs, FColumnBase Rhs);
		TYPEDELEMENTFRAMEWORK_API friend FConditions operator&&(const FConditions& Lhs, const FConditions& Rhs);
		TYPEDELEMENTFRAMEWORK_API friend FConditions operator&&(FColumnBase Lhs, FColumnBase Rhs);
		TYPEDELEMENTFRAMEWORK_API friend FConditions operator&&(FColumnBase Lhs, const FConditions& Rhs);

		TYPEDELEMENTFRAMEWORK_API friend FConditions operator||(const FConditions& Lhs, FColumnBase Rhs);
		TYPEDELEMENTFRAMEWORK_API friend FConditions operator||(const FConditions& Lhs, const FConditions& Rhs);
		TYPEDELEMENTFRAMEWORK_API friend FConditions operator||(FColumnBase Lhs, FColumnBase Rhs);
		TYPEDELEMENTFRAMEWORK_API friend FConditions operator||(FColumnBase Lhs, const FConditions& Rhs);

		TYPEDELEMENTFRAMEWORK_API FConditions();
		// Not marked as "explicit" to allow conversion from a column. This means that conditions with a single
		// argument can be written in the same way as ones that use combinations.
		TYPEDELEMENTFRAMEWORK_API FConditions(FColumnBase Column);

		// Compile must be called before using any functions that access the columns
		TYPEDELEMENTFRAMEWORK_API FConditions& Compile(const IQueryConditionCompileContext& CompileContext);

		// Check whether these query conditions have been compiled
		TYPEDELEMENTFRAMEWORK_API bool IsCompiled() const;

		/** Convert the conditions into a string and append them to the provided string. */
		TYPEDELEMENTFRAMEWORK_API void AppendToString(FString& Output) const;

		/** Runs the provided list of columns through the conditions and returns true if a valid combination of columns is found. */
		TYPEDELEMENTFRAMEWORK_API bool Verify(TConstArrayView<FColumnBase> AvailableColumns) const;
		/**
		 * Runs the provided list of columns through the conditions and returns true if a valid combination of columns is found.
		 * This version returns a list of the columns that were used to match the condition.
		 */
		TYPEDELEMENTFRAMEWORK_API bool Verify(
			TArray<TWeakObjectPtr<const UScriptStruct>>& MatchedColumns,
			TConstArrayView<FColumnBase> AvailableColumns,
			bool AvailableColumnsAreSorted = false) const;
		/** Runs the provided list of columns through the conditions and returns true if a valid combination of columns is found. */
		TYPEDELEMENTFRAMEWORK_API bool Verify(TConstArrayView<TWeakObjectPtr<const UScriptStruct>> AvailableColumns,
			bool AvailableColumnsAreSorted = false) const;
		/**
		 * Runs the provided list of columns through the conditions and returns true if a valid combination of columns is found.
		 * This version returns a list of the columns that were used to match the condition.
		 */
		TYPEDELEMENTFRAMEWORK_API bool Verify(
			TArray<TWeakObjectPtr<const UScriptStruct>>& MatchedColumns,
			TConstArrayView<TWeakObjectPtr<const UScriptStruct>> AvailableColumns,
			bool AvailableColumnsAreSorted = false) const;
		/** Runs the provided list of columns through the conditions and returns true if a valid combination of columns is found. */
		TYPEDELEMENTFRAMEWORK_API bool Verify(TSet<TWeakObjectPtr<const UScriptStruct>> AvailableColumns) const;
		/** Runs through the list of query conditions and uses the callback to verify if a column is available. */
		TYPEDELEMENTFRAMEWORK_API bool Verify(ContainsCallback Callback) const;

		/** Returns the minimum number of columns needed for a successful match. */
		TYPEDELEMENTFRAMEWORK_API uint8_t MinimumColumnMatchRequired() const;
		/** Returns a list of all columns used. This can include duplicate columns. */
		TYPEDELEMENTFRAMEWORK_API TConstArrayView<TWeakObjectPtr<const UScriptStruct>> GetColumns() const;
		/** Whether or not there are any columns registered for operation.*/
		TYPEDELEMENTFRAMEWORK_API bool IsEmpty() const;

	private:
		void AppendName(FString& Output, TWeakObjectPtr<const UScriptStruct> TypeInfo) const;

		bool EntersScopeNext(uint8_t Index) const;
		bool EntersScope(uint8_t Index) const;
		bool Contains(TWeakObjectPtr<const UScriptStruct> ColumnType, const TArray<FColumnBase>& AvailableColumns) const;

		bool VerifyBootstrap(ContainsCallback Contains) const;
		bool VerifyRange(uint8_t& TokenIndex, uint8_t& ColumnIndex, ContainsCallback Contains) const;

		void ConvertColumnBitToArray(TArray<TWeakObjectPtr<const UScriptStruct>>& MatchedColumns, uint64 ColumnBits) const;

		uint8_t MinimumColumnMatchRequiredRange(uint8_t& Front) const;

		static void AppendQuery(FConditions& Target, const FConditions& Source);

		static constexpr SIZE_T MaxColumnCount = 32;
		static constexpr SIZE_T MaxTokenCount = 64;

		enum class Token : uint8_t
		{
			None,
			And,
			Or,
			ScopeOpen,
			ScopeClose
		};
		using ColumnArray = TWeakObjectPtr<const UScriptStruct>[MaxColumnCount];
		using TokenArray = Token[MaxTokenCount];
		using IdentifierArray = TArray<FName, TInlineAllocator<MaxColumnCount>>;

		ColumnArray Columns{ nullptr };
		TokenArray Tokens{ Token::None };
		IdentifierArray Identifiers;
		uint8_t ColumnCount = 0;
		uint8_t TokenCount = 0;
		bool bIsCompiled = false;
	};

	TYPEDELEMENTFRAMEWORK_API FConditions operator&&(const FConditions& Lhs, FColumnBase Rhs);
	TYPEDELEMENTFRAMEWORK_API FConditions operator&&(const FConditions& Lhs, const FConditions& Rhs);
	TYPEDELEMENTFRAMEWORK_API FConditions operator&&(FColumnBase Lhs, FColumnBase Rhs);
	TYPEDELEMENTFRAMEWORK_API FConditions operator&&(FColumnBase Lhs, const FConditions& Rhs);

	TYPEDELEMENTFRAMEWORK_API FConditions operator||(const FConditions& Lhs, FColumnBase Rhs);
	TYPEDELEMENTFRAMEWORK_API FConditions operator||(const FConditions& Lhs, const FConditions& Rhs);
	TYPEDELEMENTFRAMEWORK_API FConditions operator||(FColumnBase Lhs, FColumnBase Rhs);
	TYPEDELEMENTFRAMEWORK_API FConditions operator||(FColumnBase Lhs, const FConditions& Rhs);
} // namespace UE::Editor::DataStorage::Queries

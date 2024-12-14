// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Common/TypedElementHandles.h"

#include "TypedElementCommonTypes.generated.h"

/**
 * Base for the data structures for a column.
 */
USTRUCT()
struct FEditorDataStorageColumn
{
	GENERATED_BODY()
};

/**
 * Base for the data structures that act as tags to rows. Tags should not have any data.
 */
USTRUCT()
struct FEditorDataStorageTag
{
	GENERATED_BODY()
};

namespace UE
{
	// Work around missing header/implementations on some platforms
	namespace detail
	{
		template<typename T, typename U>
		concept SameHelper = std::is_same_v<T, U>;
	}
	template<typename T, typename U>
	concept same_as = detail::SameHelper<T, U> && detail::SameHelper<U, T>;

	template<typename From, typename To>
	concept convertible_to = std::is_convertible_v<From, To> && requires { static_cast<To>(std::declval<From>()); };

	template<typename Derived, typename Base>
	concept derived_from = std::is_base_of_v<Base, Derived> && std::is_convertible_v<const volatile Derived*, const volatile Base*>;

	namespace Editor::DataStorage
	{
		using FColumn = FEditorDataStorageColumn;
		using FTag = FEditorDataStorageTag;

		/**
		 * Defines a dynamic type for a value tag
		 * Example:
		 *   FValueTag ColorTagType(TEXT("Color"));
		 *   FValueTag DirectionTagType(TEXT("Direction"));
		 * A value tag can take on different values for each type.  This is set up when a tag is added to a row.
		 */
		class FValueTag
		{
		public:
			TYPEDELEMENTFRAMEWORK_API explicit FValueTag(const FName& InName);
			
			TYPEDELEMENTFRAMEWORK_API const FName& GetName() const;
			TYPEDELEMENTFRAMEWORK_API bool operator==(const FValueTag& Other) const = default;
		private:
			TYPEDELEMENTFRAMEWORK_API friend uint32 GetTypeHash(const FValueTag& InName);
			FName Name;
		};

		template<typename T>
		concept TValueTagType = std::is_same_v<T, FValueTag>;

		struct FDynamicColumnDescription
		{
			const UScriptStruct* TemplateType;
			FName Identifier;

			TYPEDELEMENTFRAMEWORK_API friend uint32 GetTypeHash(const FDynamicColumnDescription& Descriptor);
			TYPEDELEMENTFRAMEWORK_API bool operator==(const FDynamicColumnDescription&) const = default;
		};
		// Standard callbacks.

		using RowCreationCallbackRef = TFunctionRef<void(RowHandle Row)>;
		using ColumnCreationCallbackRef = TFunctionRef<void(void* Column, const UScriptStruct& ColumnType)>;
		using ColumnListCallbackRef = TFunctionRef<void(const UScriptStruct& ColumnType)>;
		using ColumnListWithDataCallbackRef = TFunctionRef<void(void* Column, const UScriptStruct& ColumnType)>;
		using ColumnCopyOrMoveCallback = void (*)(const UScriptStruct& ColumnType, void* Destination, void* Source);



		// Template concepts to enforce type correctness.
		template<typename T>
		concept TDataColumnType = UE::derived_from<T, FColumn>;

		template<typename T>
		concept TTagColumnType = UE::derived_from<T, FTag>;

		template<typename T>
		concept TColumnType = TDataColumnType<T> || TTagColumnType<T>;

		template<typename T>
		concept TEnumType = std::is_enum_v<T>;
	} // namespace Editor::DataStorage
} // namespace UE
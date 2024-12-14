// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "HAL/PreprocessorHelpers.h"
#include "Serialization/CompactBinary.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"
#include "UObject/NameTypes.h"

class FCbFieldView;
class FCbWriter;
class UObject;
namespace UE::ConfigAccessTracking { enum class ELoadType : uint8; }
namespace UE::ConfigAccessTracking { struct FConfigAccessData; }
struct FARFilter;
#endif

#if WITH_EDITOR

namespace UE::Cook
{

/** Context passed into FCookDependencyFunction to provide calling flags and receive their hash output. */
struct FCookDependencyContext
{
public:
	/** InHasher is void* to mask the implementation details of the hashbuilder. See Update function. */
	explicit FCookDependencyContext(void* InHasher, TUniqueFunction<void(FString&&)>&& InOnLogError, FName InPackageName);

	/**
	 * Update the hashbuilder for the key being constructed (e.g. TargetDomainKey for cooked packages)
	 * with the given Data of Size bytes.
	 */
	COREUOBJECT_API void Update(const void* Data, uint64 Size);

	/**
	 * Reports failure to compute the hash (e.g. because a file cannot be read).
	 * When calculating the initial hash during package save, this error will be logged as an error
	 * and the package will be recooked on the next cook. When calculating the hash during an incremental cook
	 * the message will be logged at Log level and will cause the package to be recooked.
	 */
	COREUOBJECT_API void LogError(FString Message);

	/**
	 * Private implementation struct used for AddErrorHandlerScope. Should only be used via
	 * FCookDependencyContext::FErrorHandlerScope& Scope = Context.ErrorHandlerScope(<Function>);
	 */
	struct FErrorHandlerScope
	{
	public:
		COREUOBJECT_API ~FErrorHandlerScope();
	private:
		COREUOBJECT_API FErrorHandlerScope(FCookDependencyContext& InContext);
		friend FCookDependencyContext;
		FCookDependencyContext& Context;
	};
	/**
	 * Add a function that will be removed when the return value goes out of scope, to modify error strings reported
	 * inside the scope before passing them on to higher scopes or the error consumer.
	 * e.g. 
	 * FCookDependencyContext::FErrorHandlerScope Scope = Context.ErrorHandlerScope([](FString&& Inner)
	 * { return FString::Printf(TEXT("OuterClass for %s: %s"), *Name, *Inner);});
	 */
	[[nodiscard]] COREUOBJECT_API FErrorHandlerScope ErrorHandlerScope(
		TUniqueFunction<FString(FString&&)>&& ErrorHandler);

	/**
	 * Get the name of the package being considered
	 */
	COREUOBJECT_API FName GetPackageName() const { return PackageName; }
private:
	TUniqueFunction<void(FString&&)> OnLogError;
	TArray<TUniqueFunction<FString(FString&&)>, TInlineAllocator<1>> ErrorHandlers;
	FName PackageName;
	void* Hasher; // Type is void* to mask the implementation detail
};

/**
 * TypeSelector enum for the FCookDependency variable type. Values are serialized into the oplog as integers,
 * so do not change them without changing oplog version.
 */
enum class ECookDependency : uint8
{
	None					= 0,
	File					= 1,
	Function				= 2,
	TransitiveBuild			= 3,
	Package					= 4,
	ConsoleVariable			= 5,
	Config					= 6,
	SettingsObject			= 7,
	NativeClass				= 8,
	AssetRegistryQuery		= 9,

	Count,
};

/**
 * TargetDomain dependencies that can be reported from the class instances in a package. These dependencies are
 * stored in the cook oplog and are evaluated during incremental cook. If any of them changes, the package is
 * invalidated and must be recooked (loaded/saved). These dependencies do not impact whether DDC keys built
 * from the package need to be recalculated.
 */
class FCookDependency
{
public:
	/**
	 * Create a dependency on the contents of the file. Filename will be normalized. Contents are loaded via
	 * IFileManager::Get().CreateFileReader and contents are hashed for comparison.
	 */
	COREUOBJECT_API static FCookDependency File(FStringView InFileName);
	/**
	 * Create a dependency on a call to the specified function with the given arguments. Arguments should be
	 * created using FCbWriter Writer; ... <append arbitrary number of fields to Writer> ...; Writer.Save().
	 * The function should read the arguments using the corresponding FCbFieldIteratorView methods and
	 * LoadFromCompactBinary calls.
	 * 
	 * The function must be registered during editor startup for use with FCookDependency via
	 * UE_COOK_DEPENDENCY_FUNCTION(CppTokenUsedAsName, CppNameOfFunctionToCall).
	 * The name to pass to FCookDependency::Function can be retrieved via
	 * UE_COOK_DEPENDENCY_FUNCTION_CALL(CppTokenUsedAsName).
	 */
	COREUOBJECT_API static FCookDependency Function(FName InFunctionName, FCbFieldIterator&& InArgs);

	/**
	 * Create a transitive build dependency on another package. In an incremental cook if the other package was not
	 * cooked in a previous cook session, or its previous cook result was invalidated, the current package will also
	 * have its cook result invalidated.
	 *
	 * This version of the function also adds a runtime dependency - the requested package will be staged for the
	 * current platform. Adding a transitive build dependency without adding a runtime dependency is not yet supported
	 * due to limitations in the cooker.
	 */
	COREUOBJECT_API static FCookDependency TransitiveBuildAndRuntime(FName PackageName);

	/**
	 * Create a build dependency on the contents of a package.
	 * Only the bytes of the .uasset/.umap file are considered.
	 */
	COREUOBJECT_API static FCookDependency Package(FName PackageName);

	/**
	 * Create a dependency on the value of a cvar. The cvar will be read and its value (as a string) will be hashed into the oplog data
	 * If the cvar value is changed, the packages that depend on it will be invalidated
	 */
	COREUOBJECT_API static FCookDependency ConsoleVariable(FStringView VariableName);

	/** Create a dependency on the value of a config variable. */
	COREUOBJECT_API static FCookDependency Config(UE::ConfigAccessTracking::FConfigAccessData AccessData);
	COREUOBJECT_API static FCookDependency Config(UE::ConfigAccessTracking::ELoadType LoadType, FName Platform,
		FName FileName, FName SectionName, FName ValueName);
	/** Create a dependency on the value of a config variable, with LoadType=ConfigSystem and Platform=NAME_None. */
	COREUOBJECT_API static FCookDependency Config(FName FileName, FName SectionName, FName ValueName);

	/**
	 * Adds a dependency on the config values and class schema of a settings object. Gives an error and ignores the object
	 * if the object is not a config-driven settings object, such as the CDO of a config UClass or a perObjectConfig
	 * object.
	 *
	 * SettingsObject dependencies are not directly persistable; all of the dependencies reported by the SettingsObject are
	 * copied onto the dependencies of the package declaring the SettingsObject dependency.
	 */
	COREUOBJECT_API static FCookDependency SettingsObject(const UObject* InObject);

	/** Adds a dependency on the class schema of a nativeclass. */
	COREUOBJECT_API static FCookDependency NativeClass(const UClass* InClass);
	COREUOBJECT_API static FCookDependency NativeClass(FStringView ClassPath);

	/** Adds a dependency on the results reported by an AssetRegistry query. */
	COREUOBJECT_API static FCookDependency AssetRegistryQuery(FARFilter Filter);

	/** Construct an empty dependency; it will never be invalidated. */
	COREUOBJECT_API FCookDependency();

	COREUOBJECT_API ~FCookDependency();
	COREUOBJECT_API FCookDependency(const FCookDependency& Other);
	COREUOBJECT_API FCookDependency(FCookDependency&& Other);
	COREUOBJECT_API FCookDependency& operator=(const FCookDependency& Other);
	COREUOBJECT_API FCookDependency& operator=(FCookDependency&& Other);
	
	/** FCookDependency is a vararg type. Return the type of this instance. */
	ECookDependency GetType() const;

	/** FileName if GetType() == File, else empty. StringView points to null or a null-terminated string. */
	FStringView GetFileName() const;

	/** FunctionName if GetType() == Function, else NAME_None. */
	FName GetFunctionName() const;
	/** FunctionArgs if GetType() == Function, else FCbFieldViewIterator(). */
	FCbFieldViewIterator GetFunctionArgs() const;

	/** PackageName if GetType() == TransitiveBuild or GetType() == Package, else NAME_None. */
	FName GetPackageName() const;
	/** If GetType() == TransitiveBuild, whether AlsoAddRuntimeDependency was selected, otherwise false. */
	bool IsAlsoAddRuntimeDependency() const;

	/**
	 * Returns the full path of the config access (e.g. Platform.Filename.Section.ValueName)
	 * if GetType() == Config, else empty.
	 */
	COREUOBJECT_API FString GetConfigPath() const;

	/**
	 * Returns the SettingsObject pointer if GetType() == SettingsObject, else nullptr. Can also be null for
	 * SettingsObject that was found to be invalid.
	 */
	const UObject* GetSettingsObject() const;

	/**
	 * Returns the classpath if GetType() == NativeClass, else empty string. Can also be empty for
	 * NativeClass that was found to be invalid.
	 */
	FStringView GetClassPath() const;

	/** Returns the FARFilter if GetType() == AssetRegistryQuery, else nullptr. */
	const FARFilter* GetARFilter() const;

	/**
	 * Comparison operator for e.g. deterministic ordering of dependencies.
	 * Uses persistent comparison data and is somewhat expensive.
	 */
	bool operator<(const FCookDependency& Other) const;
	/** Equality operator for uniqueness testing */
	bool operator==(const FCookDependency& Other) const;
	bool operator!=(const FCookDependency& Other) const;

	/** Calculate the current hash of this CookDependency, and add it into Context. */
	COREUOBJECT_API void UpdateHash(FCookDependencyContext& Context) const;

private:
	explicit FCookDependency(ECookDependency InType);
	void Construct();
	void Destruct();
	COREUOBJECT_API void Save(FCbWriter& Writer) const;
	COREUOBJECT_API bool Load(FCbFieldView Value);
	static COREUOBJECT_API bool ConfigAccessDataLessThan(const UE::ConfigAccessTracking::FConfigAccessData& A,
		const UE::ConfigAccessTracking::FConfigAccessData& B);
	static COREUOBJECT_API bool ConfigAccessDataEqual(const UE::ConfigAccessTracking::FConfigAccessData& A,
		const UE::ConfigAccessTracking::FConfigAccessData& B);
	static COREUOBJECT_API bool ARFilterLessThan(const FARFilter& A, const FARFilter& B);
	static COREUOBJECT_API bool ARFilterEqual(const FARFilter& A, const FARFilter& B);

	/** Public hidden friend for operator<< into an FCbWriter. */
	friend FCbWriter& operator<<(FCbWriter& Writer, const FCookDependency& CookDependencies)
	{
		CookDependencies.Save(Writer);
		return Writer;
	}
	/** Public hidden friend for LoadFromCompactBinary. */
	friend bool LoadFromCompactBinary(FCbFieldView Value, FCookDependency& CookDependencies)
	{
		return CookDependencies.Load(Value);
	}

private:
	ECookDependency Type;
	struct FFunctionData
	{
		FName Name;
		FCbFieldIterator Args;
	};
	struct FTransitiveBuildData
	{
		FName PackageName;
		bool bAlsoAddRuntimeDependency = true;
	};
	union
	{
		FString StringData;
		FFunctionData FunctionData;
		FTransitiveBuildData TransitiveBuildData;
		FName NameData;
		const UObject* ObjectPtr;
		TUniquePtr<UE::ConfigAccessTracking::FConfigAccessData> ConfigAccessData;
		TUniquePtr<FARFilter> ARFilter;
	};
};

/**
 * Type of functions used in FCookDependency to append the hash values of arbitrary data.
 * 
 * @param Args Variable-length, variable-typed input data (e.g. names of files, configuration flags)
 *             that specify which hash data. The function should read this data using FCbFieldViewIterator
 *             methods and LoadFromCompactBinary calls that correspond to the FCbWriter methods used
 *             at the callsite of FCookDependency::Function.
 * @param Context that provides calling flags and receives the hashdata. The function should call
 *        Context.Update with the data to be added to the target key (e.g. the hash of the contents
 *        of a filename that was specified in Args).
 */
using FCookDependencyFunction = void (*)(FCbFieldViewIterator Args, FCookDependencyContext& Context);

} // namespace UE::Cook

namespace UE::Cook::Dependency::Private
{

/**
 * Implementation struct of UE_COOK_DEPENDENCY_FUNCTION. Instances of this class are stored in global or
 * namespace scope and add themselves to a list during c++ pre-main static initialization. This list
 * is read later to create a map from FName to c++ function.
 */
struct FCookDependencyFunctionRegistration
{
	template<int N>
	FCookDependencyFunctionRegistration(const TCHAR(&InName)[N], FCookDependencyFunction InFunction)
		: Name(InName), Function(InFunction), Next(nullptr)
	{
		static_assert(N > 0, "Name must be provided");
		check(InName[0] != '\0');
		Construct();
	}
	COREUOBJECT_API ~FCookDependencyFunctionRegistration();
	COREUOBJECT_API void Construct();
	FName GetFName();

	FLazyName Name;
	FCookDependencyFunction Function;
	FCookDependencyFunctionRegistration* Next;
};

} // namespace UE::Cook::Dependency::Private

/**
 * UE_COOK_DEPENDENCY_FUNCTION(<CppToken> Name, void (*)(FCbFieldView Args, FCookDependencyContext& Context))
 * 
 * Registers the given function pointer to handle FCookDependency::Function(Name, Args) calls.
 * @see FCookDependencyFunction. @see FCookDependency::Function.
 * 
 * Name should be a bare cpptoken, e.g.
 * UE_COOK_DEPENDENCY_FUNCTION(MyTypeDependencies, UE::MyTypeDependencies::ImplementationFunction).
*/
#define UE_COOK_DEPENDENCY_FUNCTION(Name, Function) \
	UE::Cook::Dependency::Private::FCookDependencyFunctionRegistration \
	PREPROCESSOR_JOIN(FCookDependencyFunctionRegistration_,Name)(TEXT(#Name), Function)
/**
 * Return the FName to use to call a function that was registered via UE_COOK_DEPENDENCY_FUNCTION(Name, Function).
 * Name should be the same bare cpptoken that was passed into UE_COOK_DEPENDENCY_FUNCTION.
 */
#define UE_COOK_DEPENDENCY_FUNCTION_CALL(Name) \
	PREPROCESSOR_JOIN(FCookDependencyFunctionRegistration_,Name).GetFName()

#else // WITH_EDITOR

#define UE_COOK_DEPENDENCY_FUNCTION(Name, Function)
#define UE_COOK_DEPENDENCY_FUNCTION_CALL(Name) NAME_None

#endif // !WITH_EDITOR

#if WITH_EDITOR
namespace UE::Cook
{

inline ECookDependency FCookDependency::GetType() const
{
	return Type;
}

inline FStringView FCookDependency::GetFileName() const
{
	return Type == ECookDependency::File ? StringData : FStringView();
}

inline FName FCookDependency::GetFunctionName() const
{
	return Type == ECookDependency::Function ? FunctionData.Name : NAME_None;
}

inline FCbFieldViewIterator FCookDependency::GetFunctionArgs() const
{
	return Type == ECookDependency::Function ? FunctionData.Args : FCbFieldViewIterator();
}

inline FName FCookDependency::GetPackageName() const
{
	switch (GetType())
	{
	case ECookDependency::TransitiveBuild:
		return TransitiveBuildData.PackageName;
	case ECookDependency::Package:
		return NameData;
	default:
		return NAME_None;
	};
}

inline bool FCookDependency::IsAlsoAddRuntimeDependency() const
{
	return Type == ECookDependency::TransitiveBuild ? TransitiveBuildData.bAlsoAddRuntimeDependency : false;
}

inline const UObject* FCookDependency::GetSettingsObject() const
{
	return Type == ECookDependency::SettingsObject ? ObjectPtr : nullptr;
}

inline FStringView FCookDependency::GetClassPath() const
{
	return Type == ECookDependency::NativeClass ? StringData : FStringView();
}

inline const FARFilter* FCookDependency::GetARFilter() const
{
	return Type == ECookDependency::AssetRegistryQuery ? ARFilter.Get() : nullptr;
}

inline bool FCookDependency::operator<(const FCookDependency& Other) const
{
	if (static_cast<uint8>(Type) != static_cast<uint8>(Other.Type))
	{
		return static_cast<uint8>(Type) < static_cast<uint8>(Other.Type);
	}

	switch (Type)
	{
	case ECookDependency::None:
		return false;
	case ECookDependency::File:
	case ECookDependency::ConsoleVariable:
	case ECookDependency::NativeClass:
		return StringData.Compare(Other.StringData, ESearchCase::IgnoreCase) < 0;
	case ECookDependency::Function:
	{
		int32 Compare = FunctionData.Name.Compare(Other.FunctionData.Name);
		if (Compare != 0)
		{
			return Compare < 0;
		}
		FMemoryView ViewA;
		FMemoryView ViewB;
		bool bHasViewA = FunctionData.Args.TryGetRangeView(ViewA);
		bool bHasViewB = Other.FunctionData.Args.TryGetRangeView(ViewB);
		if ((!bHasViewA) | (!bHasViewB))
		{
			return bHasViewB; // If both false, return false. If only one, return true only if A is the false.
		}
		return ViewA.CompareBytes(ViewB) < 0;
	}
	case ECookDependency::TransitiveBuild:
	{
		// FName.Compare is lexical and case-insensitive, which is what we want
		int32 Compare = TransitiveBuildData.PackageName.Compare(Other.TransitiveBuildData.PackageName);
		if (Compare != 0)
		{
			return Compare < 0;
		}
		if (TransitiveBuildData.bAlsoAddRuntimeDependency != Other.TransitiveBuildData.bAlsoAddRuntimeDependency)
		{
			return TransitiveBuildData.bAlsoAddRuntimeDependency == false;
		}
		return false;
	}
	case ECookDependency::Package:
		return NameData.Compare(Other.NameData) < 0;
	case ECookDependency::Config:
		if (ConfigAccessData.IsValid() != Other.ConfigAccessData.IsValid())
		{
			return !ConfigAccessData.IsValid();
		}
		if (!ConfigAccessData.IsValid())
		{
			return false; // equal
		}
		return ConfigAccessDataLessThan(*ConfigAccessData, *Other.ConfigAccessData);
	case ECookDependency::SettingsObject:
		// SettingsObjects are not persistable, so we do not use a persistent sort key; just the object ptr.
		return ObjectPtr < Other.ObjectPtr;
	case ECookDependency::AssetRegistryQuery:
		if (ARFilter.IsValid() != Other.ARFilter.IsValid())
		{
			return !ARFilter.IsValid();
		}
		if (!ARFilter.IsValid())
		{
			return false; // equal
		}
		return ARFilterLessThan(*ARFilter, *Other.ARFilter);
	default:
		checkNoEntry();
		return false;
	}
}

inline bool FCookDependency::operator==(const FCookDependency& Other) const
{
	if (static_cast<uint8>(Type) != static_cast<uint8>(Other.Type))
	{
		return false;
	}

	switch (Type)
	{
	case ECookDependency::None:
		return true;
	case ECookDependency::File:
	case ECookDependency::ConsoleVariable:
	case ECookDependency::NativeClass:
		return StringData.Compare(Other.StringData, ESearchCase::IgnoreCase) == 0;
	case ECookDependency::Function:
	{
		if (FunctionData.Name.Compare(Other.FunctionData.Name) != 0)
		{
			return false;
		}
		FMemoryView ViewA;
		FMemoryView ViewB;
		bool bHasViewA = FunctionData.Args.TryGetRangeView(ViewA);
		bool bHasViewB = Other.FunctionData.Args.TryGetRangeView(ViewB);
		if (bHasViewA != bHasViewB)
		{
			return false;
		}
		return ViewA.CompareBytes(ViewB) == 0;
	}
	case ECookDependency::TransitiveBuild:
	{
		// FName.Compare is lexical and case-insensitive, which is what we want
		int32 Compare = TransitiveBuildData.PackageName.Compare(Other.TransitiveBuildData.PackageName);
		if (Compare != 0)
		{
			return false;
		}
		return TransitiveBuildData.bAlsoAddRuntimeDependency == Other.TransitiveBuildData.bAlsoAddRuntimeDependency;
	}
	case ECookDependency::Package:
		return NameData.Compare(Other.NameData) == 0;
	case ECookDependency::Config:
		if (ConfigAccessData.IsValid() != Other.ConfigAccessData.IsValid())
		{
			return false;
		}
		if (!ConfigAccessData.IsValid())
		{
			return true;
		}
		return ConfigAccessDataEqual(*ConfigAccessData, *Other.ConfigAccessData);
	case ECookDependency::SettingsObject:
		// SettingsObjects are not persistable, so we do not use a persistent sort key; just the object ptr.
		return ObjectPtr == Other.ObjectPtr;
	case ECookDependency::AssetRegistryQuery:
	{
		if (ARFilter.IsValid() != Other.ARFilter.IsValid())
		{
			return false;
		}
		if (!ARFilter.IsValid())
		{
			return true;
		}

		return ARFilterEqual(*ARFilter, *Other.ARFilter);
	}
	default:
		checkNoEntry();
		return false;
	}
}

inline bool FCookDependency::operator!=(const FCookDependency& Other) const
{
	return !(*this == Other);
}

inline FCookDependencyContext::FCookDependencyContext(void* InHasher, TUniqueFunction<void(FString&&)>&& InOnLogError, FName InPackageName)
	: OnLogError(MoveTemp(InOnLogError))
	, PackageName(InPackageName)
	, Hasher(InHasher)
{
}

} // namespace UE::Cook

namespace UE::Cook::Dependency::Private
{

inline FName FCookDependencyFunctionRegistration::GetFName()
{
	return Name.Resolve();
}

} // namespace UE::Cook::Dependency::Private

#endif
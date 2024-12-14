// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"

#include "Misc/PackageName.h"
#include "TestHarness.h"

TEST_CASE("Split full object path with subobjects", "[CoreUObject][FPackageName::SplitFullObjectPath]")
{
	const FStringView ExpectedClassPath = TEXT("/Script/SomePackage.SomeClass");
	const FStringView ExpectedPackagePath = TEXT("/Path/To/A/Package");
	const FStringView ExpectedObjectName = TEXT("Object");
	const FStringView ExpectedSubobject1Name = TEXT("Subobject1");
	const FStringView ExpectedSubobject2Name = TEXT("Subobject2");

	// Good cases
	const FStringView TestSingleSubobject = TEXT("/Script/SomePackage.SomeClass /Path/To/A/Package.Object:Subobject1");
	const FStringView TestTwoSubobjects = TEXT("/Script/SomePackage.SomeClass /Path/To/A/Package.Object:Subobject1.Subobject2");
	const FStringView TestNoSubobjects = TEXT("/Script/SomePackage.SomeClass /Path/To/A/Package.Object");
	const FStringView TestTwoSubobjectsAndNoClassPath = TEXT("/Path/To/A/Package.Object:Subobject1.Subobject2");
	const FStringView TestPackage = TEXT("/Script/SomePackage.SomeClass /Path/To/A/Package");

	// Suspicious cases
	const FStringView TestMissingSubobject = TEXT("/Script/SomePackage.SomeClass /Path/To/A/Package.Object:");
	const FStringView TestMissingSubobjectWithTrailingDot = TEXT("/Script/SomePackage.SomeClass /Path/To/A/Package.Object:.");
	const FStringView TestValidSubobjectWithTrailingDot = TEXT("/Script/SomePackage.SomeClass /Path/To/A/Package.Object:Subobject1.");
	const FStringView TestTwoValidSubobjectsWithTrailingDot = TEXT("/Script/SomePackage.SomeClass /Path/To/A/Package.Object:Subobject1.Subobject2.");

	FStringView ClassPath;
	FStringView PackagePath;
	FStringView ObjectName; 
	TArray<FStringView> SubobjectNames;

	SECTION("Single subobject verification")
	{
		FPackageName::SplitFullObjectPath(TestSingleSubobject, ClassPath, PackagePath, ObjectName, SubobjectNames);

		REQUIRE(ClassPath == ExpectedClassPath);
		REQUIRE(PackagePath == ExpectedPackagePath);
		REQUIRE(ObjectName == ExpectedObjectName);
		REQUIRE(SubobjectNames.Num() == 1);
		REQUIRE(SubobjectNames[0] == ExpectedSubobject1Name);
	}

	SECTION("Two subobjects verification")
	{
		FPackageName::SplitFullObjectPath(TestTwoSubobjects, ClassPath, PackagePath, ObjectName, SubobjectNames);

		REQUIRE(ClassPath == ExpectedClassPath);
		REQUIRE(PackagePath == ExpectedPackagePath);
		REQUIRE(ObjectName == ExpectedObjectName);
		REQUIRE(SubobjectNames.Num() == 2);
		REQUIRE(SubobjectNames[0] == ExpectedSubobject1Name);
		REQUIRE(SubobjectNames[1] == ExpectedSubobject2Name);
	}

	SECTION("No subobjects verification")
	{
		FPackageName::SplitFullObjectPath(TestNoSubobjects, ClassPath, PackagePath, ObjectName, SubobjectNames);

		REQUIRE(ClassPath == ExpectedClassPath);
		REQUIRE(PackagePath == ExpectedPackagePath);
		REQUIRE(ObjectName == ExpectedObjectName);
		REQUIRE(SubobjectNames.Num() == 0);
	}

	SECTION("No class path verification (bDetectClassName on)")
	{
		FPackageName::SplitFullObjectPath(TestTwoSubobjectsAndNoClassPath, ClassPath, PackagePath, ObjectName, SubobjectNames);

		REQUIRE(ClassPath.Len() == 0);
		REQUIRE(PackagePath == ExpectedPackagePath);
		REQUIRE(ObjectName == ExpectedObjectName);
		REQUIRE(SubobjectNames.Num() == 2);
		REQUIRE(SubobjectNames[0] == ExpectedSubobject1Name);
		REQUIRE(SubobjectNames[1] == ExpectedSubobject2Name);
	}

	SECTION("No class path verification (bDetectClassName off)")
	{
		FPackageName::SplitFullObjectPath(TestTwoSubobjectsAndNoClassPath, ClassPath, PackagePath, ObjectName, SubobjectNames, false);

		REQUIRE(ClassPath.Len() == 0);
		REQUIRE(PackagePath == ExpectedPackagePath);
		REQUIRE(ObjectName == ExpectedObjectName);
		REQUIRE(SubobjectNames.Num() == 2);
		REQUIRE(SubobjectNames[0] == ExpectedSubobject1Name);
		REQUIRE(SubobjectNames[1] == ExpectedSubobject2Name);
	}

	SECTION("Package verification")
	{
		FPackageName::SplitFullObjectPath(TestPackage, ClassPath, PackagePath, ObjectName, SubobjectNames);

		REQUIRE(ClassPath == ExpectedClassPath);
		REQUIRE(PackagePath == ExpectedPackagePath);
		REQUIRE(ObjectName == TEXT(""));
		REQUIRE(SubobjectNames.Num() == 0);
	}

	SECTION("Missing subobject name yields empty subobjects array")
	{
		FPackageName::SplitFullObjectPath(TestMissingSubobject, ClassPath, PackagePath, ObjectName, SubobjectNames);

		REQUIRE(ClassPath == ExpectedClassPath);
		REQUIRE(PackagePath == ExpectedPackagePath);
		REQUIRE(ObjectName == ExpectedObjectName);
		REQUIRE(SubobjectNames.Num() == 0);
	}

	SECTION("Missing subobject name with trailing dot yields empty subobjects array")
	{
		FPackageName::SplitFullObjectPath(TestMissingSubobjectWithTrailingDot, ClassPath, PackagePath, ObjectName, SubobjectNames);

		REQUIRE(ClassPath == ExpectedClassPath);
		REQUIRE(PackagePath == ExpectedPackagePath);
		REQUIRE(ObjectName == ExpectedObjectName);
		REQUIRE(SubobjectNames.Num() == 0);
	}

	SECTION("Valid subobject with trailing dot still reports correct subobject name")
	{
		FPackageName::SplitFullObjectPath(TestValidSubobjectWithTrailingDot, ClassPath, PackagePath, ObjectName, SubobjectNames);

		REQUIRE(ClassPath == ExpectedClassPath);
		REQUIRE(PackagePath == ExpectedPackagePath);
		REQUIRE(ObjectName == ExpectedObjectName);
		REQUIRE(SubobjectNames.Num() == 1);
		REQUIRE(SubobjectNames[0] == ExpectedSubobject1Name);
	}

	SECTION("Two valid subobjects with trailing dot still reports correct subobject names")
	{
		FPackageName::SplitFullObjectPath(TestTwoValidSubobjectsWithTrailingDot, ClassPath, PackagePath, ObjectName, SubobjectNames);

		REQUIRE(ClassPath == ExpectedClassPath);
		REQUIRE(PackagePath == ExpectedPackagePath);
		REQUIRE(ObjectName == ExpectedObjectName);
		REQUIRE(SubobjectNames.Num() == 2);
		REQUIRE(SubobjectNames[0] == ExpectedSubobject1Name);
		REQUIRE(SubobjectNames[1] == ExpectedSubobject2Name);
	}
}
// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_TESTS
#include "Tests/TestHarnessAdapter.h"
#include "Containers/AnsiString.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Containers/StringView.h"
#include "PlainPropsRead.h"
#include "PlainPropsWrite.h"
#include "PlainPropsBuildSchema.h"
#include "PlainPropsIndex.h"
#include "PlainPropsInternalBuild.h"
#include "PlainPropsInternalFormat.h"
#include "Templates/UnrealTemplate.h"

namespace PlainProps
{
	
static bool operator==(FScopeId A, FNestedScopeId B) { return A == FScopeId(B); }
static bool operator==(FScopeId A, FFlatScopeId B) { return A == FScopeId(B); }
static bool operator==(FParametricTypeView A, FParametricTypeView B)
{
	return A.Name == B.Name && A.NumParameters == B.NumParameters && !FMemory::Memcmp(A.Parameters, B.Parameters, A.NumParameters * sizeof(FTypeId));
}
	
TEST_CASE_NAMED(FPlainPropsIndexTest, "System::Core::Serialization::PlainProps::Index", "[Core][PlainProps][SmokeFilter]")
{
	SECTION("NestedScope")
	{
		FScopeId S0{FFlatScopeId{{0}}};
		FScopeId S1{FFlatScopeId{{1}}};
		FScopeId S2{FFlatScopeId{{2}}};

		FNestedScope N01{S0, S1.AsFlat()};
		FNestedScope N10{S1, S0.AsFlat()};
		FNestedScope N12{S1, S2.AsFlat()};

		FNestedScopeIndexer Indexer;

		FScopeId S01(Indexer.Index(N01));
		FScopeId S10(Indexer.Index(N10));
		FScopeId S12(Indexer.Index(N12));

		FNestedScope N012{S01, S2.AsFlat()};
		FScopeId S012(Indexer.Index(N012));

		FNestedScope N0120{S012, S0.AsFlat()};
		FScopeId S0120(Indexer.Index(N0120));
		
		CHECK(S01	== Indexer.Index(N01));
		CHECK(S10	== Indexer.Index(N10));
		CHECK(S12	== Indexer.Index(N12));
		CHECK(S012	== Indexer.Index(N012));
		CHECK(S0120 == Indexer.Index(N0120));
		CHECK(N01	== Indexer.Resolve(S01.AsNested()));
		CHECK(N10	== Indexer.Resolve(S10.AsNested()));
		CHECK(N12	== Indexer.Resolve(S12.AsNested()));
		CHECK(N012	== Indexer.Resolve(S012.AsNested()));
		CHECK(N0120 == Indexer.Resolve(S0120.AsNested()));
		CHECK(Indexer.Num() == 5);
	}
	
	SECTION("ParametricType")
	{
		FScopeId S0{FFlatScopeId{{0}}};
		FScopeId S1{FFlatScopeId{{1}}};
		FScopeId S2{FFlatScopeId{{2}}};
		
		FConcreteTypenameId T3{{3}};
		FConcreteTypenameId T4{{4}};
		FConcreteTypenameId T5{{5}};
		
		FTypeId S0T3 = {S0, FTypenameId{T3}};
		FTypeId S1T3 = {S1, FTypenameId{T3}};
		FTypeId S2T3 = {S2, FTypenameId{T3}};

		FParametricTypeIndexer Indexer;

		FParametricTypeId T4_S0T3 = Indexer.Index({T4, 1, &S0T3});
		FParametricTypeId T4_S1T3 = Indexer.Index({T4, 1, &S1T3});
		
		CHECK(Indexer.Resolve(T4_S0T3) == FParametricTypeView{T4, 1, &S0T3});
		CHECK(Indexer.Resolve(T4_S1T3) == FParametricTypeView{T4, 1, &S1T3});
		
		FTypeId S1T4_S0T3 = {S1,  FTypenameId{T4_S0T3}};
		FTypeId S2T4_S1T3 = {S2,  FTypenameId{T4_S1T3}};
		
		CHECK(S1T4_S0T3.Name.AsParametric() == T4_S0T3);
		CHECK(S2T4_S1T3.Name.AsParametric() == T4_S1T3);

		FParametricTypeId T5_S0T3_S2T3 = Indexer.Index({T5, {S1T4_S0T3, S2T4_S1T3}});
		CHECK(Indexer.Resolve(T5_S0T3_S2T3) == FParametricTypeView{T5, {S1T4_S0T3, S2T4_S1T3}});
		
		CHECK(T4_S0T3		== Indexer.Index({T4, 1, &S0T3}));
		CHECK(T4_S1T3		== Indexer.Index({T4, 1, &S1T3}));
		CHECK(T5_S0T3_S2T3	== Indexer.Index({T5, {S1T4_S0T3, S2T4_S1T3}}));

		CHECK(Indexer.Num() == 3);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////

inline constexpr uint32 TestMagics[] = {0xFEEDF00D, 0xABCD1234, 0xDADADAAA, 0x99887766, 0xF0F1F2F3 };

class FTestBatchBuilder : public TIdIndexer<FAnsiString>, public IStructBindIds
{
public:
	FTestBatchBuilder(FScratchAllocator& InScratch) : Declarations(*this), Scratch(InScratch) {}

	FEnumSchemaId				DeclareEnum(FTypeId Type, EEnumMode Mode, ELeafWidth Width, std::initializer_list<const char*> Names, std::initializer_list<uint64> Constants);
	FEnumSchemaId				DeclareEnum(const char* Scope, const char* Name, EEnumMode Mode, ELeafWidth Width, std::initializer_list<const char*> Names, std::initializer_list<uint64> Constants);
	FStructSchemaId				DeclareStruct(FTypeId Type, std::initializer_list<const char*> MemberOrder, EMemberPresence Occupancy, FOptionalStructSchemaId Super = {});
	FStructSchemaId				DeclareStruct(const char* Scope, const char* Name, std::initializer_list<const char*> MemberOrder, EMemberPresence Occupancy, FOptionalStructSchemaId Super = {});
	
	const FEnumDeclaration&		Get(FEnumSchemaId Id) { return Declarations.Get(Id); }
	const FStructDeclaration&	Get(FStructSchemaId Id) { return Declarations.Get(Id); }

	void						AddObject(FStructSchemaId Schema, FMemberBuilder&& Members);
	void						AddObjects(FStructSchemaId Schema, std::initializer_list<FMemberBuilder&&> Objects);

	TArray64<uint8>				Write();

private:
	TArray<TPair<FStructSchemaId, FBuiltStruct*>>	Objects;
	FDeclarations									Declarations;
	FScratchAllocator&								Scratch;

	TArray<FMemberId> NameMembers(std::initializer_list<const char*> Members)
	{
		TArray<FMemberId> Out;
		Out.Reserve(Members.size());
		for (const char* Member : Members)
		{
			Out.Add(NameMember(Member));
		}
		return Out;
	}
	
	TArray<FEnumerator> MakeEnumerators(TConstArrayView<const char*> InNames, TConstArrayView<uint64> Constants)
	{
		check(InNames.Num() == Constants.Num());
		TArray<FEnumerator> Out;
		Out.SetNumUninitialized(InNames.Num());
		for (uint32 Idx = 0, Num = Out.Num(); Idx < Num; ++Idx)
		{
			Out[Idx] = { MakeName(InNames[Idx]), Constants[Idx] };
		}
		return Out;
	}

	TArray<char> GetNameData() const;

	virtual FStructSchemaId GetDeclId(FStructSchemaId BindId) const override final
	{
		checkf(false, TEXT("All struct ids should be declared, nothing is bound with different names in this test suite"));
		return BindId;
	}
};

FStructSchemaId FTestBatchBuilder::DeclareStruct(FTypeId Type, std::initializer_list<const char*> MemberOrder, EMemberPresence Occupancy, FOptionalStructSchemaId Super)
{
	FStructSchemaId Id = IndexStruct(Type);
	Declarations.DeclareStruct(Id, Type, NameMembers(MemberOrder), Occupancy, Super);
	return Id;
}

FStructSchemaId FTestBatchBuilder::DeclareStruct(const char* Scope, const char* Name, std::initializer_list<const char*> MemberOrder, EMemberPresence Occupancy, FOptionalStructSchemaId Super)
{
	return DeclareStruct(MakeType(Scope, Name), MemberOrder, Occupancy, Super);
}

FEnumSchemaId FTestBatchBuilder::DeclareEnum(FTypeId Type, EEnumMode Mode, ELeafWidth Width, std::initializer_list<const char*> InNames, std::initializer_list<uint64> Constants)
{
	FEnumSchemaId Id = IndexEnum(Type);
	Declarations.DeclareEnum(Id, Type, Mode, Width, MakeEnumerators(InNames, Constants));
	return Id;
}

FEnumSchemaId FTestBatchBuilder::DeclareEnum(const char* Scope, const char* Name, EEnumMode Mode, ELeafWidth Width, std::initializer_list<const char*> InNames, std::initializer_list<uint64> Constants)
{
	return DeclareEnum(MakeType(Scope, Name), Mode, Width, InNames, Constants);
}

void FTestBatchBuilder::AddObject(FStructSchemaId Schema, FMemberBuilder&& Members)
{
	Objects.Emplace(Schema, Members.BuildAndReset(Scratch, Declarations.Get(Schema), *this));
}

TArray64<uint8> FTestBatchBuilder::Write()
{
	// Build partial schemas
	FSchemasBuilder SchemaBuilders(Declarations, *this, Scratch);
	for (const TPair<FStructSchemaId, FBuiltStruct*>& Object : Objects)
	{
		SchemaBuilders.NoteStructAndMembers(Object.Key, *Object.Value);
	}
	FBuiltSchemas Schemas = SchemaBuilders.Build(); 

	// Filter out declared but unused names and ids
	FWriter Writer(*this, *this, Schemas, ESchemaFormat::StableNames);

	// Write names
	TArray64<uint8> Out;
	TArray64<uint8> Tmp;
	uint32 OldNameIdx = 0;
	for (const FAnsiString& Name : Names)
	{
		if (Writer.Uses({OldNameIdx++}))
		{
			WriteData(Tmp, *Name, Name.Len() + 1);
		}
	}
	WriteU32(Out, TestMagics[0]);
	WriteSkippableSlice(Out, Tmp);
	Tmp.Reset();

	// Write schemas
	WriteU32(Out, TestMagics[1]);
	Writer.WriteSchemas(/* Out */ Tmp);
	WriteAlignmentPadding<uint32>(Out);
	WriteU32(Out, IntCastChecked<uint32>(Tmp.Num()));
	WriteArray(Out, Tmp);
	Tmp.Reset();

	// Write objects
	WriteU32(Out, TestMagics[2]);
	for (const TPair<FStructSchemaId, FBuiltStruct*>& Object : Objects)
	{
		WriteU32(/* out */ Tmp, TestMagics[3]);
		WriteU32(/* out */ Tmp, Writer.GetWriteId(Object.Key).Get().Idx);
		Writer.WriteMembers(/* out */ Tmp, Object.Key, *Object.Value);
		WriteSkippableSlice(Out, Tmp);
		Tmp.Reset();
	}

	// Write object terminator
	WriteSkippableSlice(Out, TConstArrayView64<uint8>());
	WriteU32(Out, TestMagics[4]);
		
	return Out;
}

TArray<char> FTestBatchBuilder::GetNameData() const
{
	TArray<char> Out;
	Out.Reserve(Names.Num() * 100);
	for (const FAnsiString& Name : Names)
	{
		Out.Append(Name.GetCharArray());
		check(Out.Last() == '\0');
	}
	return Out;
}

////////////////////////////////////////////////////////////////////////////////////////////////

class FTestNameReader
{
public:
	void Read(FMemoryView Data)
	{
		check(Names.IsEmpty() && !Data.IsEmpty());
		TConstArrayView<char> AllChars(static_cast<const char*>(Data.GetData()), IntCastChecked<int32>(Data.GetSize()));
		
		const char* NextStr = AllChars.GetData();
		for (const char& Char : AllChars)
		{
			if (!Char)
			{
				Names.Add(NextStr);
				NextStr = &Char + 1;
			}
		}
		
		check(Names.Num() >= 3); // 1 FTypeId and 1 member id at least
		check(NextStr == Data.GetDataEnd()); // end with null-terminator
	}
	
	FAnsiStringView operator[](FNameId Id) const { return MakeStringView(Names[Id.Idx]); }
	FAnsiStringView operator[](FMemberId Name) const { return MakeStringView(Names[Name.Id.Idx]); }
	FAnsiStringView operator[](FOptionalMemberId Name) const { return Name ? operator[](Name.Get()) : "Super"; }
	FAnsiStringView operator[](FScopeId Scope) const { return operator[](Scope.AsFlat().Name); }
	FAnsiStringView operator[](FTypenameId Name) const { return operator[](Name.AsConcrete().Id); }

private:
	TArray<const char*> Names;
};

class FTestBatchReader
{
public:
	FTestBatchReader(FMemoryView Data)
	{
		// Read names
		FByteReader It(Data);
		CHECK(It.Grab<uint32>() == TestMagics[0]);
		Names.Read(It.GrabSkippableSlice());
		
		// Read schemas
		CHECK(It.Grab<uint32>() == TestMagics[1]);
		It.SkipAlignmentPadding<uint32>();
		uint32 SchemasSize = It.Grab<uint32>();
		const FSchemaBatch* Schemas = ValidateSchemas(It.GrabSlice(SchemasSize));
		FReadBatchId Batch = MountReadSchemas(Schemas);
		CHECK(It.Grab<uint32>() == TestMagics[2]);
		
		// Read objects
		while (uint64 NumBytes = It.GrabVarIntU())
		{	
			FByteReader ObjIt(It.GrabSlice(NumBytes));
			CHECK(ObjIt.Grab<uint32>() == TestMagics[3]);
			FStructSchemaId Schema = { ObjIt.Grab<uint32>() };
			Objects.Add({ { Schema, Batch }, ObjIt });
		}
		
		CHECK(It.Grab<uint32>() == TestMagics[4]);
		CHECK(!Objects.IsEmpty());
	}

	~FTestBatchReader()
	{
		UnmountReadSchemas(Objects[0].Schema.Batch);
	}

	TConstArrayView<FStructView>		GetObjects() const { return Objects;	}
	const FTestNameReader&				GetNames() const { return Names; }
	
private:
	FTestNameReader Names;
	TArray<FStructView> Objects;
};

static void TestSerialize(void (*BuildObjects)(FTestBatchBuilder&, FScratchAllocator&), void (*CheckObjects)(TConstArrayView<FStructView>, const FTestNameReader&))
{
	TArray64<uint8> Data;
	{
		FScratchAllocator Scratch;
		FTestBatchBuilder Batch(Scratch);
		BuildObjects(Batch, Scratch);
		Data = Batch.Write();
	}

	FTestBatchReader Batch(MakeMemoryView(Data));
	CheckObjects(Batch.GetObjects(), Batch.GetNames());
}

////////////////////////////////////////////////////////////////////////////////////////////////

// Tests that everything was read
struct FTestMemberReader : public FMemberReader
{
	using FMemberReader::FMemberReader;
	
	~FTestMemberReader()
	{
		CHECK(MemberIdx == NumMembers); // Must read all members
		CHECK(RangeTypeIdx == NumRangeTypes); // Must read all ranges
#if DO_CHECK
		CHECK(InnerSchemaIdx == NumInnerSchemas); // Must read all schema ids
#endif
	}
};

template<typename OutType, typename InType>
TArray<OutType> MakeArray(const InType& Items)
{
	TArray<OutType> Out;
	Out.Reserve(IntCastChecked<int32>(Items.Num()));
	for (const auto& Item : Items)
	{
		Out.Emplace(Item);
	}
	return Out;
}

////////////////////////////////////////////////////////////////////////////////////////////////

TEST_CASE_NAMED(FPlainPropsReadWriteTest, "System::Core::Serialization::PlainProps::ReadWrite", "[Core][PlainProps][SmokeFilter]")
{
	SECTION("Bool")
	{
		static constexpr std::initializer_list<const char*> MemberNames =  {"b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7", "b8", "b9", "b10", "b11"};

		TestSerialize([](FTestBatchBuilder& Batch, FScratchAllocator& Scratch)
		{
			FStructSchemaId SchemaId = Batch.DeclareStruct("Testing", "Bools", MemberNames, EMemberPresence::AllowSparse);

			FMemberBuilder B1T;
			B1T.Add(Batch.NameMember("b3"),	true);

			FMemberBuilder B1F;
			B1F.Add(Batch.NameMember("b1"),	false);

			FMemberBuilder B8M;
			B8M.Add(Batch.NameMember("b1"),	true);
			B8M.Add(Batch.NameMember("b2"),	false);
			B8M.Add(Batch.NameMember("b3"),	true);
			B8M.Add(Batch.NameMember("b4"),	false);
			B8M.Add(Batch.NameMember("b5"),	true);
			B8M.Add(Batch.NameMember("b6"),	false);
			B8M.Add(Batch.NameMember("b8"),	false);
			B8M.Add(Batch.NameMember("b9"), true);
			
			FMemberBuilder B9T;
			B9T.Add(Batch.NameMember("b1"),	true);
			B9T.Add(Batch.NameMember("b2"),	true);
			B9T.Add(Batch.NameMember("b3"),	true);
			B9T.Add(Batch.NameMember("b4"),	true);
			B9T.Add(Batch.NameMember("b5"),	true);
			B9T.Add(Batch.NameMember("b6"),	true);
			B9T.Add(Batch.NameMember("b8"),	true);
			B9T.Add(Batch.NameMember("b9"), true);
			B9T.Add(Batch.NameMember("b10"),true);

			Batch.AddObject(SchemaId, MoveTemp(B1T));
			Batch.AddObject(SchemaId, MoveTemp(B1F));
			Batch.AddObject(SchemaId, MoveTemp(B8M));
			Batch.AddObject(SchemaId, MoveTemp(B9T));
		}, 
		[](TConstArrayView<FStructView> Objects, const FTestNameReader& Names)
		{
			CHECK(Objects.Num() == 4);
			FTestMemberReader B1T(Objects[0]);
			FTestMemberReader B1F(Objects[1]);
			FTestMemberReader B8M(Objects[2]);
			FTestMemberReader B9T(Objects[3]);
			CHECK(Objects[0].Schema.Id == Objects[3].Schema.Id);

			// Check schema
			const FStructSchema& Schema =  Objects[0].Schema.Resolve();
			CHECK(Names[Schema.Type.Scope] == "Testing");
			CHECK(Names[Schema.Type.Name] == "Bools");
			CHECK(Schema.NumMembers == 9); // b0, b7 and b11 unused
			CHECK(Schema.NumRangeTypes == 0);
			CHECK(Schema.NumInnerSchemas == 0);
			CHECK(Schema.IsDense == 0);
			CHECK(Schema.Inheritance == ESuper::No);
			CHECK(FStructSchema::GetMemberTypes(Schema.Footer)[0] == FUnpackedLeafType(ELeafType::Bool, ELeafWidth::B8).Pack());
			CHECK(FStructSchema::GetMemberTypes(Schema.Footer)[8] == FUnpackedLeafType(ELeafType::Bool, ELeafWidth::B8).Pack());
			TConstArrayView<FMemberId> MemberIds = Schema.GetMemberNames();
			CHECK(Names[MemberIds[0]] == "b1");
			CHECK(Names[MemberIds[1]] == "b2");
			CHECK(Names[MemberIds[2]] == "b3");
			CHECK(Names[MemberIds[3]] == "b4");
			CHECK(Names[MemberIds[4]] == "b5");
			CHECK(Names[MemberIds[5]] == "b6");
			CHECK(Names[MemberIds[6]] == "b8");
			CHECK(Names[MemberIds[7]] == "b9");
			CHECK(Names[MemberIds[8]] == "b10");

			CHECK(Names[B1T.PeekName()] == "b3");
			CHECK(B1T.GrabLeaf().AsBool() == true);

			CHECK(Names[B1F.PeekName()] == "b1");
			CHECK(B1F.GrabLeaf().AsBool() == false);
			
			CHECK(B8M.GrabLeaf().AsBool() == true);
			CHECK(B8M.GrabLeaf().AsBool() == false);
			CHECK(B8M.GrabLeaf().AsBool() == true);
			CHECK(B8M.GrabLeaf().AsBool() == false);
			CHECK(B8M.GrabLeaf().AsBool() == true);
			CHECK(B8M.GrabLeaf().AsBool() == false);
			CHECK(B8M.GrabLeaf().AsBool() == false);
			CHECK(B8M.GrabLeaf().AsBool() == true);

			for (int32 Idx = 0; Idx < 9; ++Idx)
			{
				CHECK(B9T.GrabLeaf().AsBool() == true);
			}
		});
	}

	SECTION("Number")
	{
		static constexpr std::initializer_list<const char*> MemberNames =  {"F32", "F64", "S8", "U8", "S16", "U16", "S32", "U32", "S64", "U64"};

		TestSerialize([](FTestBatchBuilder& Batch, FScratchAllocator& Scratch)
		{
			FStructSchemaId SchemaId = Batch.DeclareStruct("Test", "Numbers", MemberNames, EMemberPresence::AllowSparse);

			FMemberBuilder Misc, Mins, Maxs, Some;

			Misc.Add(Batch.NameMember("F32"),	32.f);
			Misc.Add(Batch.NameMember("F64"),	64.0);
			Misc.Add(Batch.NameMember("S8"),	int8(-8));
			Misc.Add(Batch.NameMember("U8"),	uint8(8));
			Misc.Add(Batch.NameMember("S16"),	int16(-16));
			Misc.Add(Batch.NameMember("U16"),	uint16(16));
			Misc.Add(Batch.NameMember("S32"),	int32(-32));
			Misc.Add(Batch.NameMember("U32"),	uint32(32));
			Misc.Add(Batch.NameMember("S64"),	int64(-64));
			Misc.Add(Batch.NameMember("U64"),	uint64(64));
			
			Mins.Add(Batch.NameMember("F32"),	std::numeric_limits< float>::min());
			Mins.Add(Batch.NameMember("F64"),	std::numeric_limits<double>::min());
			Mins.Add(Batch.NameMember("S8"),	std::numeric_limits<  int8>::min());
			Mins.Add(Batch.NameMember("U8"),	std::numeric_limits< uint8>::min());
			Mins.Add(Batch.NameMember("S16"),	std::numeric_limits< int16>::min());
			Mins.Add(Batch.NameMember("U16"),	std::numeric_limits<uint16>::min());
			Mins.Add(Batch.NameMember("S32"),	std::numeric_limits< int32>::min());
			Mins.Add(Batch.NameMember("U32"),	std::numeric_limits<uint32>::min());
			Mins.Add(Batch.NameMember("S64"),	std::numeric_limits< int64>::min());
			Mins.Add(Batch.NameMember("U64"),	std::numeric_limits<uint64>::min());

			Maxs.Add(Batch.NameMember("F32"),	std::numeric_limits< float>::max());
			Maxs.Add(Batch.NameMember("F64"),	std::numeric_limits<double>::max());
			Maxs.Add(Batch.NameMember("S8"),	std::numeric_limits<  int8>::max());
			Maxs.Add(Batch.NameMember("U8"),	std::numeric_limits< uint8>::max());
			Maxs.Add(Batch.NameMember("S16"),	std::numeric_limits< int16>::max());
			Maxs.Add(Batch.NameMember("U16"),	std::numeric_limits<uint16>::max());
			Maxs.Add(Batch.NameMember("S32"),	std::numeric_limits< int32>::max());
			Maxs.Add(Batch.NameMember("U32"),	std::numeric_limits<uint32>::max());
			Maxs.Add(Batch.NameMember("S64"),	std::numeric_limits< int64>::max());
			Maxs.Add(Batch.NameMember("U64"),	std::numeric_limits<uint64>::max());

			Some.Add(Batch.NameMember("S32"),	0);
			
			Batch.AddObject(SchemaId, MoveTemp(Misc));
			Batch.AddObject(SchemaId, MoveTemp(Mins));
			Batch.AddObject(SchemaId, MoveTemp(Maxs));
			Batch.AddObject(SchemaId, MoveTemp(Some));
		}, 
		[](TConstArrayView<FStructView> Objects, const FTestNameReader& Names)
		{
			for (const FStructView& Object : Objects.Left(3))
			{
				FTestMemberReader Members(Object);
				for (const char* MemberName : MemberNames)
				{
					CHECK(Members.HasMore());
					CHECK(Names[Members.PeekName()] == MemberName);
					CHECK(Members.PeekKind() == EMemberKind::Leaf);
					(void)Members.GrabLeaf();
				}
			}
			
			FTestMemberReader Misc(Objects[0]);
			CHECK(Misc.GrabLeaf().AsFloat()		== 32.f);
			CHECK(Misc.GrabLeaf().AsDouble()	== 64.0);
			CHECK(Misc.GrabLeaf().AsS8()		== int8(-8));
			CHECK(Misc.GrabLeaf().AsU8()		== uint8(8));
			CHECK(Misc.GrabLeaf().AsS16()		== int16(-16));
			CHECK(Misc.GrabLeaf().AsU16()		== uint16(16));
			CHECK(Misc.GrabLeaf().AsS32()		== int32(-32));
			CHECK(Misc.GrabLeaf().AsU32()		== uint32(32));
			CHECK(Misc.GrabLeaf().AsS64()		== int64(-64));
			CHECK(Misc.GrabLeaf().AsU64()		== uint64(64));

			FTestMemberReader Mins(Objects[1]);
			CHECK(Mins.GrabLeaf().AsFloat()		== std::numeric_limits< float>::min());
			CHECK(Mins.GrabLeaf().AsDouble()	== std::numeric_limits<double>::min());
			CHECK(Mins.GrabLeaf().AsS8()		== std::numeric_limits<  int8>::min());
			CHECK(Mins.GrabLeaf().AsU8()		== std::numeric_limits< uint8>::min());
			CHECK(Mins.GrabLeaf().AsS16()		== std::numeric_limits< int16>::min());
			CHECK(Mins.GrabLeaf().AsU16()		== std::numeric_limits<uint16>::min());
			CHECK(Mins.GrabLeaf().AsS32()		== std::numeric_limits< int32>::min());
			CHECK(Mins.GrabLeaf().AsU32()		== std::numeric_limits<uint32>::min());
			CHECK(Mins.GrabLeaf().AsS64()		== std::numeric_limits< int64>::min());
			CHECK(Mins.GrabLeaf().AsU64()		== std::numeric_limits<uint64>::min());

			FTestMemberReader Maxs(Objects[2]);
			CHECK(Maxs.GrabLeaf().AsFloat()		== std::numeric_limits< float>::max());
			CHECK(Maxs.GrabLeaf().AsDouble()	== std::numeric_limits<double>::max());
			CHECK(Maxs.GrabLeaf().AsS8()		== std::numeric_limits<  int8>::max());
			CHECK(Maxs.GrabLeaf().AsU8()		== std::numeric_limits< uint8>::max());
			CHECK(Maxs.GrabLeaf().AsS16()		== std::numeric_limits< int16>::max());
			CHECK(Maxs.GrabLeaf().AsU16()		== std::numeric_limits<uint16>::max());
			CHECK(Maxs.GrabLeaf().AsS32()		== std::numeric_limits< int32>::max());
			CHECK(Maxs.GrabLeaf().AsU32()		== std::numeric_limits<uint32>::max());
			CHECK(Maxs.GrabLeaf().AsS64()		== std::numeric_limits< int64>::max());
			CHECK(Maxs.GrabLeaf().AsU64()		== std::numeric_limits<uint64>::max());

			FTestMemberReader Some(Objects[3]);
			CHECK(Names[Some.PeekName()] == "S32");
			CHECK(Some.GrabLeaf().AsS32() == 0);
		});
	}

	SECTION("Dense")
	{
		static constexpr std::initializer_list<const char*> MemberNames =  {"A", "B", "C"};

		TestSerialize([](FTestBatchBuilder& Batch, FScratchAllocator& Scratch)
		{
			FStructSchemaId ExplicitId = Batch.DeclareStruct("Test", "ExplicitDense", MemberNames, EMemberPresence::RequireAll);
			FStructSchemaId ImplicitId = Batch.DeclareStruct("Test", "ImplicitDense", MemberNames, EMemberPresence::AllowSparse);

			FMemberBuilder X;
			X.Add(Batch.NameMember("A"), char8_t('a'));
			X.Add(Batch.NameMember("B"), char16_t('b'));
			X.Add(Batch.NameMember("C"), char32_t('c'));

			FMemberBuilder Y;
			Y.Add(Batch.NameMember("A"), char8_t('1'));
			Y.Add(Batch.NameMember("B"), char16_t('2'));
			Y.Add(Batch.NameMember("C"), char32_t('3'));
						
			Batch.AddObject(ExplicitId, MoveTemp(X));
			Batch.AddObject(ImplicitId, MoveTemp(Y));
		}, 
		[](TConstArrayView<FStructView> Objects, const FTestNameReader& Names)
		{
			CHECK(Objects.Num() == 2);
			FTestMemberReader X(Objects[0]);
			FTestMemberReader Y(Objects[1]);
			
			CHECK(Names[X.PeekName()]		== "A");
			CHECK(X.GrabLeaf().AsChar8()	== 'a');
			CHECK(Names[X.PeekName()]		== "B");
			CHECK(X.GrabLeaf().AsChar16()	== 'b');
			CHECK(Names[X.PeekName()]		== "C");
			CHECK(X.GrabLeaf().AsChar32()	== 'c');

			CHECK(Names[Y.PeekName()]		== "A");
			CHECK(Y.GrabLeaf().AsChar8()	== '1');
			CHECK(Names[Y.PeekName()]		== "B");
			CHECK(Y.GrabLeaf().AsChar16()	== '2');
			CHECK(Names[Y.PeekName()]		== "C");
			CHECK(Y.GrabLeaf().AsChar32()	== '3');
		});
	}

	SECTION("Struct")
	{
		static constexpr std::initializer_list<const char*> ObjectMembers =  {"L1", "S", "N", "L2"};
		static constexpr std::initializer_list<const char*> StructMembers =  {"Nested", "Leaf"};
		static constexpr std::initializer_list<const char*> NestedMembers =  {"I1", "I2"};

		TestSerialize([](FTestBatchBuilder& Batch, FScratchAllocator& Scratch)
		{
			FStructSchemaId ObjectId = Batch.DeclareStruct("Test", "Object", ObjectMembers, EMemberPresence::AllowSparse);
			FStructSchemaId StructId = Batch.DeclareStruct("Test", "Struct", StructMembers, EMemberPresence::AllowSparse);
			FStructSchemaId NestedId = Batch.DeclareStruct("Test", "Nested", NestedMembers, EMemberPresence::AllowSparse);

			FMemberBuilder Members;
			Members.Add(	Batch.NameMember("I1"), 100);
			FBuiltStruct* NestedInStruct = Members.BuildAndReset(Scratch, Batch.Get(NestedId), Batch);

			Members.AddStruct(Batch.NameMember("Nested"), NestedId, MoveTemp(NestedInStruct));
			Members.Add(Batch.NameMember("Leaf"), true);
			FBuiltStruct* Struct = Members.BuildAndReset(Scratch, Batch.Get(StructId), Batch);
		
			Members.Add(Batch.NameMember("I2"), 200);
			FBuiltStruct* NestedInObject = Members.BuildAndReset(Scratch, Batch.Get(NestedId), Batch);

			Members.Add(Batch.NameMember("L1"), 123.f);
			Members.AddStruct(Batch.NameMember("S"), StructId, MoveTemp(Struct));
			Members.AddStruct(Batch.NameMember("N"), NestedId, MoveTemp(NestedInObject));
			Members.Add(Batch.NameMember("L2"), -45.f);

			Batch.AddObject(ObjectId, MoveTemp(Members));
		}, 
		[](TConstArrayView<FStructView> Objects, const FTestNameReader& Names)
		{
			CHECK(Objects.Num() == 1);

			FTestMemberReader Object(Objects[0]);
			CHECK(Object.GrabLeaf().AsFloat() == 123.f);
			FTestMemberReader Struct(Object.GrabStruct());
			FTestMemberReader NestedInObject(Object.GrabStruct());
			CHECK(Object.GrabLeaf().AsFloat()	== -45.f);
			
			FTestMemberReader NestedInStruct(Struct.GrabStruct());
			CHECK(Struct.GrabLeaf().AsBool() == true);

			CHECK(NestedInObject.GrabLeaf().AsS32() == 200);

			CHECK(NestedInStruct.GrabLeaf().AsS32() == 100);
		});
	}

	SECTION("Enum")
	{
		static constexpr std::initializer_list<const char*> MemberNames = 
		{ "A2", "A0", "B0", "B4", "B5", "B7", "C3", "D34", "Max8", "Max16", "Max32", "Max64", "IF" };

		TestSerialize([](FTestBatchBuilder& Batch, FScratchAllocator& Scratch)
		{
			// Test create holes in the original FNameId, FStructSchemaId and FEnumSchemaId index range
			FStructSchemaId UnusedId = Batch.DeclareStruct("Test", "UnusedStruct", {"U1", "U2"}, EMemberPresence::AllowSparse);
			FStructSchemaId ObjectId = Batch.DeclareStruct("Test", "Enums", MemberNames, EMemberPresence::AllowSparse);

			FEnumSchemaId U	= Batch.DeclareEnum("Test", "UnusedEnum1",	EEnumMode::Flag, ELeafWidth::B8,	{"U3"}, {1}); // Hole
			FEnumSchemaId A	= Batch.DeclareEnum("Test", "FlatDense8",	EEnumMode::Flat, ELeafWidth::B8,	{"A", "B", "C"}, {0, 1, 2});
			FEnumSchemaId X	= Batch.DeclareEnum("Test", "UnusedEnum2",	EEnumMode::Flag, ELeafWidth::B8,	{"U4"}, {1}); // Hole
			FEnumSchemaId B	= Batch.DeclareEnum("Test", "FlagDense8",	EEnumMode::Flag, ELeafWidth::B8,	{"A", "B", "C"}, {1, 2, 4});
			FEnumSchemaId C	= Batch.DeclareEnum("Test", "FlatSparse8",	EEnumMode::Flat, ELeafWidth::B8,	{"A", "B", "C"}, {1, 2, 3});
			FEnumSchemaId D	= Batch.DeclareEnum("Test", "FlagSparse8",	EEnumMode::Flag, ELeafWidth::B8,	{"A", "B", "C"}, {2, 16, 32});
			FEnumSchemaId E	= Batch.DeclareEnum("Test", "FlatLimit8",	EEnumMode::Flat, ELeafWidth::B8,	{"Min", "Max"}, {0, 0xFF});
			FEnumSchemaId F	= Batch.DeclareEnum("Test", "FlatLimit16",	EEnumMode::Flat, ELeafWidth::B16,	{"Min", "Max"}, {0, 0xFFFF});
			FEnumSchemaId G	= Batch.DeclareEnum("Test", "FlatLimit32",	EEnumMode::Flat, ELeafWidth::B32,	{"Min", "Max"}, {0, 0xFFFFFFFF});
			FEnumSchemaId H	= Batch.DeclareEnum("Test", "FlatLimit64",	EEnumMode::Flat, ELeafWidth::B64,	{"Min", "Max"}, {0, 0xFFFFFFFFFFFFFFFF});
			FEnumSchemaId I	= Batch.DeclareEnum("Test", "FlagLimit64",	EEnumMode::Flag, ELeafWidth::B64,	{"One", "Max"}, {1, 0x8000000000000000});
					
			FMemberBuilder Members;
			Members.AddEnum8(Batch.NameMember("A2"),		A, 2);
			Members.AddEnum8(Batch.NameMember("A0"),		A, 0);
			Members.AddEnum8(Batch.NameMember("B0"),		B, 0);
			Members.AddEnum8(Batch.NameMember("B4"),		B, 4);
			Members.AddEnum8(Batch.NameMember("B5"),		B, 5);
			Members.AddEnum8(Batch.NameMember("B7"),		B, 7);
			Members.AddEnum8(Batch.NameMember("C3"),		C, 3);
			Members.AddEnum8(Batch.NameMember("D34"),		D, 34);
			Members.AddEnum8(Batch.NameMember("Max8"),		E, 0xFF);
			Members.AddEnum16(Batch.NameMember("Max16"),	F, 0xFFFF);
			Members.AddEnum32(Batch.NameMember("Max32"),	G, 0xFFFFFFFF);
			Members.AddEnum64(Batch.NameMember("Max64"),	H, 0xFFFFFFFFFFFFFFFF);
			Members.AddEnum64(Batch.NameMember("IF"),		I, 0x8000000000000001);

			Batch.AddObject(ObjectId, MoveTemp(Members));
		}, 
		[](TConstArrayView<FStructView> Objects, const FTestNameReader& Names)
		{
			CHECK(Objects.Num() == 1);

			FReadBatchId Batch = Objects[0].Schema.Batch;
			auto GetEnumName = [Batch, &Names](FLeafView Leaf) { return Names[ResolveEnumSchema(Batch, Leaf.Enum).Type.Name]; };

			FTestMemberReader It1(Objects[0]);
			CHECK(It1.GrabLeaf().AsEnum8() == 2);
			CHECK(It1.GrabLeaf().AsEnum8() == 0);
			CHECK(It1.GrabLeaf().AsEnum8() == 0);
			CHECK(It1.GrabLeaf().AsEnum8() == 4);
			CHECK(It1.GrabLeaf().AsEnum8() == 5);
			CHECK(It1.GrabLeaf().AsEnum8() == 7);
			CHECK(It1.GrabLeaf().AsEnum8() == 3);
			CHECK(It1.GrabLeaf().AsEnum8() == 34);
			CHECK(It1.GrabLeaf().AsEnum8() == 0xFF);
			CHECK(It1.GrabLeaf().AsEnum16() == 0xFFFF);
			CHECK(It1.GrabLeaf().AsEnum32() == 0xFFFFFFFF);
			CHECK(It1.GrabLeaf().AsEnum64() == 0xFFFFFFFFFFFFFFFF);
			CHECK(It1.GrabLeaf().AsEnum64() == 0x8000000000000001);	

			FTestMemberReader It2(Objects[0]);
			CHECK(GetEnumName(It2.GrabLeaf()) == "FlatDense8");
			CHECK(GetEnumName(It2.GrabLeaf()) == "FlatDense8");
			CHECK(GetEnumName(It2.GrabLeaf()) == "FlagDense8");
			CHECK(GetEnumName(It2.GrabLeaf()) == "FlagDense8");
			CHECK(GetEnumName(It2.GrabLeaf()) == "FlagDense8");
			CHECK(GetEnumName(It2.GrabLeaf()) == "FlagDense8");
			CHECK(GetEnumName(It2.GrabLeaf()) == "FlatSparse8");
			CHECK(GetEnumName(It2.GrabLeaf()) == "FlagSparse8");
			CHECK(GetEnumName(It2.GrabLeaf()) == "FlatLimit8");
			CHECK(GetEnumName(It2.GrabLeaf()) == "FlatLimit16");
			CHECK(GetEnumName(It2.GrabLeaf()) == "FlatLimit32");
			CHECK(GetEnumName(It2.GrabLeaf()) == "FlatLimit64");
			CHECK(GetEnumName(It2.GrabLeaf()) == "FlagLimit64");			
		});
	}

	SECTION("LeafRange")
	{
		enum class EABCD : uint16 { A, B, C, D };
		enum class EUnused1 : uint8 { X };
		enum class EUnused2 : uint8 { Y };

		TestSerialize([](FTestBatchBuilder& Batch, FScratchAllocator& Scratch)
		{
			static constexpr std::initializer_list<const char*> MemberNames = { "B0", "B1", "B8", "B9", "D0", "D3", "Hi", "E3", "E0" };
		
			FStructSchemaId ObjectId = Batch.DeclareStruct("Test", "Object", MemberNames, EMemberPresence::AllowSparse);
			FEnumSchemaId Enum	= Batch.DeclareEnum("Test", "ABCD", EEnumMode::Flat, ELeafWidth::B16, {"A", "B", "C", "D"}, {0, 1, 2, 3});
			FEnumSchemaId UnusedEnum1	= Batch.DeclareEnum("Test", "Unused1", EEnumMode::Flat, ELeafWidth::B8, {"X"}, {0});
			FEnumSchemaId UnusedEnum2	= Batch.DeclareEnum("Test", "Unused2", EEnumMode::Flat, ELeafWidth::B8, {"Y"}, {0});
		
			FMemberBuilder Members;
			Members.AddRange(Batch.NameMember("B0"), BuildLeafRange(Scratch, TConstArrayView<bool>()));
			Members.AddRange(Batch.NameMember("B1"), BuildLeafRange(Scratch, MakeArrayView({true})));
			Members.AddRange(Batch.NameMember("B8"), BuildLeafRange(Scratch, MakeArrayView({false, true, false, true, false, true, false, true})));
			Members.AddRange(Batch.NameMember("B9"), BuildLeafRange(Scratch, MakeArrayView({true, false, true, false, true, false, true, false, true})));
			Members.AddRange(Batch.NameMember("D0"), BuildLeafRange(Scratch, TConstArrayView<double>()));
			Members.AddRange(Batch.NameMember("D3"), BuildLeafRange(Scratch, MakeArrayView({DBL_MIN, 0.0, DBL_MAX})));
			Members.AddRange(Batch.NameMember("Hi"), BuildLeafRange(Scratch, MakeArrayView(u8"Hello!")));
			Members.AddRange(Batch.NameMember("E3"), BuildEnumRange(Scratch, Enum, MakeArrayView({EABCD::B, EABCD::A, EABCD::D})));
			Members.AddRange(Batch.NameMember("E0"), BuildEnumRange(Scratch, Enum, TConstArrayView<EUnused1>()));

			Batch.AddObject(ObjectId, MoveTemp(Members));
		}, 
		[](TConstArrayView<FStructView> Objects, const FTestNameReader& Names)
		{
			CHECK(Objects.Num() == 1);

			FTestMemberReader It(Objects[0]);
			FLeafRangeView B0 = It.GrabRange().AsLeaves();
			FLeafRangeView B1 = It.GrabRange().AsLeaves();
			FLeafRangeView B8 = It.GrabRange().AsLeaves();
			FLeafRangeView B9 = It.GrabRange().AsLeaves();
			FLeafRangeView D0 = It.GrabRange().AsLeaves();
			FLeafRangeView D3 = It.GrabRange().AsLeaves();
			FLeafRangeView Hi = It.GrabRange().AsLeaves();
			FLeafRangeView E3 = It.GrabRange().AsLeaves();
			FLeafRangeView E0 = It.GrabRange().AsLeaves();
			
			CHECK(B0.Num() == 0);
			CHECK(EqualItems(B1.AsBools(),	MakeArrayView({true})));
			CHECK(EqualItems(B8.AsBools(),	MakeArrayView({false, true, false, true, false, true, false, true})));
			CHECK(EqualItems(B9.AsBools(),	MakeArrayView({true, false, true, false, true, false, true, false, true})));
			CHECK(EqualItems(D0.AsDoubles(), TConstArrayView<double>()));
			CHECK(EqualItems(D3.AsDoubles(), MakeArrayView({DBL_MIN, 0.0, DBL_MAX})));
			CHECK(EqualItems(Hi.AsUtf8(), u8"Hello!"));
			CHECK(EqualItems(E3.As<EABCD>(), MakeArrayView({EABCD::B, EABCD::A, EABCD::D})));
			CHECK(EqualItems(E0.As<EUnused1>(), TConstArrayView<EUnused1>()));
		});
	}
		
	SECTION("StructRange")
	{
		TestSerialize([](FTestBatchBuilder& Batch, FScratchAllocator& Scratch)
		{
			FStructSchemaId ObjectId = Batch.DeclareStruct("Test", "Object", {"Structs"}, EMemberPresence::AllowSparse);
			FStructSchemaId StructId = Batch.DeclareStruct("Test", "Struct", {"I", "F"}, EMemberPresence::AllowSparse);
			 
			FStructRangeBuilder Structs(3);
			Structs[0].Add(Batch.NameMember("I"), 1);
			Structs[1].Add(Batch.NameMember("F"), 1.f);
		
			FMemberBuilder Members;
			Members.AddRange(Batch.NameMember("Structs"), Structs.BuildAndReset(Scratch, Batch.Get(StructId), Batch));

			Batch.AddObject(ObjectId, MoveTemp(Members));
		}, 
		[](TConstArrayView<FStructView> Objects, const FTestNameReader& Names)
		{
			CHECK(Objects.Num() == 1);

			FTestMemberReader It(Objects[0]);
			TArray<FTestMemberReader> Structs = MakeArray<FTestMemberReader>(It.GrabRange().AsStructs());
			CHECK(Structs.Num() == 3);
			CHECK(Structs[0].GrabLeaf().AsS32() == 1);
			CHECK(Structs[1].GrabLeaf().AsFloat() == 1.f);	
		});
	}
	
	SECTION("NestedRange")
	{
		enum class EAB : uint8 { A = 1, B = 4 };

		TestSerialize([](FTestBatchBuilder& Batch, FScratchAllocator& Scratch)
		{
			FStructSchemaId Object = Batch.DeclareStruct("Test", "Object", {"IntRs", "EmptyRs", "EnumRs", "StructRs", "StructRRs"}, EMemberPresence::AllowSparse);
			FStructSchemaId XY = Batch.DeclareStruct("Test", "XY", {"X", "Y"}, EMemberPresence::RequireAll);
			FStructSchemaId ZW = Batch.DeclareStruct("Test", "ZW", {"Z", "W"}, EMemberPresence::AllowSparse);
			FEnumSchemaId Enum	= Batch.DeclareEnum("Test", "AB", EEnumMode::Flag, ELeafWidth::B8, {"A", "B"}, {1, 4});
	
			FNestedRangeBuilder IntRs(MakeLeafRangeSchema<int32, int32>(), 3);
			IntRs.Add(BuildLeafRange(Scratch, MakeArrayView({1})));
			IntRs.Add({});
			IntRs.Add(BuildLeafRange(Scratch, MakeArrayView({2, 3})));

			FNestedRangeBuilder EnumRs(MakeEnumRangeSchema<EAB, int32>(Enum), 2);
			EnumRs.Add({});
			EnumRs.Add(BuildEnumRange(Scratch, Enum, MakeArrayView({EAB::A, EAB(0), EAB::B})));

			FStructRangeBuilder XYs(uint64(2));
			XYs[0].Add(Batch.NameMember("X"), 1.f);
			XYs[0].Add(Batch.NameMember("Y"), 2.f);
			XYs[1].Add(Batch.NameMember("X"), 3.f);
			XYs[1].Add(Batch.NameMember("Y"), 4.f);
			FNestedRangeBuilder StructRs(MakeStructRangeSchema(ERangeSizeType::U64, XY), 1);
			StructRs.Add(XYs.BuildAndReset(Scratch, Batch.Get(XY), Batch));

			FStructRangeBuilder ZWs(int16(3));
			ZWs[0].Add(Batch.NameMember("Z"), 1.5f);
			ZWs[2].Add(Batch.NameMember("Z"), 2.5f);
			ZWs[2].Add(Batch.NameMember("W"), 3.5f);
			FMemberSchema ZWRangeSchema = MakeStructRangeSchema(ERangeSizeType::S16, ZW);
			FNestedRangeBuilder ZWRs(ZWRangeSchema, 1);
			ZWRs.Add(ZWs.BuildAndReset(Scratch, Batch.Get(ZW), Batch));
			FNestedRangeBuilder StructRRs(MakeNestedRangeSchema(Scratch, ERangeSizeType::U32, ZWRangeSchema), 1);
			StructRRs.Add(ZWRs.BuildAndReset(Scratch, ERangeSizeType::U32));


			FMemberBuilder Members;
			Members.AddRange(Batch.NameMember("IntRs"), IntRs.BuildAndReset(Scratch, ERangeSizeType::S32));
			Members.AddRange(Batch.NameMember("EmptyRs"), IntRs.BuildAndReset(Scratch, ERangeSizeType::S32));
			Members.AddRange(Batch.NameMember("EnumRs"), EnumRs.BuildAndReset(Scratch, ERangeSizeType::U8));
			Members.AddRange(Batch.NameMember("StructRs"), StructRs.BuildAndReset(Scratch, ERangeSizeType::U64));
			Members.AddRange(Batch.NameMember("StructRRs"), StructRRs.BuildAndReset(Scratch, ERangeSizeType::U32));

			Batch.AddObject(Object, MoveTemp(Members));
		}, 
		[](TConstArrayView<FStructView> Objects, const FTestNameReader& Names)
		{
			CHECK(Objects.Num() == 1);

			FTestMemberReader It(Objects[0]);
			TArray<FRangeView> IntRs = MakeArray<FRangeView>(It.GrabRange().AsRanges());
			FNestedRangeView EmptyRs = It.GrabRange().AsRanges();
			TArray<FRangeView> EnumRs = MakeArray<FRangeView>(It.GrabRange().AsRanges());
			TArray<FRangeView> StructRs = MakeArray<FRangeView>(It.GrabRange().AsRanges());
			TArray<FRangeView> StructRRs = MakeArray<FRangeView>(It.GrabRange().AsRanges());

			CHECK(IntRs.Num() == 3);
			CHECK(EqualItems(IntRs[0].AsLeaves().AsS32s(), MakeArrayView({1})));
			CHECK(IntRs[1].IsEmpty());
			CHECK(EqualItems(IntRs[2].AsLeaves().AsS32s(), MakeArrayView({2, 3})));

			CHECK(EmptyRs.Num() == 0);
			
			CHECK(EnumRs.Num() == 2);
			CHECK(EnumRs[0].IsEmpty());
			CHECK(EqualItems(EnumRs[1].AsLeaves().As<EAB>(), MakeArrayView({EAB::A, EAB(0), EAB::B})));

			CHECK(StructRs.Num() == 1);
			TArray<FTestMemberReader> XYs = MakeArray<FTestMemberReader>(StructRs[0].AsStructs());
			CHECK(Names[XYs[0].PeekName()] == "X");
			CHECK(XYs[0].GrabLeaf().AsFloat() == 1.f);
			CHECK(Names[XYs[0].PeekName()] == "Y");
			CHECK(XYs[0].GrabLeaf().AsFloat() == 2.f);
			CHECK(Names[XYs[1].PeekName()] == "X");
			CHECK(XYs[1].GrabLeaf().AsFloat() == 3.f);
			CHECK(Names[XYs[1].PeekName()] == "Y");
			CHECK(XYs[1].GrabLeaf().AsFloat() == 4.f);

			CHECK(StructRRs.Num() == 1);
			TArray<FRangeView> ZWRs = MakeArray<FRangeView>(StructRRs[0].AsRanges());
			CHECK(ZWRs.Num() == 1);
			TArray<FTestMemberReader> ZWs = MakeArray<FTestMemberReader>(ZWRs[0].AsStructs());
			CHECK(ZWs.Num() == 3);
			CHECK(Names[ZWs[0].PeekName()] == "Z");
			CHECK(ZWs[0].GrabLeaf().AsFloat() == 1.5f);
			CHECK(Names[ZWs[2].PeekName()] == "Z");
			CHECK(ZWs[2].GrabLeaf().AsFloat() == 2.5f);
			CHECK(Names[ZWs[2].PeekName()] == "W");
			CHECK(ZWs[2].GrabLeaf().AsFloat() == 3.5f);
		});

	}
	
	SECTION("UniRange")
	{
		TestSerialize([](FTestBatchBuilder& Batch, FScratchAllocator& Scratch)
		{
			FStructSchemaId Object = Batch.DeclareStruct("Test", "Object", {"Bools", "Structs", "BF", "BT" }, EMemberPresence::AllowSparse);
			FStructSchemaId Struct = Batch.DeclareStruct("Test", "Struct", {"MaybeB", "Bs", "MaybeBs", "B"}, EMemberPresence::AllowSparse);
						
			const bool True = true;
			const bool False = false;
			FNestedRangeBuilder MaybeBs(MakeLeafRangeSchema<bool, bool>(), 1);
			FStructRangeBuilder Structs(10);
			Structs[5].AddRange(Batch.NameMember("MaybeB"), BuildLeafRange(Scratch, &False, true));
			Structs[6].AddRange(Batch.NameMember("MaybeB"), BuildLeafRange(Scratch, &True, false));
			Structs[7].AddRange(Batch.NameMember("MaybeB"), BuildLeafRange(Scratch, &True, true));
			Structs[7].AddRange(Batch.NameMember("Bs"),		BuildLeafRange(Scratch, MakeArrayView({true, true, false, false, true, true, false, false, true, true})));
			MaybeBs.Add(BuildLeafRange(Scratch, &True, true));
			Structs[7].AddRange(Batch.NameMember("MaybeBs"), MaybeBs.BuildAndReset(Scratch, ERangeSizeType::Uni));
			Structs[7].Add(Batch.NameMember("B"), true);
			MaybeBs.Add(BuildLeafRange(Scratch, &True, false));
			Structs[8].AddRange(Batch.NameMember("MaybeBs"), MaybeBs.BuildAndReset(Scratch, ERangeSizeType::Uni));
			Structs[9].Add(Batch.NameMember("B"), false);

			FMemberBuilder Members;
			Members.AddRange(Batch.NameMember("Bools"), BuildLeafRange(Scratch, &True, true));
			Members.AddRange(Batch.NameMember("Structs"), Structs.BuildAndReset(Scratch, Batch.Get(Struct), Batch));
			Members.Add(Batch.NameMember("BF"), false);
			Members.Add(Batch.NameMember("BT"), true);

			Batch.AddObject(Object, MoveTemp(Members));
		}, 
		[](TConstArrayView<FStructView> Objects, const FTestNameReader& Names)
		{
			CHECK(Objects.Num() == 1);
			FTestMemberReader It(Objects[0]);
			
			FBoolRangeView Bools = It.GrabRange().AsLeaves().AsBools();
			TArray<FTestMemberReader> Structs = MakeArray<FTestMemberReader>(It.GrabRange().AsStructs());
			CHECK(It.GrabLeaf().AsBool() == false);
			CHECK(It.GrabLeaf().AsBool() == true);

			CHECK(Bools.Num() == 1);
			CHECK(Bools[0] == true);
			
			CHECK(EqualItems(Structs[5].GrabRange().AsLeaves().AsBools(), MakeArrayView({false})));
			CHECK(Structs[6].GrabRange().AsLeaves().AsBools().Num() == 0);
			CHECK(EqualItems(Structs[7].GrabRange().AsLeaves().AsBools(), MakeArrayView({true})));
			CHECK(EqualItems(Structs[7].GrabRange().AsLeaves().AsBools(), MakeArrayView({true, true, false, false, true, true, false, false, true, true})));
			TArray<FRangeView> MaybeBs7 = MakeArray<FRangeView>(Structs[7].GrabRange().AsRanges());
			CHECK(MaybeBs7.Num() == 1);
			CHECK(EqualItems(MaybeBs7[0].AsLeaves().AsBools(), MakeArrayView({true})));
			CHECK(Structs[7].GrabLeaf().AsBool() == true);
			TArray<FRangeView> MaybeBs8 = MakeArray<FRangeView>(Structs[8].GrabRange().AsRanges());
			CHECK(MaybeBs8.Num() == 1);
			CHECK(MaybeBs8[0].AsLeaves().AsBools().Num() == 0);
			CHECK(Structs[9].GrabLeaf().AsBool() == false);
		});
	}
	
	SECTION("DynamicStruct")
	{
		TestSerialize([](FTestBatchBuilder& Batch, FScratchAllocator& Scratch)
		{
			FStructSchemaId Unused1 = Batch.DeclareStruct("Test", "Unused1", {"X"}, EMemberPresence::AllowSparse);
			FStructSchemaId SA = Batch.DeclareStruct("Test", "SA", {"X"}, EMemberPresence::AllowSparse);
			FStructSchemaId Unused2 = Batch.DeclareStruct("Test", "Unused2", {"X"}, EMemberPresence::AllowSparse);
			FStructSchemaId SB = Batch.DeclareStruct("Test", "SB", {"X"}, EMemberPresence::AllowSparse);
			FStructSchemaId Object = Batch.DeclareStruct("Test", "Object", {"Same", "Some", "None", "Diff"}, EMemberPresence::AllowSparse);
			FStructSchemaId Unused3 = Batch.DeclareStruct("Test", "Unused3", {"X"}, EMemberPresence::AllowSparse);
		
			auto BuildStruct = [&](FStructSchemaId Struct, auto X)
				{
					FMemberBuilder Members;
					Members.Add(Batch.NameMember("X"), X);
					return Members.BuildAndReset(Scratch, Batch.Get(Struct), Batch);
				};

			FMemberBuilder O1;
			O1.AddStruct(Batch.NameMember("Same"), SA, BuildStruct(SA, 0));
			O1.AddStruct(Batch.NameMember("Some"), SA, BuildStruct(SA, 1));
			O1.AddStruct(Batch.NameMember("Diff"), SA, BuildStruct(SA, 2));
			FMemberBuilder O2;
			O2.AddStruct(Batch.NameMember("Same"), SA, BuildStruct(SA, 3));
			O2.AddStruct(Batch.NameMember("Diff"), SB, BuildStruct(SB, 4.f));
			
			Batch.AddObject(Object, MoveTemp(O1));
			Batch.AddObject(Object, MoveTemp(O2));
		}, 
		[](TConstArrayView<FStructView> Objects, const FTestNameReader& Names)
		{
			CHECK(Objects.Num() == 2);

			FTestMemberReader O1(Objects[0]);
			CHECK(O1.PeekType().AsStruct().IsDynamic == 0);
			CHECK(FTestMemberReader(O1.GrabStruct()).GrabLeaf().AsS32() == 0);
			CHECK(O1.PeekType().AsStruct().IsDynamic == 0);
			CHECK(FTestMemberReader(O1.GrabStruct()).GrabLeaf().AsS32() == 1);
			CHECK(O1.PeekType().AsStruct().IsDynamic == 1);
			CHECK(FTestMemberReader(O1.GrabStruct()).GrabLeaf().AsS32() == 2);
			
			FTestMemberReader O2(Objects[1]);
			CHECK(O2.PeekType().AsStruct().IsDynamic == 0);
			CHECK(FTestMemberReader(O2.GrabStruct()).GrabLeaf().AsS32() == 3);
			CHECK(O2.PeekType().AsStruct().IsDynamic == 1);
			CHECK(FTestMemberReader(O2.GrabStruct()).GrabLeaf().AsFloat() == 4.f);
		});
	}

	SECTION("DynamicStructRange")
	{
		TestSerialize([](FTestBatchBuilder& Batch, FScratchAllocator& Scratch)
		{
			FStructSchemaId SA = Batch.DeclareStruct("Test", "SA", {"X"}, EMemberPresence::AllowSparse);
			FStructSchemaId Unused = Batch.DeclareStruct("Test", "Unused2", {"X"}, EMemberPresence::AllowSparse);
			FStructSchemaId SB = Batch.DeclareStruct("Test", "SB", {"X"}, EMemberPresence::AllowSparse);
			FStructSchemaId Object = Batch.DeclareStruct("Test", "Object", {"Same", "Some", "None", "Diff", "SameEmpty", "DiffEmpty", "DiffNested"}, EMemberPresence::AllowSparse);
		
			auto BuildStructRange = [&](FStructSchemaId Struct, auto X)
				{
					FStructRangeBuilder Members(1);
					Members[0].Add(Batch.NameMember("X"), X);
					return Members.BuildAndReset(Scratch, Batch.Get(Struct), Batch);
				};

			
			FMemberBuilder O1;
			O1.AddRange(Batch.NameMember("Same"), BuildStructRange(SA, 10));
			O1.AddRange(Batch.NameMember("Some"), BuildStructRange(SA, 11));
			O1.AddRange(Batch.NameMember("Diff"), BuildStructRange(SA, 12));
			O1.AddRange(Batch.NameMember("SameEmpty"), BuildStructRange(SA, 13));
			O1.AddRange(Batch.NameMember("DiffEmpty"), BuildStructRange(SA, 14));
			FNestedRangeBuilder NestedSA(MakeStructRangeSchema(ERangeSizeType::S32, SA), 1);
			NestedSA.Add(BuildStructRange(SA, 100));
			O1.AddRange(Batch.NameMember("DiffNested"), NestedSA.BuildAndReset(Scratch, ERangeSizeType::S32));
			
			FMemberBuilder O2;
			O2.AddRange(Batch.NameMember("Same"), BuildStructRange(SA, 20));
			O2.AddRange(Batch.NameMember("Diff"), BuildStructRange(SB, 22.f));
			O2.AddRange(Batch.NameMember("SameEmpty"), FStructRangeBuilder(0).BuildAndReset(Scratch, Batch.Get(SA), Batch));
			O2.AddRange(Batch.NameMember("DiffEmpty"), FStructRangeBuilder(0).BuildAndReset(Scratch, Batch.Get(SB), Batch));
			FNestedRangeBuilder NestedSB(MakeStructRangeSchema(ERangeSizeType::S32, SB), 1);
			NestedSB.Add(BuildStructRange(SB, 200.f));
			O2.AddRange(Batch.NameMember("DiffNested"), NestedSB.BuildAndReset(Scratch, ERangeSizeType::S32));
			
			Batch.AddObject(Object, MoveTemp(O1));
			Batch.AddObject(Object, MoveTemp(O2));
		}, 
		[](TConstArrayView<FStructView> Objects, const FTestNameReader& Names)
		{
			CHECK(Objects.Num() == 2);

			FTestMemberReader O1(Objects[0]);
			CHECK(MakeArray<FTestMemberReader>(O1.GrabRange().AsStructs())[0].GrabLeaf().AsS32() == 10);
			CHECK(MakeArray<FTestMemberReader>(O1.GrabRange().AsStructs())[0].GrabLeaf().AsS32() == 11);
			CHECK(MakeArray<FTestMemberReader>(O1.GrabRange().AsStructs())[0].GrabLeaf().AsS32() == 12);
			CHECK(MakeArray<FTestMemberReader>(O1.GrabRange().AsStructs())[0].GrabLeaf().AsS32() == 13);
			CHECK(MakeArray<FTestMemberReader>(O1.GrabRange().AsStructs())[0].GrabLeaf().AsS32() == 14);
			TArray<FRangeView> DiffNested1 = MakeArray<FRangeView>(O1.GrabRange().AsRanges());
			CHECK(MakeArray<FTestMemberReader>(DiffNested1[0].AsStructs())[0].GrabLeaf().AsS32() == 100);

			FTestMemberReader O2(Objects[1]);
			CHECK(MakeArray<FTestMemberReader>(O2.GrabRange().AsStructs())[0].GrabLeaf().AsS32() == 20);
			CHECK(MakeArray<FTestMemberReader>(O2.GrabRange().AsStructs())[0].GrabLeaf().AsFloat() == 22.f);
			CHECK(O2.GrabRange().AsStructs().Num() == 0);
			CHECK(O2.GrabRange().AsStructs().Num() == 0);
			TArray<FRangeView> DiffNested2 = MakeArray<FRangeView>(O2.GrabRange().AsRanges());
			CHECK(MakeArray<FTestMemberReader>(DiffNested2[0].AsStructs())[0].GrabLeaf().AsFloat() == 200.f);
		});
	}

	SECTION("Inheritance")
	{
		TestSerialize([](FTestBatchBuilder& Batch, FScratchAllocator& Scratch)
		{
			FStructSchemaId Unused = Batch.DeclareStruct("Test", "X", {"X"}, EMemberPresence::AllowSparse);
			FStructSchemaId Low = Batch.DeclareStruct("Test", "Low", {"LInt"}, EMemberPresence::AllowSparse);
			FStructSchemaId Mid = Batch.DeclareStruct("Test", "Mid", {"MInt", "MLow"}, EMemberPresence::AllowSparse, ToOptional(Low));
			FStructSchemaId Top = Batch.DeclareStruct("Test", "Top", {"TInt", "TLow", "TMids"}, EMemberPresence::AllowSparse, ToOptional(Mid));
			
			FMemberBuilder Members;
			Members.Add(Batch.NameMember("LInt"), 123);
			Members.BuildSuperStruct(Scratch, Batch.Get(Low), Batch);
			Members.Add(Batch.NameMember("MInt"), 456);
			FMemberBuilder Nested;
			Nested.Add(Batch.NameMember("LInt"), 1000);
			Members.AddStruct(Batch.NameMember("MLow"), Low, Nested.BuildAndReset(Scratch, Batch.Get(Low), Batch));
			Members.BuildSuperStruct(Scratch, Batch.Get(Mid), Batch);
			Members.Add(Batch.NameMember("TInt"), 789);
			Nested.Add(Batch.NameMember("LInt"), 2000);
			Members.AddStruct(Batch.NameMember("TLow"), Low, Nested.BuildAndReset(Scratch, Batch.Get(Low), Batch));
			FStructRangeBuilder NestedRange(1);
			NestedRange[0].Add(Batch.NameMember("MInt"), 3000);
			Members.AddRange(Batch.NameMember("TMids"), NestedRange.BuildAndReset(Scratch, Batch.Get(Mid), Batch));

			Batch.AddObject(Top, MoveTemp(Members));
		}, 
		[](TConstArrayView<FStructView> Objects, const FTestNameReader& Names)
		{
			CHECK(Objects.Num() == 1);

			FTestMemberReader TopIt(Objects[0]);
			FTestMemberReader MidIt(TopIt.GrabStruct());
			FTestMemberReader LowIt(MidIt.GrabStruct());
			CHECK(LowIt.GrabLeaf().AsS32() == 123);
			CHECK(MidIt.GrabLeaf().AsS32() == 456);
			CHECK(FTestMemberReader(MidIt.GrabStruct()).GrabLeaf().AsS32() == 1000);
			CHECK(Names[TopIt.PeekName()] == "TInt");
			CHECK(TopIt.GrabLeaf().AsS32() == 789);
			CHECK(Names[TopIt.PeekName()] == "TLow");
			CHECK(FTestMemberReader(TopIt.GrabStruct()).GrabLeaf().AsS32() == 2000);
			CHECK(Names[TopIt.PeekName()] == "TMids");
			CHECK(MakeArray<FTestMemberReader>(TopIt.GrabRange().AsStructs())[0].GrabLeaf().AsS32() == 3000);

			FFlatMemberReader FlatIt(Objects[0]);
			CHECK(Names[FlatIt.PeekOwner().Name] == "Low");
			CHECK(FlatIt.GrabLeaf().AsS32() == 123);
			CHECK(Names[FlatIt.PeekOwner().Name] == "Mid");
			CHECK(FlatIt.GrabLeaf().AsS32() == 456);
			CHECK(Names[FlatIt.PeekOwner().Name] == "Mid");
			CHECK(FTestMemberReader(FlatIt.GrabStruct()).GrabLeaf().AsS32() == 1000);
			CHECK(Names[FlatIt.PeekOwner().Name] == "Top");
			CHECK(FlatIt.GrabLeaf().AsS32() == 789);
			CHECK(Names[FlatIt.PeekOwner().Name] == "Top");
			CHECK(FTestMemberReader(FlatIt.GrabStruct()).GrabLeaf().AsS32() == 2000);
			CHECK(Names[FlatIt.PeekOwner().Name] == "Top");
			CHECK(MakeArray<FTestMemberReader>(FlatIt.GrabRange().AsStructs())[0].GrabLeaf().AsS32() == 3000);
			CHECK(!FlatIt.HasMore());
		});
	}

	SECTION("SparseInheritance")
	{
		TestSerialize([](FTestBatchBuilder& Batch, FScratchAllocator& Scratch)
		{																										// Usage by A B C 
			FStructSchemaId B0 = Batch.DeclareStruct("Test", "B0", {"0"}, EMemberPresence::AllowSparse);				 // - - -
			FStructSchemaId B1 = Batch.DeclareStruct("Test", "B1", {"1"}, EMemberPresence::AllowSparse, ToOptional(B0)); // 1 - 0
			FStructSchemaId B2 = Batch.DeclareStruct("Test", "B2", {"2"}, EMemberPresence::AllowSparse, ToOptional(B1)); // - 1 1
			FStructSchemaId B3 = Batch.DeclareStruct("Test", "B3", {"3"}, EMemberPresence::AllowSparse, ToOptional(B2)); // - - 0
			FStructSchemaId B4 = Batch.DeclareStruct("Test", "B4", {"4"}, EMemberPresence::AllowSparse, ToOptional(B3)); // 1 1 0
			FStructSchemaId B5 = Batch.DeclareStruct("Test", "B5", {"5"}, EMemberPresence::AllowSparse, ToOptional(B4)); // 1 0 0
			FStructSchemaId B6 = Batch.DeclareStruct("Test", "B6", {"6"}, EMemberPresence::AllowSparse, ToOptional(B5)); // 0 - 0
			FStructSchemaId C5 = Batch.DeclareStruct("Test", "C5", {"5"}, EMemberPresence::AllowSparse, ToOptional(B4)); // - - -
			
			FMemberBuilder A;
			A.Add(Batch.NameMember("1"), 1);
			A.BuildSuperStruct(Scratch, Batch.Get(B1), Batch);
			A.Add(Batch.NameMember("4"), 4);
			A.BuildSuperStruct(Scratch, Batch.Get(B4), Batch);
			A.Add(Batch.NameMember("5"), 5);
			A.BuildSuperStruct(Scratch, Batch.Get(B5), Batch);

			FMemberBuilder B;
			B.Add(Batch.NameMember("2"), 20);
			B.BuildSuperStruct(Scratch, Batch.Get(B2), Batch);
			B.Add(Batch.NameMember("4"), 40);
			B.BuildSuperStruct(Scratch, Batch.Get(B4), Batch);

			FMemberBuilder C;
			C.BuildSuperStruct(Scratch, Batch.Get(B1), Batch); // Empty -> noop
			C.Add(Batch.NameMember("2"), 200);
			C.BuildSuperStruct(Scratch, Batch.Get(B2), Batch);
			C.BuildSuperStruct(Scratch, Batch.Get(B3), Batch); // Empty -> noop
			C.Add(Batch.NameMember("4"), 400);
			C.BuildSuperStruct(Scratch, Batch.Get(B4), Batch);
			C.BuildSuperStruct(Scratch, Batch.Get(B5), Batch); // Empty -> noop
			
			Batch.AddObject(B6, MoveTemp(A));
			Batch.AddObject(B5, MoveTemp(B));
			Batch.AddObject(B6, MoveTemp(C));
		}, 
		[](TConstArrayView<FStructView> Objects, const FTestNameReader& Names)
		{
			CHECK(Objects.Num() == 3);
			
			const FStructSchema& Schema0 = Objects[0].Schema.Resolve();
			const FStructSchema& Schema1 = Objects[1].Schema.Resolve();
			const FStructSchema& Schema2 = Objects[2].Schema.Resolve();
			CHECK(Names[Schema0.Type.Name] == "B6");
			CHECK(Names[Schema1.Type.Name] == "B5");
			CHECK(Names[Schema2.Type.Name] == "B6");
			CHECK(Schema0.GetSuperSchema() == Objects[1].Schema.Id);
			CHECK(Schema2.GetSuperSchema() == Objects[1].Schema.Id);
			CHECK(&Objects[0].Schema.ResolveSuper() == &Schema1);
			
			CHECK(Names[FMemberReader(Objects[0]).GrabStruct().Schema.Resolve().Type.Name]  == "B5");
			CHECK(Names[FMemberReader(Objects[1]).GrabStruct().Schema.Resolve().Type.Name]  == "B4");
			CHECK(Names[FMemberReader(Objects[2]).GrabStruct().Schema.Resolve().Type.Name]  == "B4");

			FTestMemberReader B1(Objects[1]);
			FStructView Super = B1.GrabStruct();

			FFlatMemberReader A(Objects[0]);
			FFlatMemberReader B(Objects[1]);
			FFlatMemberReader C(Objects[2]);
			CHECK(A.GrabLeaf().AsS32() == 1);
			CHECK(A.GrabLeaf().AsS32() == 4);
			CHECK(A.GrabLeaf().AsS32() == 5);
			CHECK(B.GrabLeaf().AsS32() == 20);
			CHECK(B.GrabLeaf().AsS32() == 40);
			CHECK(C.GrabLeaf().AsS32() == 200);
			CHECK(C.GrabLeaf().AsS32() == 400);
			CHECK(!A.HasMore());
			CHECK(!B.HasMore());
			CHECK(!C.HasMore());
		});
	}

	SECTION("SparseIndex")
	{
		TestSerialize([](FTestBatchBuilder& Batch, FScratchAllocator& Scratch)
		{
			FScopeId Unused = Batch.MakeScope("Unused");
			FScopeId NestedUnused1 = Batch.NestScope(Unused, "NestedUnused1");
			FScopeId FlatUsed = Batch.MakeScope("FlatUsed");
			FScopeId NestedUsed = Batch.NestScope(FlatUsed, "NestedUsed");
			FScopeId NestedUnused2 = Batch.NestScope(Unused, "NestedUnused2");
			FScopeId DoubleNested = Batch.NestScope(NestedUsed, "DoubleNested");
			FScopeId NestedUnused3 = Batch.NestScope(FlatUsed, "NestedUnused3");
			
			FTypeId E1T{NestedUnused1,	Batch.MakeTypename("E1")};
			FTypeId E2T{NestedUsed,		Batch.MakeTypename("E2")};
			FTypeId E3T{NestedUnused2,	Batch.MakeTypename("E3")};
			
			FEnumSchemaId E1D = Batch.DeclareEnum(E1T, EEnumMode::Flat, ELeafWidth::B8, {"C1"}, {1});
			FEnumSchemaId E2D = Batch.DeclareEnum(E2T, EEnumMode::Flat, ELeafWidth::B8, {"C2"}, {2});
			FEnumSchemaId E3D = Batch.DeclareEnum(E3T, EEnumMode::Flat, ELeafWidth::B8, {"C3"}, {3});

			FTypeId S1T{NestedUnused1,	Batch.MakeTypename("S1")};
			FTypeId S2T{NestedUsed,		Batch.MakeTypename("S2")};
			FTypeId S3T = Batch.MakeParametricType({NestedUnused2,	Batch.MakeTypename("S3")}, {S1T});
			FTypeId S4T = Batch.MakeParametricType({DoubleNested,	Batch.MakeTypename("S4")}, {S2T, E2T});
			FTypeId S5T = Batch.MakeParametricType({NestedUnused3,	Batch.MakeTypename("S5")}, {E3T, E1T, S2T});
			
			FStructSchemaId S1D = Batch.DeclareStruct(S1T, {"M1"}, EMemberPresence::AllowSparse);
			FStructSchemaId S2D = Batch.DeclareStruct(S2T, {"M2"}, EMemberPresence::AllowSparse);
			FStructSchemaId S3D = Batch.DeclareStruct(S3T, {"M3"}, EMemberPresence::AllowSparse);
			FStructSchemaId S4D = Batch.DeclareStruct(S4T, {"M4"}, EMemberPresence::AllowSparse);
			FStructSchemaId S5D = Batch.DeclareStruct(S5T, {"M5"}, EMemberPresence::AllowSparse);

			FMemberBuilder Members;
			Members.Add(Batch.NameMember("M4"), 1);

			Batch.AddObject(S4D, MoveTemp(Members));
		}, 
		[](TConstArrayView<FStructView> Objects, const FTestNameReader& Names)
		{	
			FReadBatchId Batch = Objects[0].Schema.Batch;
			FTypeId S4T = Objects[0].Schema.Resolve().Type;

			FNestedScope DoubleNested = ResolveUntranslatedNestedScope(Batch, S4T.Scope.AsNested());
			FNestedScope NestedUsed = ResolveUntranslatedNestedScope(Batch, DoubleNested.Outer.AsNested());
			FFlatScopeId FlatUsed = NestedUsed.Outer.AsFlat();
			CHECK(Names[DoubleNested.Inner.Name] == "DoubleNested");
			CHECK(Names[NestedUsed.Inner.Name] == "NestedUsed");
			CHECK(Names[FlatUsed.Name] == "FlatUsed");

			FParametricTypeView S4 = ResolveUntranslatedParametricType(Batch, S4T.Name.AsParametric());
			CHECK(Names[S4.Name.Get().Id] == "S4");
			CHECK(S4.NumParameters == 2);
			
			FTypeId S2T = S4.Parameters[0];
			FTypeId E2T = S4.Parameters[1];
			CHECK(S2T.Scope == DoubleNested.Outer);
			CHECK(E2T.Scope == DoubleNested.Outer);
			CHECK(Names[S2T.Name.AsConcrete().Id] == "S2");
			CHECK(Names[E2T.Name.AsConcrete().Id] == "E2");
		});
	}
}

////////////////////////////////////////////////////////////////////////////

TEST_CASE_NAMED(FPlainPropsLoadSaveTest, "System::Core::Serialization::PlainProps::LoadSave", "[Core][PlainProps][SmokeFilter]")
{
	SECTION("Leaves")
	{}

	SECTION("Enums")
	{}
		
	SECTION("NestedStruct")
	{}
	
	SECTION("StaticArray")
	{}
	
	SECTION("LeafVariant")
	{}
	
	SECTION("BitfieldBool")
	{}

	SECTION("LeafArray")
	{}
	
	SECTION("LeafOptional")
	{}
	
	SECTION("LeafSmartPtr")
	{}

	SECTION("LeafSetWhole")
	{}

	SECTION("LeafSparseArrayAppends")
	{}

	SECTION("LeafSetOps")
	{}
	
	SECTION("SparseStructArray")
	{}

	SECTION("DenseStructArray")
	{}
	
	SECTION("SubStructArray")
	{}

	SECTION("NestedLeafArray")
	{}

	SECTION("NestedStructArray")
	{}

	SECTION("StructToSubStructMapOps")
	{}
}

} // namespace PlainProps
#endif // WITH_TESTS
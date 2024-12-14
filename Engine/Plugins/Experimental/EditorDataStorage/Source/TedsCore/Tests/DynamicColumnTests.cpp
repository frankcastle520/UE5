// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/TypedElementTestColumns.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Debug/TypedElementDatabaseDebugTypes.h"
#include "Misc/CoreDelegates.h"
#if WITH_TESTS

#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"

#include "Misc/AutomationTest.h"

namespace UE::Editor::DataStorage::Tests
{
	BEGIN_DEFINE_SPEC(DynamicColumnTestFixture, "Editor.DataStorage.DynamicColumns", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
		IEditorDataStorageProvider* TedsInterface = nullptr;
		const FName TestTableName = TEXT("TestTable_DynamicColumnsTest");

		TableHandle TestTable;
		TArray<RowHandle> Rows;
		TArray<QueryHandle> QueryHandles;

		TArray<FName> Identifiers;

		TableHandle RegisterTestTable() const
		{
			const TableHandle Table = TedsInterface->FindTable(TestTableName);
				
			if (Table != InvalidTableHandle)
			{
				return Table;
			}
				
			return TedsInterface->RegisterTable(
			{
				FTestColumnA::StaticStruct()
			},
			TestTableName);
		}

		RowHandle CreateTestRow(TableHandle InTableHandle)
		{
			RowHandle Row = TedsInterface->AddRow(InTableHandle);
			Rows.Add(Row);
			return Row;
		}

		QueryHandle RegisterQuery(FQueryDescription&& Query)
		{
			QueryHandle QueryHandle = TedsInterface->RegisterQuery(MoveTemp(Query));
			QueryHandles.Add(QueryHandle);
			return QueryHandle;
		}

		// Wait at least FrameCount gamethread ticks
		void WaitFrames(int32 FrameCount = 2)
		{
			if(ensureMsgf(!IsInGameThread(), TEXT("Must not be on async thread")))
			{
				TPromise<void> Promise;
				TFuture<void> Future = Promise.GetFuture();
				bool bFutureSet = false;

				FDelegateHandle DelegateHandle;

				Async(EAsyncExecution::TaskGraphMainThread, [&Promise, FrameCount, &DelegateHandle, &bFutureSet]()
				{
					const uint64 LastFrameCount = GFrameCounter;
					const uint64 UnblockAt = LastFrameCount + FrameCount;
					DelegateHandle = FCoreDelegates::OnEndFrame.AddLambda([UnblockAt, &Promise, &bFutureSet]()
					{
						const uint64 CurrentFrameCount = GFrameCounter;
						if (UnblockAt < CurrentFrameCount)
						{
							// Handle case where test thread hasn't yet unregistered the delegate
							if (!bFutureSet)
							{
								Promise.SetValue();
								bFutureSet = true;
							}
						}
					});
				});

				Future.Wait();
				
				Async(EAsyncExecution::TaskGraphMainThread, [&DelegateHandle]()
				{
					FCoreDelegates::OnEndFrame.Remove(DelegateHandle);
					DelegateHandle = FDelegateHandle();
				}).Wait();
			}
		}

	
	END_DEFINE_SPEC(DynamicColumnTestFixture)

	void DynamicColumnTestFixture::Define()
	{
		BeforeEach([this]()
		{
			TedsInterface = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName);
			TestTable = RegisterTestTable();
			Identifiers = {TEXT("StaticMesh"), TEXT("Animation"), TEXT("AudioClip")};
			TestTrue("", TedsInterface != nullptr);
		});
			
		Describe("", [this]
		{
			It(TEXT("Tags"), EAsyncExecution::ThreadPool, [this]()
			{
				// Add dynamic columns that are actually tags (ie. dataless)
				Async(EAsyncExecution::TaskGraphMainThread,[this]()
				{
					for (int32 Index = 0; Index <= 2; ++Index)
					{
						CreateTestRow(TestTable);
					}

					TedsInterface->AddColumn<FTestDynamicTag>(Rows[0], Identifiers[0]);
					TedsInterface->AddColumn<FTestDynamicTag>(Rows[0], Identifiers[1]);

					TedsInterface->AddColumn<FTestDynamicTag>(Rows[1], Identifiers[0]);

					TedsInterface->AddColumn<FTestDynamicTag>(Rows[2], Identifiers[1]);

					// Check they were added
					// Note: There is no HasColumn function for syntactic sugar to get a dynamic column type
					TArray<const UScriptStruct*> DynamicTagTypes;
					
					DynamicTagTypes.Emplace(TedsInterface->FindDynamicColumn(FDynamicColumnDescription
						{
							.TemplateType = FTestDynamicTag::StaticStruct(),
							.Identifier = Identifiers[0]
						}));
					DynamicTagTypes.Emplace(TedsInterface->FindDynamicColumn(FDynamicColumnDescription
						{
							.TemplateType = FTestDynamicTag::StaticStruct(),
							.Identifier = Identifiers[1]
						}));
					TestTrue("Expected columns not found", TedsInterface->HasColumns(Rows[0], MakeConstArrayView({DynamicTagTypes[0], DynamicTagTypes[1]})));
					
					TestTrue("Expected columns not found", TedsInterface->HasColumns(Rows[1], MakeConstArrayView({DynamicTagTypes[0]})));
					TestFalse("Unexpected columns found", TedsInterface->HasColumns(Rows[1], MakeConstArrayView({DynamicTagTypes[1]})));

					TestFalse("Unexpected columns found", TedsInterface->HasColumns(Rows[2], MakeConstArrayView({DynamicTagTypes[0]})));
					TestTrue("Expected columns not found", TedsInterface->HasColumns(Rows[2], MakeConstArrayView({DynamicTagTypes[1]})));
				}).Wait();
				
				// Direct Query
				{
					Async(EAsyncExecution::TaskGraphMainThread,[this]()
					{
						using namespace UE::Editor::DataStorage::Queries;

						TArray<RowHandle> RowsToMatch;
						TBitArray WasMatched;
						auto SetExpectedMatches = [&RowsToMatch, &WasMatched](TConstArrayView<RowHandle> Expectation)
						{
							RowsToMatch.Empty(RowsToMatch.Num());
							RowsToMatch.Append(Expectation);
							WasMatched.Reset();
							WasMatched.SetNum(Expectation.Num(), false);
						};
						auto GetMatchCount = [&WasMatched]()
						{
							return WasMatched.CountSetBits();
						};
						auto Callback = CreateDirectQueryCallbackBinding([this, &RowsToMatch, &WasMatched] (IDirectQueryContext& Context, const RowHandle* CallbackRows)
						{
							TConstArrayView<RowHandle> RowsView = MakeConstArrayView(CallbackRows, Context.GetRowCount());
							for (RowHandle Row : RowsView)
							{
								int32 Index = RowsToMatch.Find(Row);
								TestTrue(TEXT("Returned row in query is within expected match array"), Index != INDEX_NONE);
								if (Index != INDEX_NONE)
								{
									TestFalse("Returned row was not duplicated in the callback", WasMatched[Index]);
									WasMatched[Index] = true;
								}
							}
						});					

						// Should match Rows[0]
						{
							QueryHandle Query = RegisterQuery(
								Select().
								Where().
									All<FTestDynamicTag>(Identifiers[0]).
									All<FTestDynamicTag>(Identifiers[1]).
								Compile());
						
							SetExpectedMatches({Rows[0]});
							FQueryResult Result = TedsInterface->RunQuery(Query, Callback);
							TestEqual("Match Row[0]", Result.Count, GetMatchCount());
						}
						{
							// Should match Rows 0 and 1
							QueryHandle Query = RegisterQuery(
								Select().
									Where().
										All<FTestDynamicTag>(Identifiers[0]).
								Compile());
							SetExpectedMatches({Rows[0], Rows[1]});
							FQueryResult Result = TedsInterface->RunQuery(Query, Callback);
							TestEqual("Match Row[0] and Row[1]", Result.Count, GetMatchCount());
						}
						{
							// Should match Rows 1
							QueryHandle Query = RegisterQuery(
								Select().
									Where().
										All<FTestDynamicTag>(Identifiers[0]).
										None<FTestDynamicTag>(Identifiers[1]).
								Compile());
							SetExpectedMatches({Rows[1]});
							FQueryResult Result = TedsInterface->RunQuery(Query, Callback);
							TestEqual("Match Row[1]", Result.Count, GetMatchCount());
						}
						{
							// Should match Rows 0 and 2
							QueryHandle Query = RegisterQuery(
								Select().
									Where().
										All<FTestDynamicTag>(Identifiers[1]).
								Compile());
							SetExpectedMatches({Rows[0], Rows[2]});
							FQueryResult Result = TedsInterface->RunQuery(Query, Callback);
							TestEqual("Match Row[0] and Row[2]", Result.Count, GetMatchCount());
						}
						{
							// Should match Rows 0 and 2
							QueryHandle Query = RegisterQuery(
								Select().
									Where().
										None<FTestDynamicTag>(Identifiers[0]).
										All<FTestDynamicTag>(Identifiers[1]).
								Compile());
							SetExpectedMatches({Rows[2]});
							FQueryResult Result = TedsInterface->RunQuery(Query, Callback);
							TestEqual("Match Row[2]", Result.Count, GetMatchCount());
						}
						{
							// Should match Rows 0, 1 and 2
							QueryHandle Query = RegisterQuery(
								Select().
									Where().
										Any<FTestDynamicTag>(Identifiers[0]).
										Any<FTestDynamicTag>(Identifiers[1]).
								Compile());
							SetExpectedMatches({Rows[0], Rows[1], Rows[2]});
							FQueryResult Result = TedsInterface->RunQuery(Query, Callback);
							TestEqual("Match All Rows", Result.Count, GetMatchCount());
						}
					}).Wait();
				}

				// Processor Query
				{
					TArray<RowHandle> RowsToMatch;
					TArray<int32> MatchCount;
					int32 UnexpectedRowCount;
					auto SetExpectedMatches = [&RowsToMatch, &MatchCount, &UnexpectedRowCount](TConstArrayView<RowHandle> Expectation)
					{
						RowsToMatch.Empty(RowsToMatch.Num());
						RowsToMatch.Append(Expectation);
						MatchCount.Empty(MatchCount.Num());
						MatchCount.SetNum(Expectation.Num());
						for (int32 I = 0; I < MatchCount.Num(); ++I)
						{
							MatchCount[I] = 0;
						}
						UnexpectedRowCount = 0;
					};

					TArray<FName> ActivationKeys;
					TArray<TArray<RowHandle>> QueryExpectedMatchRows;
					
					Async(EAsyncExecution::TaskGraphMainThread,[this, &RowsToMatch, &MatchCount, &UnexpectedRowCount, &ActivationKeys, &QueryExpectedMatchRows]()
					{
						auto Callback = [this, &RowsToMatch, &MatchCount, &UnexpectedRowCount] (IQueryContext& Context, const RowHandle* CallbackRows)
						{
							TConstArrayView<RowHandle> RowsView = MakeConstArrayView(CallbackRows, Context.GetRowCount());
							for (auto Row : RowsView)
							{
								int32 Index = RowsToMatch.Find(Row);
								TestTrue(TEXT("Returned row in query is within expected match array"), Index != INDEX_NONE);
								if (Index != INDEX_NONE)
								{
									++MatchCount[Index];
								}
								else
								{
									++UnexpectedRowCount;
								}
							}
						};

						using namespace UE::Editor::DataStorage::Queries;
						// Setup an activatable processor for the test
						ActivationKeys.Emplace(TEXT("TEST: Match Row[0]"));
						QueryExpectedMatchRows.Emplace(TArray({Rows[0]}));
						RegisterQuery(
							Select(
								ActivationKeys.Last(),
								FProcessor(EQueryTickPhase::FrameEnd, TedsInterface->GetQueryTickGroupName(EQueryTickGroups::SyncDataStorageToExternal))
									.MakeActivatable(ActivationKeys.Last()),
									CopyTemp(Callback)
							)
							.Where()
								.All<FTestDynamicTag>(Identifiers[0])
								.All<FTestDynamicTag>(Identifiers[1])
							.Compile());

						ActivationKeys.Emplace(TEXT("TEST: Match Row[0] and Row[1]"));
						QueryExpectedMatchRows.Emplace(TArray({Rows[0], Rows[1]}));
						RegisterQuery(
							Select(
								ActivationKeys.Last(),
								FProcessor(EQueryTickPhase::FrameEnd, TedsInterface->GetQueryTickGroupName(EQueryTickGroups::SyncDataStorageToExternal))
									.MakeActivatable(ActivationKeys.Last()),
									CopyTemp(Callback)
							)
							.Where()
								.All<FTestDynamicTag>(Identifiers[0])
							.Compile());

						ActivationKeys.Emplace(TEXT("TEST: Match Row[1]"));
						QueryExpectedMatchRows.Emplace(TArray({Rows[1]}));
						RegisterQuery(
							Select(
								ActivationKeys.Last(),
								FProcessor(EQueryTickPhase::FrameEnd, TedsInterface->GetQueryTickGroupName(EQueryTickGroups::SyncDataStorageToExternal))
									.MakeActivatable(ActivationKeys.Last()),
									CopyTemp(Callback)
							)
							.Where()
								.All<FTestDynamicTag>(Identifiers[0])
								.None<FTestDynamicTag>(Identifiers[1])
							.Compile());

						ActivationKeys.Emplace(TEXT("TEST: Match Row[0] and Row[2]"));
						QueryExpectedMatchRows.Emplace(TArray({Rows[0], Rows[2]}));
						RegisterQuery(
							Select(
								ActivationKeys.Last(),
								FProcessor(EQueryTickPhase::FrameEnd, TedsInterface->GetQueryTickGroupName(EQueryTickGroups::SyncDataStorageToExternal))
									.MakeActivatable(ActivationKeys.Last()),
									CopyTemp(Callback)
							)
							.Where()
								.All<FTestDynamicTag>(Identifiers[1])
							.Compile());

						ActivationKeys.Emplace(TEXT("TEST: Match Row[2]"));
						QueryExpectedMatchRows.Emplace(TArray({Rows[2]}));
						RegisterQuery(
							Select(
								ActivationKeys.Last(),
								FProcessor(EQueryTickPhase::FrameEnd, TedsInterface->GetQueryTickGroupName(EQueryTickGroups::SyncDataStorageToExternal))
									.MakeActivatable(ActivationKeys.Last()),
									CopyTemp(Callback)
							)
							.Where()
								.None<FTestDynamicTag>(Identifiers[0])
								.All<FTestDynamicTag>(Identifiers[1])
							.Compile());

						ActivationKeys.Emplace(TEXT("TEST: Match all rows"));
						QueryExpectedMatchRows.Emplace(TArray({Rows[0], Rows[1], Rows[2]}));
						RegisterQuery(
							Select(
								ActivationKeys.Last(),
								FProcessor(EQueryTickPhase::FrameEnd, TedsInterface->GetQueryTickGroupName(EQueryTickGroups::SyncDataStorageToExternal))
									.MakeActivatable(ActivationKeys.Last()),
									CopyTemp(Callback)
							)
							.Where()
								.Any<FTestDynamicTag>(Identifiers[0])
								.Any<FTestDynamicTag>(Identifiers[1])
							.Compile());
					}).Wait();

					for (int32 TestIndex = 0; TestIndex < ActivationKeys.Num(); ++TestIndex)
					{
						SetExpectedMatches(QueryExpectedMatchRows[TestIndex]);
						// Kick off the activation
						TFuture<void> Future = Async(EAsyncExecution::TaskGraphMainThread, [this, &ActivationKeys, TestIndex]()
						{
							TedsInterface->ActivateQueries(ActivationKeys[TestIndex]);
						});
						Future.Wait();

						// Wait for execution to occur
						WaitFrames();
						
						// Check results
						TStringBuilder<256> TestMessage;
						TestMessage.Appendf(TEXT("'%s': Check that no rows processed that weren't expected"), *ActivationKeys[TestIndex].ToString());
						TestEqual(TestMessage.ToString(), UnexpectedRowCount, static_cast<int32>(0));

						TestMessage.Reset();
						for (int32 RowIndex = 0; RowIndex < MatchCount.Num(); ++RowIndex)
						{
							TestMessage.Appendf(TEXT("'%s': Check row matched 1 times"), *ActivationKeys[TestIndex].ToString());
							TestEqual(TestMessage.ToString(), MatchCount[RowIndex], 1);
						}
					}
				}

				WaitFrames();
			});

			It(TEXT("Columns"), EAsyncExecution::ThreadPool, [this]()
			{
				// Add dynamic columns that have data
				Async(EAsyncExecution::TaskGraphMainThread,[this]()
				{
					for (int32 Index = 0; Index <= 2; ++Index)
					{
						CreateTestRow(TestTable);
					}

					TedsInterface->AddColumn(Rows[0], Identifiers[0], FTestDynamicColumn
					{
						.IntArray = {1, 2, 3},
					});
					
					TedsInterface->AddColumn(Rows[0], Identifiers[1], FTestDynamicColumn
					{
						.IntArray = {10, 11, 12, 13},
					});

					TedsInterface->AddColumn(Rows[1], Identifiers[0], FTestDynamicColumn
					{
						.IntArray = {14, 15, 16},
					});

					TedsInterface->AddColumn(Rows[2], Identifiers[1], FTestDynamicColumn
					{
						.IntArray = {11, 22, 33, 44},
					});

					// Check they were added
					// Note: There is no HasColumn function for syntactic sugar to get a dynamic column type
					TArray<const UScriptStruct*> DynamicTagTypes;
								
					DynamicTagTypes.Emplace(TedsInterface->FindDynamicColumn(FDynamicColumnDescription
						{
							.TemplateType = FTestDynamicColumn::StaticStruct(),
							.Identifier = Identifiers[0]
						}));
					DynamicTagTypes.Emplace(TedsInterface->FindDynamicColumn(FDynamicColumnDescription
						{
							.TemplateType = FTestDynamicColumn::StaticStruct(),
							.Identifier = Identifiers[1]
						}));
					TestTrue("Expected columns not found", TedsInterface->HasColumns(Rows[0], MakeConstArrayView({DynamicTagTypes[0], DynamicTagTypes[1]})));
								
					TestTrue("Expected columns not found", TedsInterface->HasColumns(Rows[1], MakeConstArrayView({DynamicTagTypes[0]})));
					TestFalse("Unexpected columns found", TedsInterface->HasColumns(Rows[1], MakeConstArrayView({DynamicTagTypes[1]})));

					TestFalse("Unexpected columns found", TedsInterface->HasColumns(Rows[2], MakeConstArrayView({DynamicTagTypes[0]})));
					TestTrue("Expected columns not found", TedsInterface->HasColumns(Rows[2], MakeConstArrayView({DynamicTagTypes[1]})));
				}).Wait();
				
				// Processor Query
				{
					TArray<FName> ActivationKeys;
					
					struct FExpectation
					{
						RowHandle Row;
						TArray<int32> ColumnValues;

						int32 MatchCount = 0;
						bool bValuesMatch = false;
					};
					
					TArray<TArray<FExpectation>> AllTestExpectations;

					// Expectation for the current test
					int32 UnexpectedRowCount;
					
					Async(EAsyncExecution::TaskGraphMainThread,[this, &ActivationKeys, &AllTestExpectations, &UnexpectedRowCount]()
					{
						using namespace UE::Editor::DataStorage::Queries;

						auto RunTest = [this, &AllTestExpectations, &UnexpectedRowCount](IQueryContext& Context, const RowHandle* RowsPtr, int32 TestIndex)
						{
							TConstArrayView<RowHandle> RowView = MakeConstArrayView(RowsPtr, Context.GetRowCount());
							TConstArrayView<FTestDynamicColumn> ColumnView = MakeConstArrayView(
								Context.GetColumn<FTestDynamicColumn>(Identifiers[0]),
								Context.GetRowCount());

							TArray<FExpectation>& Expectations = AllTestExpectations[TestIndex];
							for (int32 RowIndex = 0, End = Context.GetRowCount(); RowIndex < End; ++RowIndex)
							{
								RowHandle Row = RowView[RowIndex];
																		
								FExpectation* Expectation = Expectations.FindByPredicate([Row](const FExpectation& Expectation)
								{
									return Expectation.Row == Row;
								});
								if (Expectation)
								{
									Expectation->MatchCount++;
									// Check values
									const FTestDynamicColumn& Column = ColumnView[RowIndex];
									if (Column.IntArray.Num() == Expectation->ColumnValues.Num())
									{
										bool bAllMatch = true;
										for (int32 ColumnDataIndex = 0; ColumnDataIndex < Column.IntArray.Num(); ++ColumnDataIndex)
										{
											if (Column.IntArray[ColumnDataIndex] != Expectation->ColumnValues[ColumnDataIndex])
											{
												bAllMatch = false;
												break;
											}
										}
										Expectation->bValuesMatch = bAllMatch;
									}
								}
								else
								{
									++UnexpectedRowCount;
								}
							}
						};
						
						// Setup an activatable processor for the test
						ActivationKeys.Emplace(TEXT("TEST: Match Row[0]"));
						AllTestExpectations.Add(
							{
								{
									.Row = Rows[0],
									.ColumnValues = {1, 2, 3}
								},
								{
									.Row = Rows[1],
									.ColumnValues = {14, 15, 16}
								}
							});

						RegisterQuery(
							Select(
								ActivationKeys.Last(),
								FProcessor(EQueryTickPhase::FrameEnd, TedsInterface->GetQueryTickGroupName(EQueryTickGroups::SyncDataStorageToExternal))
									.MakeActivatable(ActivationKeys.Last()),
								[this, TestIndex = AllTestExpectations.Num() - 1, &RunTest](IQueryContext& Context, const RowHandle* RowsPtr)
								{
									RunTest(Context, RowsPtr, TestIndex);
								})
							.ReadOnly<FTestDynamicColumn>(Identifiers[0])
							.Compile());
					});

					for (int32 TestIndex = 0; TestIndex < ActivationKeys.Num(); ++TestIndex)
					{
						// Kick off the activation
						UnexpectedRowCount = 0;
						TFuture<void> Future = Async(EAsyncExecution::TaskGraphMainThread,
							[this, &ActivationKeys, TestIndex]()
							{
								TedsInterface->ActivateQueries(ActivationKeys[TestIndex]);
							});
						Future.Wait();
						
						WaitFrames();
						
						// Check results
						TStringBuilder<256> TestMessage;
						TestMessage.Appendf(TEXT("'%s': Check that no rows processed that weren't expected"), *ActivationKeys[TestIndex].ToString());
						TestEqual(TestMessage.ToString(), UnexpectedRowCount, static_cast<int32>(0));

						TestMessage.Reset();
						
						TArray<FExpectation>& TestExpectations = AllTestExpectations[TestIndex];
						for (int32 RowIndex = 0; RowIndex < TestExpectations.Num(); ++RowIndex)
						{
							TestMessage.Appendf(TEXT("'%s': Each row matched 1 times"), *ActivationKeys[TestIndex].ToString());
							TestEqual(TestMessage.ToString(), TestExpectations[RowIndex].MatchCount, 1);

							TestMessage.Reset();
							TestMessage.Appendf(TEXT("'%s': Column values are expected"));
							TestEqual(TestMessage.ToString(), TestExpectations[RowIndex].bValuesMatch, true);
						}
					}
				}
			});
		});
		
		AfterEach([this]()
		{
			Identifiers.Empty(Identifiers.Num());
			for (RowHandle Row : Rows)
			{
				TedsInterface->RemoveRow(Row);
			}
			Rows.Empty(Rows.Num());

			for (QueryHandle QueryHandle : QueryHandles)
			{
				TedsInterface->UnregisterQuery(QueryHandle);
			}
			QueryHandles.Empty(QueryHandles.Num());

			TedsInterface = nullptr;
		});
	}
} // namespace UE::Editor::DataStorage::Tests

#endif // WITH_TESTS

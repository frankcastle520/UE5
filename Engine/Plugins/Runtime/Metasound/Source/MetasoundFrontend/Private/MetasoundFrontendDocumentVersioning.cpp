// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundFrontendDocumentVersioning.h"

#if WITH_EDITORONLY_DATA
#include "Algo/Transform.h"
#include "CoreGlobals.h"
#include "Interfaces/MetasoundFrontendInterface.h"
#include "Interfaces/MetasoundFrontendInterfaceRegistry.h"
#include "MetasoundAccessPtr.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundFrontendDocumentBuilder.h"
#include "MetasoundFrontendDocumentController.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundLog.h"
#include "MetasoundTrace.h"
#include "Misc/App.h"


namespace Metasound::Frontend
{
	namespace VersioningPrivate
	{
		class FMigratePagePropertiesTransform : public FMetaSoundFrontendDocumentBuilder::IPropertyVersionTransform
		{
		public:
			virtual ~FMigratePagePropertiesTransform() = default;

			bool Transform(FMetaSoundFrontendDocumentBuilder& OutBuilder) const override
			{
				using namespace Metasound;

				bool bUpdated = false;
				auto MigrateInterfaceInputDefaults = [&](FMetasoundFrontendClassInterface& OutInterface)
				{
					for (FMetasoundFrontendClassInput& Input : OutInterface.Inputs)
					{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
						if (Input.DefaultLiteral.IsValid())
						{
							Input.AddDefault(Frontend::DefaultPageID) = MoveTemp(Input.DefaultLiteral);
							Input.DefaultLiteral = FMetasoundFrontendLiteral::GetInvalid();
							bUpdated = true;
						}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
					}
				};

				FMetasoundFrontendDocument& Document = GetDocumentUnsafe(OutBuilder);
				FMetasoundFrontendGraphClass& GraphClass = Document.RootGraph;
				MigrateInterfaceInputDefaults(GraphClass.Interface);
				for (FMetasoundFrontendClass& Dependency : Document.Dependencies)
				{
					MigrateInterfaceInputDefaults(Dependency.Interface);
				}

				struct FMigratePageGraphs : public FMetasoundFrontendGraphClass::IPropertyVersionTransform
				{
				public:
					virtual ~FMigratePageGraphs() = default;

					virtual bool Transform(FMetasoundFrontendGraphClass& OutClass) const override
					{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
						TArray<FMetasoundFrontendGraph>& Pages = GetPagesUnsafe(OutClass);
						if (Pages.IsEmpty())
						{
							Pages.Add(MoveTemp(OutClass.Graph));
							return true;
						}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

						return false;
					}
				};

				bUpdated |= FMigratePageGraphs().Transform(GraphClass);
				return bUpdated;
			}
		};

		class FVersionDocumentInterfacesTransform : public FMetaSoundFrontendDocumentBuilder::IPropertyVersionTransform
		{
		public:
			virtual ~FVersionDocumentInterfacesTransform() = default;

			bool Transform(FMetaSoundFrontendDocumentBuilder& OutBuilder) const override
			{
				FMetasoundFrontendDocument& Document = GetDocumentUnsafe(OutBuilder);
				if (Document.RequiresInterfaceVersioning())
				{
					Document.VersionInterfaces();
					return true;
				}

				return false;
			}
		};

		class FVersionDocumentTransform
		{
			public:
				virtual ~FVersionDocumentTransform() = default;

			protected:
				virtual FMetasoundFrontendVersionNumber GetTargetVersion() const = 0;

				virtual void TransformInternal(FDocumentHandle) const
				{
					checkNoEntry();
				}

				virtual void TransformInternal(FMetasoundFrontendDocument& OutDocument) const
				{
					FDocumentAccessPtr DocAccessPtr = MakeAccessPtr<FDocumentAccessPtr>(OutDocument.AccessPoint, OutDocument);
					return TransformInternal(FDocumentController::CreateDocumentHandle(DocAccessPtr));
				}

				virtual void TransformInternal(FMetaSoundFrontendDocumentBuilder&) const
				{
				}

			public:
				bool Transform(FDocumentHandle InDocument) const
				{
					if (FMetasoundFrontendDocumentMetadata* Metadata = InDocument->GetMetadata())
					{
						const FMetasoundFrontendVersionNumber TargetVersion = GetTargetVersion();
						if (Metadata->Version.Number < TargetVersion)
						{
							TransformInternal(InDocument);
							Metadata->Version.Number = TargetVersion;
							return true;
						}
					}

					return false;
				}

				virtual bool Transform(FMetaSoundFrontendDocumentBuilder& OutDocumentBuilder) const 
				{
					const FMetasoundFrontendDocumentMetadata& Metadata = OutDocumentBuilder.GetConstDocumentChecked().Metadata;

					const FMetasoundFrontendVersionNumber TargetVersion = GetTargetVersion();
					if (Metadata.Version.Number < TargetVersion)
					{
						TransformInternal(OutDocumentBuilder);
						OutDocumentBuilder.SetVersionNumber(TargetVersion);
						return true;
					}

					return false;
				}
		};

		/** Versions document from 1.0 to 1.1. */
		class FVersionDocument_1_1 : public FVersionDocumentTransform
		{
			FName Name;
			const FString& Path;

		public:
			virtual ~FVersionDocument_1_1() = default;

			FVersionDocument_1_1(FName InName, const FString& InPath)
			: Name(InName)
			, Path(InPath)
			{
			}

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return { 1, 1 };
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
	#if WITH_EDITOR
				FGraphHandle GraphHandle = InDocument->GetRootGraph();
				TArray<FNodeHandle> FrontendNodes = GraphHandle->GetNodes();

				// Before literals could be stored on node inputs directly, they were stored
				// by creating hidden input nodes. Update the doc by finding all hidden input
				// nodes, placing the literal value of the input node directly on the
				// downstream node's input. Then delete the hidden input node.
				for (FNodeHandle& NodeHandle : FrontendNodes)
				{
					const bool bIsHiddenNode = NodeHandle->GetNodeStyle().Display.Visibility == EMetasoundFrontendNodeStyleDisplayVisibility::Hidden;
					const bool bIsInputNode = EMetasoundFrontendClassType::Input == NodeHandle->GetClassMetadata().GetType();
					const bool bIsHiddenInputNode = bIsHiddenNode && bIsInputNode;

					if (bIsHiddenInputNode)
					{
						// Get literal value from input node.
						const FGuid VertexID = GraphHandle->GetVertexIDForInputVertex(NodeHandle->GetNodeName());
						const FMetasoundFrontendLiteral DefaultLiteral = GraphHandle->GetDefaultInput(VertexID);

						// Apply literal value to downstream node's inputs.
						TArray<FOutputHandle> OutputHandles = NodeHandle->GetOutputs();
						if (ensure(OutputHandles.Num() == 1))
						{
							FOutputHandle OutputHandle = OutputHandles[0];
							TArray<FInputHandle> Inputs = OutputHandle->GetConnectedInputs();
							OutputHandle->Disconnect();

							for (FInputHandle& Input : Inputs)
							{
								if (const FMetasoundFrontendLiteral* Literal = Input->GetClassDefaultLiteral())
								{
									if (!Literal->IsEqual(DefaultLiteral))
									{
										Input->SetLiteral(DefaultLiteral);
									}
								}
								else
								{
									Input->SetLiteral(DefaultLiteral);
								}
							}
						}
						GraphHandle->RemoveNode(*NodeHandle);
					}
				}
	#else
				UE_LOG(LogMetaSound, Error, TEXT("Asset '%s' at '%s' must be saved with editor enabled in order to version document to target version '%s'."), *Name.ToString(), *Path, *GetTargetVersion().ToString());
	#endif // !WITH_EDITOR
			}
		};

		/** Versions document from 1.1 to 1.2. */
		class FVersionDocument_1_2 : public FVersionDocumentTransform
		{
		private:
			const FName Name;
			const FString& Path;

		public:
			FVersionDocument_1_2(const FName InName, const FString& InPath)
				: Name(InName)
				, Path(InPath)
			{
			}
			virtual ~FVersionDocument_1_2() = default;

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return { 1, 2 };
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
	#if WITH_EDITOR
				const FMetasoundFrontendGraphClass& GraphClass = InDocument->GetRootGraphClass();
				FMetasoundFrontendClassMetadata Metadata = GraphClass.Metadata;

				Metadata.SetClassName({ "GraphAsset", Name, *Path });
				Metadata.SetDisplayName(FText::FromString(Name.ToString()));
				InDocument->GetRootGraph()->SetGraphMetadata(Metadata);
	#else
				UE_LOG(LogMetaSound, Error, TEXT("Asset '%s' at '%s' must be saved with editor enabled in order to version document to target version '%s'."), *Name.ToString(), *Path, *GetTargetVersion().ToString());
	#endif // !WITH_EDITOR
			}
		};

		/** Versions document from 1.2 to 1.3. */
		class FVersionDocument_1_3 : public FVersionDocumentTransform
		{
		public:
			virtual ~FVersionDocument_1_3() = default;

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return {1, 3};
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
				const FMetasoundFrontendGraphClass& GraphClass = InDocument->GetRootGraphClass();
				FMetasoundFrontendClassMetadata Metadata = GraphClass.Metadata;

				Metadata.SetClassName(FMetasoundFrontendClassName { FName(), *FGuid::NewGuid().ToString(), FName() });
				InDocument->GetRootGraph()->SetGraphMetadata(Metadata);
			}
		};

		/** Versions document from 1.3 to 1.4. */
		class FVersionDocument_1_4 : public FVersionDocumentTransform
		{
		public:
			virtual ~FVersionDocument_1_4() = default;

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return {1, 4};
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
				FMetasoundFrontendDocumentMetadata* Metadata = InDocument->GetMetadata();
				check(Metadata);
				check(Metadata->Version.Number.Major == 1);
				check(Metadata->Version.Number.Minor == 3);

				const TSet<FMetasoundFrontendVersion>& Interfaces = InDocument->GetInterfaceVersions();

				// Version 1.3 did not have an "InterfaceVersion" property on the
				// document, so any document that is being updated should start off
				// with an "Invalid" interface version.
				if (ensure(Interfaces.IsEmpty()))
				{
					// At the time when version 1.4 of the document was introduced, 
					// these were the only available interfaces. 
					static const FMetasoundFrontendVersion PreexistingInterfaceVersions[] = {
						FMetasoundFrontendVersion{"MetaSound", {1, 0}},
						FMetasoundFrontendVersion{"MonoSource", {1, 0}},
						FMetasoundFrontendVersion{"StereoSource", {1, 0}},
						FMetasoundFrontendVersion{"MonoSource", {1, 1}},
						FMetasoundFrontendVersion{"StereoSource", {1, 1}}
					};
					static const int32 NumPreexistingInterfaceVersions = sizeof(PreexistingInterfaceVersions) / sizeof(PreexistingInterfaceVersions[0]);

					TArray<FMetasoundFrontendInterface> CandidateInterfaces;
					IInterfaceRegistry& InterfaceRegistry = IInterfaceRegistry::Get();
					for (int32 i = 0; i < NumPreexistingInterfaceVersions; i++)
					{
						FMetasoundFrontendInterface Interface;
						if (InterfaceRegistry.FindInterface(GetInterfaceRegistryKey(PreexistingInterfaceVersions[i]), Interface))
						{
							CandidateInterfaces.Add(Interface);
						}
					}

					const FMetasoundFrontendGraphClass& RootGraph = InDocument->GetRootGraphClass();
					const TArray<FMetasoundFrontendClass>& Dependencies = InDocument->GetDependencies();
					const TArray<FMetasoundFrontendGraphClass>& Subgraphs = InDocument->GetSubgraphs();

					if (const FMetasoundFrontendInterface* Interface = FindMostSimilarInterfaceSupportingEnvironment(RootGraph, Dependencies, Subgraphs, CandidateInterfaces))
					{
						UE_LOG(LogMetaSound, Display, TEXT("Assigned interface [InterfaceVersion:%s] to document [RootGraphClassName:%s]"),
							*Interface->Version.ToString(), *RootGraph.Metadata.GetClassName().ToString());

						InDocument->AddInterfaceVersion(Interface->Version);
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Failed to find interface for document [RootGraphClassName:%s]"),
							*RootGraph.Metadata.GetClassName().ToString());
					}
				}
			}
		};

		/** Versions document from 1.4 to 1.5. */
		class FVersionDocument_1_5 : public FVersionDocumentTransform
		{
			FName Name;
			const FString& Path;

		public:
			FVersionDocument_1_5(FName InName, const FString& InPath)
				: Name(InName)
				, Path(InPath)
			{
			}
			virtual ~FVersionDocument_1_5() = default;

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return { 1, 5 };
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
	#if WITH_EDITOR
				const FMetasoundFrontendClassMetadata& Metadata = InDocument->GetRootGraphClass().Metadata;
				const FText NewAssetName = FText::FromString(Name.ToString());
				if (Metadata.GetDisplayName().CompareTo(NewAssetName) != 0)
				{
					FMetasoundFrontendClassMetadata NewMetadata = Metadata;
					NewMetadata.SetDisplayName(NewAssetName);
					InDocument->GetRootGraph()->SetGraphMetadata(NewMetadata);
				}
	#else
				UE_LOG(LogMetaSound, Error, TEXT("Asset '%s' at '%s' must be saved with editor enabled in order to version document to target version '%s'."), *Name.ToString(), *Path, *GetTargetVersion().ToString());
	#endif // !WITH_EDITOR
			}
		};

		/** Versions document from 1.5 to 1.6. */
		class FVersionDocument_1_6 : public FVersionDocumentTransform
		{
		public:
			FVersionDocument_1_6() = default;
			virtual ~FVersionDocument_1_6() = default;

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return { 1, 6 };
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
				const FGuid NewAssetClassID = FGuid::NewGuid();
				FMetasoundFrontendGraphClass Class = InDocument->GetRootGraphClass();
				Class.Metadata.SetClassName(FMetasoundFrontendClassName({ }, FName(*NewAssetClassID.ToString()), { }));
			}
		};

		/** Versions document from 1.6 to 1.7. */
		class FVersionDocument_1_7 : public FVersionDocumentTransform
		{
			FName Name;
			const FString& Path;

		public:
			FVersionDocument_1_7(FName InName, const FString& InPath)
				: Name(InName)
				, Path(InPath)
			{
			}
			virtual ~FVersionDocument_1_7() = default;

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return { 1, 7 };
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
	#if WITH_EDITOR
				auto RenameTransform = [](FNodeHandle NodeHandle)
				{
					// Required nodes are all (at the point of this transform) providing
					// unique names and customized display names (ex. 'Audio' for both mono &
					// L/R output, On Play, & 'On Finished'), so do not replace them by nulling
					// out the guid as a name and using the converted FName of the FText DisplayName.
					if (!NodeHandle->IsInterfaceMember())
					{
						const FName NewNodeName = *NodeHandle->GetDisplayName().ToString();
						NodeHandle->IterateInputs([&](FInputHandle InputHandle)
						{
							InputHandle->SetName(NewNodeName);
						});

						NodeHandle->IterateOutputs([&](FOutputHandle OutputHandle)
						{
							OutputHandle->SetName(NewNodeName);
						});

						NodeHandle->SetDisplayName(FText());
						NodeHandle->SetNodeName(NewNodeName);
					}
				};

				InDocument->GetRootGraph()->IterateNodes(RenameTransform, EMetasoundFrontendClassType::Input);
				InDocument->GetRootGraph()->IterateNodes(RenameTransform, EMetasoundFrontendClassType::Output);
	#else
				UE_LOG(LogMetaSound, Error, TEXT("Asset '%s' at '%s' must be saved with editor enabled in order to version document to target version '%s'."), *Name.ToString(), *Path, *GetTargetVersion().ToString());
	#endif // !WITH_EDITOR
			}
		};

		/** Versions document from 1.7 to 1.8. */
		class FVersionDocument_1_8 : public FVersionDocumentTransform
		{
			FName Name;
			const FString& Path;

		public:
			FVersionDocument_1_8(FName InName, const FString& InPath)
				: Name(InName)
				, Path(InPath)
			{
			}
			virtual ~FVersionDocument_1_8() = default;

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return { 1, 8 };
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
	#if WITH_EDITOR
				// Do not serialize MetaData text for dependencies as
				// CacheRegistryData dynamically provides this.
				InDocument->IterateDependencies([](FMetasoundFrontendClass& Dependency)
				{
					constexpr bool bSerializeText = false;
					Dependency.Metadata.SetSerializeText(bSerializeText);

					for (FMetasoundFrontendClassInput& Input : Dependency.Interface.Inputs)
					{
						Input.Metadata.SetSerializeText(false);
					}

					for (FMetasoundFrontendClassOutput& Output : Dependency.Interface.Outputs)
					{
						Output.Metadata.SetSerializeText(false);
					}
				});

				const TSet<FMetasoundFrontendVersion>& InterfaceVersions = InDocument->GetInterfaceVersions();

				using FNameDataTypePair = TPair<FName, FName>;
				TSet<FNameDataTypePair> InterfaceInputs;
				TSet<FNameDataTypePair> InterfaceOutputs;

				for (const FMetasoundFrontendVersion& Version : InterfaceVersions)
				{
					FInterfaceRegistryKey RegistryKey = GetInterfaceRegistryKey(Version);
					const IInterfaceRegistryEntry* Entry = IInterfaceRegistry::Get().FindInterfaceRegistryEntry(RegistryKey);
					if (ensure(Entry))
					{
						const FMetasoundFrontendInterface& Interface = Entry->GetInterface();
						Algo::Transform(Interface.Inputs, InterfaceInputs, [](const FMetasoundFrontendClassInput& Input)
						{
							return FNameDataTypePair(Input.Name, Input.TypeName);
						});

						Algo::Transform(Interface.Outputs, InterfaceOutputs, [](const FMetasoundFrontendClassOutput& Output)
						{
							return FNameDataTypePair(Output.Name, Output.TypeName);
						});
					}
				}

				// Only serialize MetaData text for inputs owned by the graph (not by interfaces)
				FMetasoundFrontendGraphClass RootGraphClass = InDocument->GetRootGraphClass();
				for (FMetasoundFrontendClassInput& Input : RootGraphClass.Interface.Inputs)
				{
					const bool bSerializeText = !InterfaceInputs.Contains(FNameDataTypePair(Input.Name, Input.TypeName));
					Input.Metadata.SetSerializeText(bSerializeText);
				}

				// Only serialize MetaData text for outputs owned by the graph (not by interfaces)
				for (FMetasoundFrontendClassOutput& Output : RootGraphClass.Interface.Outputs)
				{
					const bool bSerializeText = !InterfaceOutputs.Contains(FNameDataTypePair(Output.Name, Output.TypeName));
					Output.Metadata.SetSerializeText(bSerializeText);
				}

				InDocument->SetRootGraphClass(MoveTemp(RootGraphClass));
	#else
			UE_LOG(LogMetaSound, Error, TEXT("Asset '%s' at '%s' must be saved with editor enabled in order to version document to target version '%s'."), *Name.ToString(), *Path, *GetTargetVersion().ToString());
	#endif // !WITH_EDITOR
			}
		};

		/** Versions document from 1.8 to 1.9. */
		class FVersionDocument_1_9 : public FVersionDocumentTransform
		{
			FName Name;
			const FString& Path;

		public:
			FVersionDocument_1_9(FName InName, const FString& InPath)
				: Name(InName)
				, Path(InPath)
			{
			}
			virtual ~FVersionDocument_1_9() = default;

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return { 1, 9 };
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
	#if WITH_EDITOR
				// Display name text is no longer copied at this versioning point for assets
				// from the asset's FName to avoid FText warnings regarding generation from
				// an FString.  It also avoids desync if asset gets moved.
				FMetasoundFrontendGraphClass RootGraphClass = InDocument->GetRootGraphClass();
				RootGraphClass.Metadata.SetDisplayName(FText());
				InDocument->SetRootGraphClass(MoveTemp(RootGraphClass));
	#else
				UE_LOG(LogMetaSound, Error, TEXT("Asset '%s' at '%s' must be saved with editor enabled in order to version document to target version '%s'."), *Name.ToString(), *Path, *GetTargetVersion().ToString());
	#endif // !WITH_EDITOR
			}
		};

		/** Versions document from 1.9 to 1.10. */
		class FVersionDocument_1_10 : public FVersionDocumentTransform
		{
		public:
			virtual ~FVersionDocument_1_10() = default;

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return { 1, 10 };
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
				FMetasoundFrontendGraphClass Class = InDocument->GetRootGraphClass();
				FMetasoundFrontendGraphClassPresetOptions PresetOptions = Class.PresetOptions;
				Class.PresetOptions.bIsPreset = Class.Metadata.GetAndClearAutoUpdateManagesInterface_Deprecated();
				InDocument->SetRootGraphClass(MoveTemp(Class));
			}
		};

		/** Versions document from 1.10 to 1.11. */
		class FVersionDocument_1_11 : public FVersionDocumentTransform
		{
		public:
			virtual ~FVersionDocument_1_11() = default;

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return { 1, 11 };
			}

			void TransformInternal(FDocumentHandle InDocument) const override
			{
				// Clear object literals on inputs that are connected 
				// to prevent referencing assets that are not used in the graph
				InDocument->GetRootGraph()->IterateNodes([](FNodeHandle NodeHandle)
				{
					TArray<FInputHandle> NodeInputs = NodeHandle->GetInputs();
					for (FInputHandle NodeInput : NodeInputs)
					{
						NodeInput->ClearConnectedObjectLiterals();
					}
				});
			}
		};

		/** Versions document from 1.11 to 1.12. */
		class FVersionDocument_1_12 : public FVersionDocumentTransform
		{
			const FName Name;
			const FSoftObjectPath* Path = nullptr;

		public:
			FVersionDocument_1_12(FName InName, const FSoftObjectPath& InAssetPath)
				: Name(InName)
				, Path(&InAssetPath)
			{
			}
			virtual ~FVersionDocument_1_12() = default;

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return { 1, 12 };
			}

			void TransformInternal(FMetaSoundFrontendDocumentBuilder& OutBuilder) const override
			{
				using namespace VersioningPrivate;

				if (IsRunningCookCommandlet())
				{
					UE_LOG(LogMetaSound, Display, TEXT("Resave recommended: Asset '%s' at '%s' skipped migrated editor data/creation of input template nodes during cook to target document version '%s'."), *Name.ToString(), *Path->ToString(), *GetTargetVersion().ToString());
				}
				else
				{
					FMigratePagePropertiesTransform().Transform(OutBuilder);
					OutBuilder.GetMetasoundAsset().MigrateEditorGraph(OutBuilder);
					UE_LOG(LogMetaSound, Display, TEXT("Resave recommended: Asset '%s' at '%s' successfully migrated editor data in target document version '%s'."), *Name.ToString(), *Path->ToString(), *GetTargetVersion().ToString());
				}
			}
		};

		/** Versions document from 1.12 to 1.13. */
		class FVersionDocument_1_13 : public FVersionDocumentTransform
		{
		public:
			virtual ~FVersionDocument_1_13() = default;

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return { 1, 13 };
			}

			void TransformInternal(FMetaSoundFrontendDocumentBuilder& OutBuilder) const override
			{
				FMigratePagePropertiesTransform().Transform(OutBuilder);
			}
		};

		/** Versions document from 1.13 to 1.14. */
		class FVersionDocument_1_14 : public FVersionDocumentTransform
		{
		public:
			virtual ~FVersionDocument_1_14() = default;

			FMetasoundFrontendVersionNumber GetTargetVersion() const override
			{
				return { 1, 14 };
			}

			void TransformInternal(FMetaSoundFrontendDocumentBuilder& OutBuilder) const override
			{
				// Between 1.13 and 1.14, it was possible to add multiple default input page values
				// due to missing versioning logic. This fixes that issue if any data was serialized
				// to a MetaSound Asset by removing any extraneous default data (early values in the
				// array were stale).
				FMetasoundFrontendDocument& Document = const_cast<FMetasoundFrontendDocument&>(OutBuilder.GetConstDocumentChecked());
				for (FMetasoundFrontendClassInput& Input : Document.RootGraph.Interface.Inputs)
				{
					int32 PageIDIndex = INDEX_NONE;
					TArray<FMetasoundFrontendClassInputDefault>& Defaults = const_cast<TArray<FMetasoundFrontendClassInputDefault>&>(Input.GetDefaults());
					for (int32 Index = 0; Index < Defaults.Num(); ++Index)
					{
						FMetasoundFrontendClassInputDefault& Default = Defaults[Index];
						const bool bIsDefault = Default.PageID == Frontend::DefaultPageID;
						if (bIsDefault)
						{
							if (PageIDIndex == INDEX_NONE)
							{
								PageIDIndex = Index;
							}
							else
							{
								Defaults.RemoveAt(PageIDIndex);
								break;
							}
						}
					}
				}

				// Safeguards against prior fix-up corrupting any cached data
				if (IDocumentBuilderRegistry* BuilderRegistry = IDocumentBuilderRegistry::Get())
				{
					BuilderRegistry->ReloadBuilder(Document.RootGraph.Metadata.GetClassName());
				}
			}
		};

		bool VersionBuilderDocument(FMetaSoundFrontendDocumentBuilder& Builder)
		{
			UObject& DocObject = Builder.CastDocumentObjectChecked<UObject>();
			const FName Name = DocObject.GetFName();
			const FString Path = DocObject.GetPathName();

			bool bWasUpdated = false;
			bWasUpdated |= FVersionDocument_1_12(Name, Path).Transform(Builder);
			bWasUpdated |= FVersionDocument_1_13().Transform(Builder);
			bWasUpdated |= FVersionDocument_1_14().Transform(Builder);

			return bWasUpdated;
		}
	} // namespace VersioningPrivate

	bool VersionDocument(FMetaSoundFrontendDocumentBuilder& Builder)
	{
		using namespace VersioningPrivate;

		bool bWasUpdated = false;

		UObject& MetaSoundAsset = Builder.CastDocumentObjectChecked<UObject>();
		const FName Name(*MetaSoundAsset.GetName());
		const FString Path = MetaSoundAsset.GetPathName();

		// Copied as value will be mutated with each applicable transform below
		const FMetasoundFrontendVersionNumber InitVersionNumber = Builder.GetConstDocumentChecked().Metadata.Version.Number;

		// Old manual property transform that was applied prior to versioning schema being added.
		// Only runs if internal logic finds necessary.
		bWasUpdated = FVersionDocumentInterfacesTransform().Transform(Builder);

		if (InitVersionNumber < GetMaxDocumentVersion())
		{
			// Controller (Soft Deprecated) Transforms
			if (InitVersionNumber.Major == 1 && InitVersionNumber.Minor < 12)
			{
				// Page Graph migration must be completed for graph accessor back
				// compat prior to all controller versioning, so just do it here.
				FMigratePagePropertiesTransform().Transform(Builder);

				FDocumentHandle DocHandle = Builder.GetMetasoundAsset().GetDocumentHandle();

				bWasUpdated |= FVersionDocument_1_1(Name, Path).Transform(DocHandle);
				bWasUpdated |= FVersionDocument_1_2(Name, Path).Transform(DocHandle);
				bWasUpdated |= FVersionDocument_1_3().Transform(DocHandle);
				bWasUpdated |= FVersionDocument_1_4().Transform(DocHandle);
				bWasUpdated |= FVersionDocument_1_5(Name, Path).Transform(DocHandle);
				bWasUpdated |= FVersionDocument_1_6().Transform(DocHandle);
				bWasUpdated |= FVersionDocument_1_7(Name, Path).Transform(DocHandle);
				bWasUpdated |= FVersionDocument_1_8(Name, Path).Transform(DocHandle);
				bWasUpdated |= FVersionDocument_1_9(Name, Path).Transform(DocHandle);
				bWasUpdated |= FVersionDocument_1_10().Transform(DocHandle);
				bWasUpdated |= FVersionDocument_1_11().Transform(DocHandle);
				// No longer supported, new versions should go in VersioningPrivate::VersionBuilderDocument
			}

			bWasUpdated |= VersionBuilderDocument(Builder);
			if (bWasUpdated)
			{
				const FMetasoundFrontendVersionNumber& NewVersionNumber = Builder.GetConstDocumentChecked().Metadata.Version.Number;
				UE_LOG(LogMetaSound, Verbose, TEXT("MetaSound at '%s' Document Versioned: '%s' --> '%s'"), *Path, *InitVersionNumber.ToString(), *NewVersionNumber.ToString());
			}
		}

		return bWasUpdated;
	}
} // namespace Metasound::Frontend
#endif // WITH_EDITORONLY_DATA
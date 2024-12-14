// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundDynamicOperatorTransactor.h"
#include "MetasoundFrontendGraph.h"
#include "MetasoundGenerator.h"

#include "Interfaces/MetasoundOutputFormatInterfaces.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace Metasound::Test::Generator::Dynamic
{
	TArray<FVertexName> GetAudioOutputVertexNames(const EMetaSoundOutputAudioFormat Format)
	{
		const Engine::FOutputAudioFormatInfo* FormatInfo = Engine::GetOutputAudioFormatInfo().Find(Format);
		return FormatInfo != nullptr ? FormatInfo->OutputVertexChannelOrder : TArray<FVertexName>{};
	}
	
	class FDynamicGeneratorBuilder
	{
	public:
		FDynamicGeneratorBuilder(FSampleRate SampleRate, int32 BlockSize)
			: OperatorSettings(SampleRate, static_cast<float>(SampleRate) / BlockSize)
			, Generator(OperatorSettings)
			, RenderBuffer(BlockSize)
		{
			check(OperatorSettings.GetNumFramesPerBlock() == BlockSize);

			// Add the minimum required interfaces so we don't get warnings when we run the test(s)
			// NB: if you start getting warnings, check FMetasoundDynamicGraphGenerator to see if the required
			// I/O has changed.
			// TODO: Add a future-proof way to do this
			AddInput<FTrigger>(Frontend::SourceInterface::Inputs::OnPlay, FGuid::NewGuid(), {});
			AddOutput<FAudioBuffer>(Engine::OutputFormatMonoInterface::Outputs::MonoOut, FGuid::NewGuid());

			// Make the generator
			FOperatorBuilderSettings BuilderSettings = FOperatorBuilderSettings::GetDefaultSettings();
			BuilderSettings.bEnableOperatorRebind = true;

			FMetasoundEnvironment Environment;
			Environment.SetValue<uint64>(CoreInterface::Environment::InstanceID, 123);
			
			FMetasoundDynamicGraphGeneratorInitParams InitParams
			{
				{
					OperatorSettings,
					MoveTemp(BuilderSettings),
					MakeShared<FGraph>(Transactor.GetGraph()),
					Environment,
					"TestMetaSoundGenerator",
					GetAudioOutputVertexNames(EMetaSoundOutputAudioFormat::Mono),
					{},
					true
				},
				Transactor.CreateTransformQueue(OperatorSettings, Environment, nullptr) // Create transaction queue
			};
			
			Generator.Init(MoveTemp(InitParams));
		}
		
		template<typename DataType>
		bool AddInput(const FVertexName& Name, const FGuid& NodeGuid, const FLiteral& DefaultLiteral)
		{
			FInputNodeConstructorParams InputNodeParams
			{
				Name,
				NodeGuid,
				Name,
				DefaultLiteral
			};
			
			TUniquePtr<INode> Node = DataRegistry.CreateInputNode(GetMetasoundDataTypeName<DataType>(), MoveTemp(InputNodeParams));

			if (!Node.IsValid())
			{
				return false;
			}

			Transactor.AddNode(InputNodeParams.InstanceID, MoveTemp(Node));

			const auto CreateDataReference = [](
				const FOperatorSettings& InSettings,
				const FName InDataType, 
				const FLiteral& InLiteral,
				const EDataReferenceAccessType InAccessType)
			{
				const Frontend::IDataTypeRegistry& DataRegistry2 = Frontend::IDataTypeRegistry::Get();
				return DataRegistry2.CreateDataReference(InDataType, InAccessType, InLiteral, InSettings);
			};
			
			Transactor.AddInputDataDestination(
				InputNodeParams.InstanceID,
				Name,
				DefaultLiteral,
				CreateDataReference);

			return true;
		}

		void RemoveInput(const FVertexName& Name, const FGuid& NodeGuid)
		{
			Transactor.RemoveInputDataDestination(Name);
			Transactor.RemoveNode(NodeGuid);
		}

		template<typename DataType>
		bool AddOutput(const FVertexName& Name, const FGuid& NodeGuid)
		{
			FOutputNodeConstructorParams OutputNodeParams
			{
				Name,
				NodeGuid,
				Name
			};

			TUniquePtr<INode> Node = DataRegistry.CreateOutputNode(GetMetasoundDataTypeName<DataType>(), MoveTemp(OutputNodeParams));

			if (!Node.IsValid())
			{
				return false;
			}

			Transactor.AddNode(OutputNodeParams.InstanceID, MoveTemp(Node));
			Transactor.AddOutputDataSource(OutputNodeParams.InstanceID, Name);
			return true;
		}

		void RemoveOutput(const FVertexName& Name, const FGuid& NodeGuid)
		{
			Transactor.RemoveOutputDataSource(Name);
			Transactor.RemoveNode(NodeGuid);
		}

		void Execute()
		{
			Generator.OnGenerateAudio(RenderBuffer.GetData(), RenderBuffer.Num());
		}

		const FOperatorSettings OperatorSettings;
		FMetasoundDynamicGraphGenerator Generator;

	private:
		DynamicGraph::FDynamicOperatorTransactor Transactor;
		Frontend::IDataTypeRegistry& DataRegistry = Frontend::IDataTypeRegistry::Get();
		FAudioBuffer RenderBuffer;
	};
	
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FMetasoundGeneratorDynamicVertexInterfaceUpdatedTest,
		"Audio.Metasound.Generator.Dynamic.VertexInterfaceUpdated",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FMetasoundGeneratorDynamicVertexInterfaceUpdatedTest::RunTest(const FString& Parameters)
	{
		// Make a dynamic generator
		FDynamicGeneratorBuilder GeneratorBuilder{ 48000, 480 };

		// Register for vertex interface updates
		FVertexInterfaceData LatestInterfaceData;
		TArray<FVertexInterfaceChange> LatestInterfaceChanges;

		GeneratorBuilder.Generator.OnVertexInterfaceDataUpdated.AddLambda([&LatestInterfaceData](FVertexInterfaceData VertexInterfaceData)
		{
			LatestInterfaceData = MoveTemp(VertexInterfaceData);
		});

		GeneratorBuilder.Generator.OnVertexInterfaceDataUpdatedWithChanges.AddLambda([&LatestInterfaceChanges](const TArray<FVertexInterfaceChange>& VertexInterfaceChanges)
		{
			LatestInterfaceChanges = VertexInterfaceChanges;
		});

		// Add an input
		const FVertexName InputName = "SomeInput";
		const FGuid InputGuid = FGuid::NewGuid();
		{
			// Add the input
			constexpr float DefaultValue = 123.456f;
			UTEST_TRUE("Added input", GeneratorBuilder.AddInput<float>(InputName, InputGuid, DefaultValue));

			// Render to flush the transaction queue
			GeneratorBuilder.Execute();

			// Check that the input actually got added with the default
			const FAnyDataReference* InputRef = LatestInterfaceData.GetInputs().FindDataReference(InputName);
			UTEST_NOT_NULL("Vertex data contains input", InputRef);
			const float* Value = InputRef->GetValue<float>();
			UTEST_NOT_NULL("Value exists", Value);
			UTEST_EQUAL("Value is default", *Value, DefaultValue);

			// Check that the change was tracked
			const TArray<FVertexInterfaceChange> InputChanges = LatestInterfaceChanges.FilterByPredicate([InputName](const FVertexInterfaceChange& Other)
				{
					return Other.VertexName.IsEqual(InputName);
				});
			UTEST_EQUAL("There is only one expected change with our Input", InputChanges.Num(), 1);
			UTEST_EQUAL("Input addition is for the right Vertex", InputChanges[0].VertexName, InputName);
			UTEST_EQUAL("Input addition is for the right Vertex type", InputChanges[0].VertexType, EMetasoundFrontendClassType::Input);
			UTEST_EQUAL("Input addition is the Added type", InputChanges[0].ChangeType, Metasound::EVertexInterfaceChangeType::Added);
		}

		// Remove the input
		GeneratorBuilder.RemoveInput(InputName, InputGuid);
		{
			// Render to flush the transaction queue
			GeneratorBuilder.Execute();

			// Check that the input actually got removed
			const FAnyDataReference* InputRef = LatestInterfaceData.GetInputs().FindDataReference(InputName);
			UTEST_NULL("Vertex data does not contain input", InputRef);

			// Check that the change was tracked
			UTEST_EQUAL("Input removal is present in changes", LatestInterfaceChanges.Num(), 1);
			const FVertexInterfaceChange LastChange = LatestInterfaceChanges.Last();
			UTEST_EQUAL("Input removal is for the right Vertex", LastChange.VertexName, InputName);
			UTEST_EQUAL("Input removal is for the right Vertex type", LastChange.VertexType, EMetasoundFrontendClassType::Input);
			UTEST_EQUAL("Input removal is the Removed type", LastChange.ChangeType, Metasound::EVertexInterfaceChangeType::Removed);
		}

		// Add an output
		const FVertexName OutputName = "SomeOutput";
		const FGuid OutputGuid = FGuid::NewGuid();
		{
			UTEST_TRUE("Added output", GeneratorBuilder.AddOutput<int32>(OutputName, OutputGuid));

			// Render to flush the transaction queue
			GeneratorBuilder.Execute();
			
			// check that the output actually got added
			const FAnyDataReference* OutputRef = LatestInterfaceData.GetOutputs().FindDataReference(OutputName);
			UTEST_NOT_NULL("Vertex data contains output", OutputRef);

			// Check that the change was tracked
			UTEST_EQUAL("Output addition is present in changes", LatestInterfaceChanges.Num(), 1);
			const FVertexInterfaceChange LastChange = LatestInterfaceChanges.Last();
			UTEST_EQUAL("Output addition is for the right Vertex", LastChange.VertexName, OutputName);
			UTEST_EQUAL("Output addition is for the right Vertex type", LastChange.VertexType, EMetasoundFrontendClassType::Output);
			UTEST_EQUAL("Output addition is the Removed type", LastChange.ChangeType, Metasound::EVertexInterfaceChangeType::Added);
		}

		// Remove the output
		GeneratorBuilder.RemoveOutput(OutputName, OutputGuid);
		{
			// Render to flush the transaction queue
			GeneratorBuilder.Execute();

			// Check that the output actually got removed
			const FAnyDataReference* OutputRef = LatestInterfaceData.GetOutputs().FindDataReference(OutputName);
			UTEST_NULL("Vertex data does not contain output", OutputRef);

			// Check that the change was tracked
			UTEST_EQUAL("Output removal is present in changes", LatestInterfaceChanges.Num(), 1);
			const FVertexInterfaceChange LastChange = LatestInterfaceChanges.Last();
			UTEST_EQUAL("Output removal is for the right Vertex", LastChange.VertexName, OutputName);
			UTEST_EQUAL("Output removal is for the right Vertex type", LastChange.VertexType, EMetasoundFrontendClassType::Output);
			UTEST_EQUAL("Output removal is the Removed type", LastChange.ChangeType, Metasound::EVertexInterfaceChangeType::Removed);
		}
		
		return true;
	}
}

#endif

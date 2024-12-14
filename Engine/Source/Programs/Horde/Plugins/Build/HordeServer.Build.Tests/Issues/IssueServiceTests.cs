// Copyright Epic Games, Inc. All Rights Reserved.

extern alias JobDriver;
using System.Buffers;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using EpicGames.Core;
using EpicGames.Horde.Commits;
using EpicGames.Horde.Issues;
using EpicGames.Horde.Jobs;
using EpicGames.Horde.Jobs.Templates;
using EpicGames.Horde.Logs;
using EpicGames.Horde.Projects;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Streams;
using EpicGames.Horde.Users;
using EpicGames.Perforce;
using HordeServer.Issues;
using HordeServer.Jobs;
using HordeServer.Jobs.Graphs;
using HordeServer.Logs;
using HordeServer.Projects;
using HordeServer.Server;
using HordeServer.Storage;
using HordeServer.Streams;
using HordeServer.Tests.Stubs.Services;
using HordeServer.Users;
using JobDriver.JobDriver.Parser;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using MongoDB.Bson;
using MongoDB.Driver;
using Moq;

namespace HordeServer.Tests.Issues
{
	[TestClass]
	public class IssueServiceTests : BuildTestSetup
	{
		class TestJsonLogger : ILogger, IAsyncDisposable
		{
			readonly ILog _log;
			readonly LogBuilder _builder;
			readonly List<(LogLevel, ReadOnlyMemory<byte>)> _events = new List<(LogLevel, ReadOnlyMemory<byte>)>();
			readonly IStorageNamespace _storageNamespace;
			readonly LoggerScopeCollection _scopeCollection = new LoggerScopeCollection();

			int _lineIndex;

			public TestJsonLogger(ILog log, IStorageNamespace storageNamespace)
			{
				_log = log;
				_builder = new LogBuilder(LogFormat.Json, NullLogger.Instance);
				_storageNamespace = storageNamespace;
			}

			public async ValueTask DisposeAsync()
			{
				foreach ((LogLevel level, ReadOnlyMemory<byte> line) in _events)
				{
					byte[] lineWithNewLine = new byte[line.Length + 1];
					line.CopyTo(lineWithNewLine);
					lineWithNewLine[^1] = (byte)'\n';
					await WriteAsync(level, lineWithNewLine);
				}

				await using (IBlobWriter writer = _storageNamespace.CreateBlobWriter())
				{
					IHashedBlobRef<LogNode> handle = await _builder.FlushAsync(writer, true, CancellationToken.None);
					await _storageNamespace.WriteRefAsync(_log.RefName, handle);
				}
			}

			public IDisposable? BeginScope<TState>(TState state) where TState : notnull => _scopeCollection.BeginScope(state);

			public bool IsEnabled(LogLevel logLevel) => true;

			public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception? exception, Func<TState, Exception?, string> formatter)
			{
				LogEvent logEvent = LogEvent.FromState(logLevel, eventId, state, exception, formatter);
				logEvent.AddProperties(_scopeCollection.GetProperties());
				_events.Add((logLevel, logEvent.ToJsonBytes()));
			}

			private async Task WriteAsync(LogLevel level, byte[] line)
			{
				_builder.WriteData(line);

				if (level >= LogLevel.Warning)
				{
					LogEvent ev = ParseEvent(line);
					if (ev.LineIndex == 0)
					{
						LogEventSeverity severity = (level == LogLevel.Warning) ? LogEventSeverity.Warning : LogEventSeverity.Error;
						await _log.AddEventsAsync(new List<NewLogEventData> { new NewLogEventData { LineIndex = _lineIndex, LineCount = ev.LineCount, Severity = severity } }, CancellationToken.None);
					}
				}

				_lineIndex++;
			}

			static LogEvent ParseEvent(byte[] line)
			{
				Utf8JsonReader reader = new Utf8JsonReader(line.AsSpan());
				reader.Read();
				return LogEvent.Read(ref reader);
			}
		}

		const string MainStreamName = "//UE4/Main";
		readonly StreamId _mainStreamId = new StreamId("ue4-main");

		const string ReleaseStreamName = "//UE4/Release";
		readonly StreamId _releaseStreamId = new StreamId("ue4-release");

		const string DevStreamName = "//UE4/Dev";
		readonly StreamId _devStreamId = new StreamId("ue4-dev");
		readonly IGraph _graph;
		readonly PerforceServiceStub _perforce;
		readonly UserId _timId;
		readonly UserId _jerryId;
		readonly UserId _bobId;
		readonly DirectoryReference _autoSdkDir;
		readonly DirectoryReference _workspaceDir;

		public IssueServiceTests()
		{
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				_autoSdkDir = new DirectoryReference("C:\\AutoSDK");
				_workspaceDir = new DirectoryReference("C:\\Horde");
			}
			else
			{
				_autoSdkDir = new DirectoryReference("/AutoSdk");
				_workspaceDir = new DirectoryReference("/Horde");
			}

			ProjectId projectId = new ProjectId("ue5");

			ProjectConfig projectConfig = new ProjectConfig();
			projectConfig.Id = projectId;
			projectConfig.Streams.Add(CreateStream(_mainStreamId, MainStreamName));
			projectConfig.Streams.Add(CreateStream(_releaseStreamId, ReleaseStreamName));
			projectConfig.Streams.Add(CreateStream(_devStreamId, DevStreamName));

			StorageConfig storageConfig = new StorageConfig();
			storageConfig.Backends.Add(new BackendConfig { Id = new BackendId("default-backend"), Type = StorageBackendType.Memory });
			storageConfig.Namespaces.Add(new NamespaceConfig { Id = new NamespaceId("horde-logs"), Backend = new BackendId("default-backend"), GcDelayHrs = 0.0 });

			BuildConfig buildConfig = new BuildConfig();
			buildConfig.Projects.Add(projectConfig);

			GlobalConfig globalConfig = new GlobalConfig();
			globalConfig.Plugins.AddBuildConfig(buildConfig);
			globalConfig.Plugins.AddStorageConfig(storageConfig);
			SetConfig(globalConfig);

			static StreamConfig CreateStream(StreamId streamId, string streamName)
			{
				TemplateRefConfig templateConfig = new TemplateRefConfig { Id = new TemplateId("test-template") };
				templateConfig.Annotations = new NodeAnnotations();
				templateConfig.Annotations.WorkflowId = new WorkflowId("test-workflow-id");

				return new StreamConfig
				{
					Id = streamId,
					Name = streamName,
					Tabs = new List<TabConfig> { new TabConfig { Title = "General", Templates = new List<TemplateId> { new TemplateId("test-template") } } },
					Templates = new List<TemplateRefConfig> { templateConfig },
					Workflows = new List<WorkflowConfig> { new WorkflowConfig { Id = new WorkflowId("test-workflow-id"), IssueHandlers = new List<string> { "Scoped" } } }
				};
			}

			IUser bill = UserCollection.FindOrAddUserByLoginAsync("Bill").Result;
			IUser anne = UserCollection.FindOrAddUserByLoginAsync("Anne").Result;
			IUser bob = UserCollection.FindOrAddUserByLoginAsync("Bob").Result;
			IUser jerry = UserCollection.FindOrAddUserByLoginAsync("Jerry").Result;
			IUser chris = UserCollection.FindOrAddUserByLoginAsync("Chris").Result;
			IUser tim = UserCollection.FindOrAddUserByLoginAsync("Tim").Result;

			_timId = tim.Id;
			_jerryId = UserCollection.FindOrAddUserByLoginAsync("Jerry").Result.Id;
			_bobId = UserCollection.FindOrAddUserByLoginAsync("Bob").Result.Id;

			_perforce = PerforceService;
			_perforce.AddChange(_mainStreamId, 100, bill, "Description", new string[] { "a/b.cpp" });
			_perforce.AddChange(_mainStreamId, 105, anne, "Description", new string[] { "a/c.cpp" });
			_perforce.AddChange(_mainStreamId, 110, bob, "Description", new string[] { "a/d.cpp" });
			_perforce.AddChange(_mainStreamId, 115, 75, jerry, jerry, "Description\n#ROBOMERGE-SOURCE: CL 75 in //UE4/Release/...", new string[] { "a/e.cpp", "a/foo.cpp" });
			_perforce.AddChange(_mainStreamId, 120, 120, chris, tim, "Description\n#ROBOMERGE-OWNER: Tim", new string[] { "a/f.cpp" });
			_perforce.AddChange(_mainStreamId, 125, chris, "Description", new string[] { "a/g.cpp" });
			_perforce.AddChange(_mainStreamId, 130, anne, "Description", new string[] { "a/g.cpp" });
			_perforce.AddChange(_mainStreamId, 135, jerry, "Description", new string[] { "a/g.cpp" });

			List<INode> nodes = new List<INode>();

			NodeAnnotations workflowAnnotations = new NodeAnnotations();
			workflowAnnotations.WorkflowId = new WorkflowId("test-workflow-id");

			nodes.Add(MockNode("Update Version Files", workflowAnnotations));
			nodes.Add(MockNode("Compile UnrealHeaderTool Win64", workflowAnnotations));
			nodes.Add(MockNode("Compile ShooterGameEditor Win64", workflowAnnotations));
			nodes.Add(MockNode("Cook ShooterGame Win64", workflowAnnotations));

			NodeAnnotations staticAnalysisAnnotations = new NodeAnnotations();
			staticAnalysisAnnotations.Add("CompileType", "Static analysis");
			nodes.Add(MockNode("Static Analysis Win64", staticAnalysisAnnotations));

			NodeAnnotations staticAnalysisAnnotations2 = new NodeAnnotations();
			staticAnalysisAnnotations2.Add("CompileType", "Static analysis");
			staticAnalysisAnnotations2.Add("IssueGroup", "StaticAnalysis");
			nodes.Add(MockNode("Static Analysis Win64 v2", staticAnalysisAnnotations2));

			Mock<INodeGroup> grp = new Mock<INodeGroup>(MockBehavior.Strict);
			grp.SetupGet(x => x.Nodes).Returns(nodes);

			Mock<IGraph> graphMock = new Mock<IGraph>(MockBehavior.Strict);
			graphMock.SetupGet(x => x.Groups).Returns(new List<INodeGroup> { grp.Object });
			_graph = graphMock.Object;
		}

		public static INode MockNode(string name, IReadOnlyNodeAnnotations annotations)
		{
			Mock<INode> node = new Mock<INode>(MockBehavior.Strict);
			node.SetupGet(x => x.Name).Returns(name);
			node.SetupGet(x => x.Annotations).Returns(annotations);
			return node.Object;
		}

		delegate void TryGetBatchDelegate(JobStepBatchId id, out IJobStepBatch? batch);
		delegate void TryGetStepDelegate(JobStepId id, out IJobStep? step);

		public IJob CreateJob(StreamId streamId, int change, string name, IGraph graph, TimeSpan time = default, bool promoteByDefault = true, bool updateIssues = true)
		{
			JobId jobId = JobIdUtils.GenerateNewId();
			DateTime utcNow = DateTime.UtcNow;

			List<IJobStepBatch> batches = new List<IJobStepBatch>();
			for (int groupIdx = 0; groupIdx < graph.Groups.Count; groupIdx++)
			{
				INodeGroup @group = graph.Groups[groupIdx];

				List<IJobStep> steps = new List<IJobStep>();
				for (int nodeIdx = 0; nodeIdx < @group.Nodes.Count; nodeIdx++)
				{
					JobStepId stepId = new JobStepId((ushort)((groupIdx * 100) + nodeIdx));

					ILog logFile = LogCollection.AddAsync(jobId, null, null, LogType.Json).Result;

					Mock<IJobStep> step = new Mock<IJobStep>(MockBehavior.Strict);
					step.SetupGet(x => x.Id).Returns(stepId);
					step.SetupGet(x => x.NodeIdx).Returns(nodeIdx);
					step.SetupGet(x => x.LogId).Returns(logFile.Id);
					step.SetupGet(x => x.StartTimeUtc).Returns(utcNow + time);

					steps.Add(step.Object);
				}

				JobStepBatchId batchId = new JobStepBatchId((ushort)(groupIdx * 100));

				Mock<IJobStepBatch> batch = new Mock<IJobStepBatch>(MockBehavior.Strict);
				batch.SetupGet(x => x.Id).Returns(batchId);
				batch.SetupGet(x => x.GroupIdx).Returns(groupIdx);
				batch.SetupGet(x => x.Steps).Returns(steps);
				batches.Add(batch.Object);
			}

			Mock<IJob> job = new Mock<IJob>(MockBehavior.Strict);
			job.SetupGet(x => x.Id).Returns(jobId);

			job.Setup(x => x.TryGetBatch(It.IsAny<JobStepBatchId>(), out It.Ref<IJobStepBatch?>.IsAny))
				.Returns(false);

			job.Setup(x => x.TryGetStep(It.IsAny<JobStepId>(), out It.Ref<IJobStep?>.IsAny))
				.Returns(false);

			foreach (IJobStepBatch batch in batches)
			{
				job.Setup(x => x.TryGetBatch(batch.Id, out It.Ref<IJobStepBatch?>.IsAny))
					.Callback(new TryGetBatchDelegate((JobStepBatchId id, out IJobStepBatch? outBatch) => outBatch = batch))
					.Returns(true);
			}

			foreach (IJobStep step in batches.SelectMany(x => x.Steps))
			{
				job.Setup(x => x.TryGetStep(step.Id, out It.Ref<IJobStep?>.IsAny))
					.Callback(new TryGetStepDelegate((JobStepId id, out IJobStep? outStep) => outStep = step))
					.Returns(true);
			}

			job.SetupGet(x => x.Name).Returns(name);
			job.SetupGet(x => x.StreamId).Returns(streamId);
			job.SetupGet(x => x.TemplateId).Returns(new TemplateId("test-template"));
			job.SetupGet(x => x.CreateTimeUtc).Returns(utcNow);
			job.SetupGet(x => x.CommitId).Returns(CommitIdWithOrder.FromPerforceChange(change));
			job.SetupGet(x => x.Batches).Returns(batches);
			job.SetupGet(x => x.ShowUgsBadges).Returns(promoteByDefault);
			job.SetupGet(x => x.ShowUgsAlerts).Returns(promoteByDefault);
			job.SetupGet(x => x.PromoteIssuesByDefault).Returns(promoteByDefault);
			job.SetupGet(x => x.UpdateIssues).Returns(updateIssues);
			job.SetupGet(x => x.NotificationChannel).Returns("#devtools-horde-slack-testing");
			return job.Object;
		}

		async Task UpdateCompleteStepAsync(IJob job, int batchIdx, int stepIdx, JobStepOutcome outcome)
		{
			IJobStepBatch batch = job.Batches[batchIdx];
			IJobStep step = batch.Steps[stepIdx];

			JobStepRefId jobStepRefId = new JobStepRefId(job.Id, batch.Id, step.Id);
			string nodeName = _graph.Groups[batch.GroupIdx].Nodes[step.NodeIdx].Name;
			await JobStepRefCollection.InsertOrReplaceAsync(jobStepRefId, "TestJob", nodeName, job.StreamId, job.TemplateId, job.CommitId, step.LogId, null, null, JobStepState.Completed, outcome, job.UpdateIssues, null, null, 0.0f, 0.0f, job.CreateTimeUtc, step.StartTimeUtc!.Value, step.StartTimeUtc);

			if (job.UpdateIssues)
			{
				await IssueService.UpdateCompleteStepAsync(job, _graph, batch.Id, step.Id);
			}
		}

		async Task AddEventAsync(IJob job, int batchIdx, int stepIdx, LogLevel logLevel, string? message = null, EventId? id = null)
		{
			ArrayBufferWriter<byte> buffer = new ArrayBufferWriter<byte>();
			using (Utf8JsonWriter writer = new Utf8JsonWriter(buffer))
			{
				writer.WriteStartObject();
				if (id != null)
				{
					writer.WriteNumber("id", (int)id.Value.Id);
				}
				writer.WriteString("level", logLevel.ToString());
				writer.WriteString("message", message ?? String.Empty);
				writer.WriteEndObject();
			}

			buffer.GetSpan(1)[0] = (byte)'\n';
			buffer.Advance(1);

			LogEventSeverity severity = (logLevel == LogLevel.Error) ? LogEventSeverity.Error : (logLevel == LogLevel.Warning) ? LogEventSeverity.Warning : LogEventSeverity.Information;
			await AddEventAsync(job, batchIdx, stepIdx, severity, buffer.WrittenMemory.ToArray());
		}

		async Task AddEventAsync(IJob job, int batchIdx, int stepIdx, LogEventSeverity severity, byte[] data)
		{
			LogId logId = job.Batches[batchIdx].Steps[stepIdx].LogId!.Value;

			ILog log = (await LogCollection.GetAsync(logId, CancellationToken.None))!;

			IStorageNamespace storageNamespace = StorageService.GetNamespace(Namespace.Logs);
			await using (IBlobWriter writer = storageNamespace.CreateBlobWriter())
			{
				LogBuilder builder = new LogBuilder(LogFormat.Json, NullLogger.Instance);
				builder.WriteData(data);

				IHashedBlobRef<LogNode> handle = await builder.FlushAsync(writer, true, CancellationToken.None);
				await storageNamespace.WriteRefAsync(log!.RefName, handle);
			}

			await log.AddEventsAsync(new List<NewLogEventData> { new NewLogEventData { LineIndex = 0, LineCount = 1, Severity = severity } }, CancellationToken.None);
		}

		private async Task<TestJsonLogger> CreateLoggerAsync(IJob job, int batchIdx, int stepIdx)
		{
			LogId logId = job.Batches[batchIdx].Steps[stepIdx].LogId!.Value;
			IStorageNamespace storageNamespace = StorageService.GetNamespace(Namespace.Logs);
			ILog? log = await LogCollection.GetAsync(logId)!;
			Assert.IsNotNull(log);
			return new TestJsonLogger(log, storageNamespace);
		}

		private async Task ParseEventsAsync(IJob job, int batchIdx, int stepIdx, string[] lines)
		{
			void WriteLinesToLogger(ILogger logger)
			{
				using (LogParser parser = new LogParser(logger, new List<string>()))
				{
					for (int idx = 0; idx < lines.Length; idx++)
					{
						parser.WriteLine(lines[idx]);
					}
				}
			}

			await WriteEventsAsync(job, batchIdx, stepIdx, WriteLinesToLogger);
		}

		private async Task WriteEventsAsync(IJob job, int batchIdx, int stepIdx, Action<ILogger> writeEvents)
		{
			LogId logId = job.Batches[batchIdx].Steps[stepIdx].LogId!.Value;
			ILog log = (await LogCollection.GetAsync(logId))!;

			IStorageNamespace storageNamespace = StorageService.GetNamespace(Namespace.Logs);
			await using (TestJsonLogger logger = new TestJsonLogger(log, storageNamespace))
			{
				PerforceMetadataLogger perforceLogger = new PerforceMetadataLogger(logger);
				perforceLogger.AddClientView(_autoSdkDir, "//depot/CarefullyRedist/...", 12345);
				perforceLogger.AddClientView(_workspaceDir, "//UE4/Main/...", 12345);

				writeEvents(perforceLogger);
			}
		}

		[TestMethod]
		public async Task DeleteStreamTestAsync()
		{
			await IssueService.StartAsync(CancellationToken.None);

			// #1
			// Scenario: Warning in first step
			// Expected: Default issues is created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await AddEventAsync(job, 0, 0, LogLevel.Warning);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Warnings);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				Assert.AreEqual("Warnings in Update Version Files", issues[0].Summary);
			}

			// #2
			// Scenario: Stream is deleted
			// Expected: Issue is closed
			{
				UpdateConfig(x => x.Plugins.GetBuildConfig().Projects.Clear());
				await Clock.AdvanceAsync(TimeSpan.FromHours(1.0));

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}
		}

		[TestMethod]
		public async Task DefaultIssueTestAsync()
		{
			// #1
			// Scenario: Warning in first step
			// Expected: Default issues is created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await AddEventAsync(job, 0, 0, LogLevel.Warning);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Warnings);
				IJobStepRef? stepRef = await JobStepRefCollection.FindAsync(job.Id, job.Batches[0].Id, job.Batches[0].Steps[0].Id);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				Assert.AreEqual("Warnings in Update Version Files", issues[0].Summary);

				Assert.AreEqual(1, stepRef!.IssueIds!.Count);
				Assert.AreEqual(1, stepRef!.IssueIds![0], issues[0].Id);
			}

			// #2
			// Scenario: Errors in other steps on same job
			// Expected: Nodes are NOT added to issue
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await AddEventAsync(job, 0, 1, LogLevel.Error);
				await UpdateCompleteStepAsync(job, 0, 1, JobStepOutcome.Failure);
				await AddEventAsync(job, 0, 2, LogLevel.Error);
				await UpdateCompleteStepAsync(job, 0, 2, JobStepOutcome.Failure);

				List<IIssue> issues = (await IssueCollection.FindIssuesAsync()).OrderBy(x => x.Summary).ToList();
				Assert.AreEqual(3, issues.Count);

				Assert.AreEqual("Errors in Compile ShooterGameEditor Win64", issues[0].Summary);
				Assert.AreEqual("Errors in Compile UnrealHeaderTool Win64", issues[1].Summary);
				Assert.AreEqual("Warnings in Update Version Files", issues[2].Summary);
			}

			// #3
			// Scenario: Subsequent jobs also error
			// Expected: Nodes are added to issue, but change outcome to error
			{
				IJob job = CreateJob(_mainStreamId, 110, "Test Build", _graph);
				await AddEventAsync(job, 0, 0, LogLevel.Error);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				List<IIssue> issues = (await IssueCollection.FindIssuesAsync()).OrderBy(x => x.Summary).ToList();
				Assert.AreEqual(3, issues.Count);

				Assert.AreEqual("Errors in Compile ShooterGameEditor Win64", issues[0].Summary);
				Assert.AreEqual("Errors in Compile UnrealHeaderTool Win64", issues[1].Summary);
				Assert.AreEqual("Errors in Update Version Files", issues[2].Summary);
			}

			// #4
			// Scenario: Subsequent jobs also error, but in different node
			// Expected: Additional error is created
			{
				IJob job = CreateJob(_mainStreamId, 110, "Test Build", _graph);
				await AddEventAsync(job, 0, 3, LogLevel.Warning);
				await UpdateCompleteStepAsync(job, 0, 3, JobStepOutcome.Warnings);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(4, issues.Count);
			}

			// #5
			// Add a description to the issue
			{
				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();

				IIssue issue = issues[0];
				await IssueService.UpdateIssueAsync(issue.Id, description: "Hello world!");
				IIssue? newIssue = await IssueCollection.GetIssueAsync(issue.Id);
				Assert.AreEqual(newIssue?.Description, "Hello world!");
			}
		}

		[TestMethod]
		public async Task DefaultIssueTest2Async()
		{
			// #1
			// Scenario: Warning in first step
			// Expected: Default issues is created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await AddEventAsync(job, 0, 0, LogLevel.Warning);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Warnings);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.AreEqual(IssueSeverity.Warning, issues[0].Severity);

				Assert.AreEqual("Warnings in Update Version Files", issues[0].Summary);
			}

			// #2
			// Scenario: Same step errors
			// Expected: Issue state changes to error
			{
				IJob job = CreateJob(_mainStreamId, 110, "Test Build", _graph);
				await AddEventAsync(job, 0, 0, LogLevel.Error);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.AreEqual(IssueSeverity.Error, issues[0].Severity);

				Assert.AreEqual("Errors in Update Version Files", issues[0].Summary);
			}
		}

		[TestMethod]
		public async Task DefaultIssueTest3Async()
		{
			// #1
			// Scenario: Warning in first step
			// Expected: Default issues is created
			{
				string[] lines =
				{
					@"Engine/Source/Editor/SparseVolumeTexture/Private/SparseVolumeTextureOpenVDB.h(38): warning: include path has multiple slashes (<openvdb//math/Half.h>)",
					@"Engine/Source/Editor/SparseVolumeTexture/Private/SparseVolumeTextureOpenVDB.h(38): warning: include path has multiple slashes (<openvdb//math/Half.h>)",
					@"Error: include cycle: 0: VulkanContext.h -> VulkanRenderpass.h -> VulkanContext.h",
					@"Error: Unable to continue until this cycle has been removed.",
					@"Took 692.6159064s to run IncludeTool.exe, ExitCode=1",
					@"ERROR: IncludeTool.exe terminated with an exit code indicating an error (1)",
					@"       while executing task <Spawn Exe=""D:\build\++UE5\Sync\Engine\Binaries\DotNET\IncludeTool\IncludeTool.exe"" Arguments=""-Mode=Scan -Target=UnrealEditor -Platform=Linux -Configuration=Development -WorkingDir=D:\build\++UE5\Sync\Working"" LogOutput=""True"" ErrorLevel=""1"" />",
					@"      at D:\build\++UE5\Sync\Engine\Restricted\NotForLicensees\Build\DevStreams.xml(938)",
					@"       (see d:\build\++UE5\Sync\Engine\Programs\AutomationTool\Saved\Logs\Log.txt for full exception trace)"
				};

				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(2, issues.Count);
				Assert.AreEqual(IssueSeverity.Error, issues[0].Severity);
				Assert.AreEqual(IssueSeverity.Warning, issues[1].Severity);

				Assert.AreEqual("Errors in Update Version Files", issues[0].Summary);
				Assert.AreEqual("Compile warnings in SparseVolumeTextureOpenVDB.h", issues[1].Summary);
			}
		}

		[TestMethod]
		public async Task PerforceCaseIssueTestAsync()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 170 with P4 case error
			// Expected: Creates issue, identifies source file correctly
			{
				IUser chris = await UserCollection.FindOrAddUserByLoginAsync("Chris");
				_perforce.AddChange(_mainStreamId, 150, chris, "Description", new string[] { "Engine/Foo/Bar.txt" });

				IUser john = await UserCollection.FindOrAddUserByLoginAsync("John");
				_perforce.AddChange(_mainStreamId, 160, john, "Description", new string[] { "Engine/Foo/Baz.txt" });

				IJob job = CreateJob(_mainStreamId, 170, "Test Build", _graph);
				await using (TestJsonLogger logger = await CreateLoggerAsync(job, 0, 0))
				{
					logger.LogWarning(KnownLogEvents.AutomationTool_PerforceCase, "    {DepotFile}", new LogValue(LogValueType.DepotPath, "//UE5/Main/Engine/Foo/Bar.txt"));
				}
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Warnings);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.AreEqual(IssueSeverity.Warning, issues[0].Severity);
				Assert.AreEqual("PerforceCase", issues[0].Fingerprints[0].Type);
				Assert.AreEqual(new IssueKey("Bar.txt", IssueKeyType.File), issues[0].Fingerprints[0].Keys.First());
				Assert.AreEqual(chris.Id, issues[0].OwnerId);

				Assert.AreEqual("Inconsistent case for Bar.txt", issues[0].Summary);
			}
		}

		[TestMethod]
		public async Task ShaderIssueTestAsync()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 170 with shader compile error
			// Expected: Creates issue, identifies source file correctly
			{
				string[] lines =
				{
					@"LogModuleManager: Display: Unable to bootstrap from archive C:/Users/buildmachine/AppData/Local/Temp/UnrealXGEWorkingDir/E196A42E4BEB54E0AE6B0EB1573DBD7A//Bootstrap-5ABC233A-4168-4640-ABFD-61E8349924DD.modules, will fallback on normal initialization",
					@"LogShaderCompilers: Warning: 1 Shader compiler errors compiling global shaders for platform SF_PS5:",
					@"LogShaderCompilers: Error: " + FileReference.Combine(_workspaceDir, "Engine/Shaders/Private/Lumen/LumenScreenProbeTracing.usf").FullName + @"(810:95): Shader FScreenProbeTraceMeshSDFsCS, Permutation 95, VF None:	/Engine/Private/Lumen/LumenScreenProbeTracing.usf(810:95): (error, code:5476) - ambiguous call to 'select_internal'. Found 88 possible candidates:",
					@"LogWindows: Error: appError called: Fatal error: [File:D:\build\U5M+Inc\Sync\Engine\Source\Runtime\Engine\Private\ShaderCompiler\ShaderCompiler.cpp] [Line: 7718]",
					@"Took 64.0177761s to run UnrealEditor-Cmd.exe, ExitCode=3",
					@"Copying crash data to d:\build\U5M+Inc\Sync\Engine\Programs\AutomationTool\Saved\Logs\Crashes\UECC-Windows-9AD258A94110A61CB524848FE0D7196D_0000...",
					@"Editor terminated with exit code 3 while running Cook for D:\build\U5M+Inc\Sync\Samples\Games\ShooterGame\ShooterGame.uproject; see log d:\build\U5M+Inc\Sync\Engine\Programs\AutomationTool\Saved\Logs\Cook-2022.07.22-05.32.05.txt",
				};

				IUser chris = await UserCollection.FindOrAddUserByLoginAsync("Chris");
				_perforce.AddChange(_mainStreamId, 150, chris, "Description", new string[] { "Engine/Foo/LumenScreenProbeTracing.usf" });

				IUser john = await UserCollection.FindOrAddUserByLoginAsync("John");
				_perforce.AddChange(_mainStreamId, 160, john, "Description", new string[] { "Engine/Foo/Baz.txt" });

				IJob job = CreateJob(_mainStreamId, 170, "Test Build", _graph);
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.AreEqual(IssueSeverity.Error, issues[0].Severity);
				Assert.AreEqual("Shader", issues[0].Fingerprints[0].Type);
				Assert.AreEqual(new IssueKey("LumenScreenProbeTracing.usf", IssueKeyType.File), issues[0].Fingerprints[0].Keys.First());
				Assert.AreEqual(chris.Id, issues[0].OwnerId);

				Assert.AreEqual("Shader compile errors in LumenScreenProbeTracing.usf", issues[0].Summary);
			}
		}

		[TestMethod]
		public async Task ThreadSanitizerDeduplicateSimilarIssuesTestAsync()
		{
			// We have three unique errors reported, but we key issues on the file since file lines change over time without fixes, so only two unique issues should be found
			string[] lines =
			{
				@"==================",
				@"WARNING: ThreadSanitizer: data race (pid=45069)",
				@"  Write of size 1 at 0x0000408ba588 by thread T15:",
				@"    #0 FPaths::IsStaged() /mnt/horde/++UE5/Sync/Engine/Source/./Runtime/Core/Private/Misc/Paths.cpp:164:33 (CitySampleEditor+0x2c2561a3) (BuildId: 7ca67a0f97321f67)",
				@"    #1 FGenericPlatformMisc::ProjectDir() /mnt/horde/++UE5/Sync/Engine/Source/./Runtime/Core/Private/GenericPlatform/GenericPlatformMisc.cpp:1264:19 (CitySampleEditor+0x2be6a866) (BuildId: 7ca67a0f97321f67)",
				@"",
				@"  Previous write of size 1 at 0x0000408ba588 by thread T13:",
				@"    #0 FPaths::IsStaged() /mnt/horde/++UE5/Sync/Engine/Source/./Runtime/Core/Private/Misc/Paths.cpp:164:33 (CitySampleEditor+0x2c2561a3) (BuildId: 7ca67a0f97321f67)",
				@"",
				@"SUMMARY: ThreadSanitizer: data race /mnt/horde/++UE5/Sync/Engine/Source/./Runtime/Core/Private/Misc/Paths.cpp:164:33 in FPaths::IsStaged()",
				@"==================",
				@"",
				@"==================",
				@"WARNING: ThreadSanitizer: data race (pid=45069)",
				@"  Write of size 1 at 0x0000408ba588 by thread T23:",
				@"    #0 FPaths::IsStaged() /mnt/horde/++UE5/Sync/Engine/Source/./Runtime/Core/Private/Misc/Paths.cpp:172:33 (CitySampleEditor+0x2c256324) (BuildId: 7ca67a0f97321f67)",
				@"",
				@"  Thread T23 'Backgro-ker #17' (tid=45131, running) created by thread T15 at:",
				@"    #0 pthread_create /src/build/llvm-src/compiler-rt/lib/tsan/rtl/tsan_interceptors_posix.cpp:1048 (CitySampleEditor+0x103ffc92) (BuildId: 7ca67a0f97321f67)",
				@"",
				@"SUMMARY: ThreadSanitizer: data race /mnt/horde/++UE5/Sync/Engine/Source/./Runtime/Core/Private/Misc/Paths.cpp:172:33 in FPaths::IsStaged()",
				@"==================",
				@"",
				@"==================",
				@"WARNING: ThreadSanitizer: data race (pid=45069)",
				@"  Read of size 1 at 0x0000408b68d8 by thread T47 (mutexes: write M0, write M1):",
				@"    #0 IsEngineStartupModuleLoadingComplete() /mnt/horde/++UE5/Sync/Engine/Source/./Runtime/Core/Private/Misc/CoreGlobals.cpp:297:9 (CitySampleEditor+0x2c1bb5c8) (BuildId: 7ca67a0f97321f67)",
				@"",
				@"SUMMARY: ThreadSanitizer: data race /mnt/horde/++UE5/Sync/Engine/Source/./Runtime/Core/Private/Misc/CoreGlobals.cpp:297:9 in IsEngineStartupModuleLoadingComplete()",
				@"=================="
			};

			IJob job = CreateJob(_mainStreamId, 120, "Sanitizer Report for 'SomeJob'", _graph);
			await ParseEventsAsync(job, 0, 0, lines);
			await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

			IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
			Assert.AreEqual(2, issues.Count);

			IIssue issue1 = issues[0];
			Assert.AreEqual(issue1.Fingerprints.Count, 1);
			Assert.IsTrue(issue1.Fingerprints[0].Type.Contains("ThreadSanitizer", StringComparison.OrdinalIgnoreCase));
			Assert.IsTrue(issue1.Fingerprints[0].Type.Contains("data race", StringComparison.OrdinalIgnoreCase));
			Assert.IsTrue(issue1.Fingerprints[0].Type.Contains("CoreGlobals.cpp", StringComparison.OrdinalIgnoreCase));

			IIssue issue2 = issues[1];
			Assert.AreEqual(issue2.Fingerprints.Count, 1);
			Assert.IsTrue(issue2.Fingerprints[0].Type.Contains("ThreadSanitizer", StringComparison.OrdinalIgnoreCase));
			Assert.IsTrue(issue2.Fingerprints[0].Type.Contains("data race", StringComparison.OrdinalIgnoreCase));
			Assert.IsTrue(issue2.Fingerprints[0].Type.Contains("Paths.cpp", StringComparison.OrdinalIgnoreCase));
		}

		[TestMethod]
		public async Task ThreadSanitizerEnsureUniqueIssuesForSameFileTestAsync()
		{
			// Generate two errors for the same file and line number but with a different summary reason. We should get two issues as the problems are unique
			string[] lines =
			{
				@"==================",
				@"WARNING: ThreadSanitizer: data race (pid=45069)",
				@"  Write of size 1 at 0x0000408ba588 by thread T15:",
				@"    #0 FPaths::IsStaged() /mnt/horde/++UE5/Sync/Engine/Source/./Runtime/Core/Private/Misc/Paths.cpp:164:33 (CitySampleEditor+0x2c2561a3) (BuildId: 7ca67a0f97321f67)",
				@"    #1 FGenericPlatformMisc::ProjectDir() /mnt/horde/++UE5/Sync/Engine/Source/./Runtime/Core/Private/GenericPlatform/GenericPlatformMisc.cpp:1264:19 (CitySampleEditor+0x2be6a866) (BuildId: 7ca67a0f97321f67)",
				@"",
				@"  Previous write of size 1 at 0x0000408ba588 by thread T13:",
				@"    #0 FPaths::IsStaged() /mnt/horde/++UE5/Sync/Engine/Source/./Runtime/Core/Private/Misc/Paths.cpp:164:33 (CitySampleEditor+0x2c2561a3) (BuildId: 7ca67a0f97321f67)",
				@"",
				@"SUMMARY: ThreadSanitizer: data race /mnt/horde/++UE5/Sync/Engine/Source/./Runtime/Core/Private/Misc/Paths.cpp:164:33 in FPaths::IsStaged()",
				@"==================",
				@"",
				@"==================",
				@"WARNING: ThreadSanitizer: lock-order-inversion (potential deadlock) (pid=45069)",
				@"  Write of size 1 at 0x0000408ba588 by thread T15:",
				@"    #0 FPaths::IsStaged() /mnt/horde/++UE5/Sync/Engine/Source/./Runtime/Core/Private/Misc/Paths.cpp:164:33 (CitySampleEditor+0x2c2561a3) (BuildId: 7ca67a0f97321f67)",
				@"    #1 FGenericPlatformMisc::ProjectDir() /mnt/horde/++UE5/Sync/Engine/Source/./Runtime/Core/Private/GenericPlatform/GenericPlatformMisc.cpp:1264:19 (CitySampleEditor+0x2be6a866) (BuildId: 7ca67a0f97321f67)",
				@"",
				@"  Previous write of size 1 at 0x0000408ba588 by thread T13:",
				@"    #0 FPaths::IsStaged() /mnt/horde/++UE5/Sync/Engine/Source/./Runtime/Core/Private/Misc/Paths.cpp:164:33 (CitySampleEditor+0x2c2561a3) (BuildId: 7ca67a0f97321f67)",
				@"",
				@"SUMMARY: ThreadSanitizer: lock-order-inversion (potential deadlock) /mnt/horde/++UE5/Sync/Engine/Source/./Runtime/Core/Private/Misc/Paths.cpp:172:33 in FPaths::IsStaged()",
				@"==================",
			};

			IJob job = CreateJob(_mainStreamId, 120, "Sanitizer Report for 'SomeJob'", _graph);
			await ParseEventsAsync(job, 0, 0, lines);
			await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

			IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
			Assert.AreEqual(2, issues.Count);

			IIssue issue1 = issues[0];
			Assert.AreEqual(issue1.Fingerprints.Count, 1);
			Assert.IsTrue(issue1.Fingerprints[0].Type.Contains("ThreadSanitizer", StringComparison.OrdinalIgnoreCase));
			Assert.IsTrue(issue1.Fingerprints[0].Type.Contains("lock-order-inversion", StringComparison.OrdinalIgnoreCase));
			Assert.IsTrue(issue1.Fingerprints[0].Type.Contains("Paths.cpp", StringComparison.OrdinalIgnoreCase));

			IIssue issue2 = issues[1];
			Assert.AreEqual(issue2.Fingerprints.Count, 1);
			Assert.IsTrue(issue2.Fingerprints[0].Type.Contains("ThreadSanitizer", StringComparison.OrdinalIgnoreCase));
			Assert.IsTrue(issue2.Fingerprints[0].Type.Contains("data race", StringComparison.OrdinalIgnoreCase));
			Assert.IsTrue(issue2.Fingerprints[0].Type.Contains("Paths.cpp", StringComparison.OrdinalIgnoreCase));
		}

		[TestMethod]
		public async Task AddressSanitizerDeduplicateSimilarIssuesTestAsync()
		{
			// We have three unique errors reported, but we key issues on the file since file lines change over time without fixes, so only two unique issues should be found
			string[] lines =
			{
				@"=================================================================",
				@"==30562==ERROR: AddressSanitizer: heap-use-after-free on address 0x617002aa8418 at pc 0x7f98a08bd090 bp 0x7ffc30203af0 sp 0x7ffc30203ae8",
				@"READ of size 8 at 0x617002aa8418 thread T0",
				@"    #0 0x7f98a08bd08f in UObjectBase::GetFName() const /mnt/horde/FNR+Main+Inc/Sync/Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectBase.h:166:10",
				@"",
				@"0x617002aa8418 is located 24 bytes inside of 712-byte region [0x617002aa8400,0x617002aa86c8)",
				@"freed by thread T0 here:",
				@"    #0 0x7f9a283e6b08 in __interceptor_free.part.5 /src/build/llvm-src/compiler-rt/lib/asan/asan_malloc_linux.cpp:52",
				@"",
				@"previously allocated by thread T0 here:",
				@"    #0 0x7f9a283e730d in posix_memalign /src/build/llvm-src/compiler-rt/lib/asan/asan_malloc_linux.cpp:145",
				@"",
				@"SUMMARY: AddressSanitizer: heap-use-after-free /mnt/horde/FNR+Main+Inc/Sync/Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectBase.h:166:10 in UObjectBase::GetFName() const",
				@"==30562==ABORTING",
				@"=================================================================",
				@"==30562==ERROR: AddressSanitizer: heap-use-after-free on address 0x617002aa8418 at pc 0x7f98a08bd090 bp 0x7ffc30203af0 sp 0x7ffc30203ae8",
				@"SUMMARY: AddressSanitizer: heap-use-after-free /mnt/horde/FNR+Main+Inc/Sync/Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectBase.h:166:10 in UObjectBase::GetFName() const",
				@"==30562==ABORTING",
				@"=================================================================",
				@"==30562==ERROR: AddressSanitizer: heap-use-after-free on address 0x617002aa8418 at pc 0x7f98a08bd090 bp 0x7ffc30203af0 sp 0x7ffc30203ae8",
				@"SUMMARY: AddressSanitizer: heap-use-after-free /mnt/horde/FNR+Main+Inc/Sync/Engine/Source/Runtime/CoreUObject/Public/UObject/SomeOtherFile.h:166:10 in UObjectBase::GetFName() const",
				@"==30562==ABORTING"
			};

			IJob job = CreateJob(_mainStreamId, 120, "Sanitizer Report for 'SomeJob'", _graph);
			await ParseEventsAsync(job, 0, 0, lines);
			await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

			IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
			Assert.AreEqual(2, issues.Count);

			IIssue issue1 = issues[0];
			Assert.AreEqual(issue1.Fingerprints.Count, 1);
			Assert.IsTrue(issue1.Fingerprints[0].Type.Contains("AddressSanitizer", StringComparison.OrdinalIgnoreCase));
			Assert.IsTrue(issue1.Fingerprints[0].Type.Contains("heap-use-after-free", StringComparison.OrdinalIgnoreCase));
			Assert.IsTrue(issue1.Fingerprints[0].Type.Contains("SomeOtherFile.h", StringComparison.OrdinalIgnoreCase));

			IIssue issue2 = issues[1];
			Assert.AreEqual(issue2.Fingerprints.Count, 1);
			Assert.IsTrue(issue2.Fingerprints[0].Type.Contains("AddressSanitizer", StringComparison.OrdinalIgnoreCase));
			Assert.IsTrue(issue2.Fingerprints[0].Type.Contains("heap-use-after-free", StringComparison.OrdinalIgnoreCase));
			Assert.IsTrue(issue2.Fingerprints[0].Type.Contains("UObjectBase.h", StringComparison.OrdinalIgnoreCase));
		}

		[TestMethod]
		public async Task AddressSanitizerEnsureUniqueIssuesForSameFileTestAsync()
		{
			// We have three unique errors reported, but we key issues on the file since file lines change over time without fixes, so only two unique issues should be found
			string[] lines =
			{
				@"=================================================================",
				@"==30562==ERROR: AddressSanitizer: heap-use-after-free on address 0x617002aa8418 at pc 0x7f98a08bd090 bp 0x7ffc30203af0 sp 0x7ffc30203ae8",
				@"READ of size 8 at 0x617002aa8418 thread T0",
				@"    #0 0x7f98a08bd08f in UObjectBase::GetFName() const /mnt/horde/FNR+Main+Inc/Sync/Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectBase.h:166:10",
				@"",
				@"0x617002aa8418 is located 24 bytes inside of 712-byte region [0x617002aa8400,0x617002aa86c8)",
				@"freed by thread T0 here:",
				@"    #0 0x7f9a283e6b08 in __interceptor_free.part.5 /src/build/llvm-src/compiler-rt/lib/asan/asan_malloc_linux.cpp:52",
				@"",
				@"previously allocated by thread T0 here:",
				@"    #0 0x7f9a283e730d in posix_memalign /src/build/llvm-src/compiler-rt/lib/asan/asan_malloc_linux.cpp:145",
				@"",
				@"SUMMARY: AddressSanitizer: heap-use-after-free /mnt/horde/FNR+Main+Inc/Sync/Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectBase.h:166:10 in UObjectBase::GetFName() const",
				@"==30562==ABORTING",
				@"=================================================================",
				@"==30562==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x617002aa8418 at pc 0x7f98a08bd090 bp 0x7ffc30203af0 sp 0x7ffc30203ae8",
				@"SUMMARY: AddressSanitizer: stack-buffer-overflow /mnt/horde/FNR+Main+Inc/Sync/Engine/Source/Runtime/CoreUObject/Public/UObject/UObjectBase.h:166:10 in UObjectBase::GetFName() const",
				@"==30562==ABORTING",
			};

			IJob job = CreateJob(_mainStreamId, 120, "Sanitizer Report for 'SomeJob'", _graph);
			await ParseEventsAsync(job, 0, 0, lines);
			await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

			IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
			Assert.AreEqual(2, issues.Count);

			IIssue issue1 = issues[0];
			Assert.AreEqual(issue1.Fingerprints.Count, 1);
			Assert.IsTrue(issue1.Fingerprints[0].Type.Contains("AddressSanitizer", StringComparison.OrdinalIgnoreCase));
			Assert.IsTrue(issue1.Fingerprints[0].Type.Contains("stack-buffer-overflow", StringComparison.OrdinalIgnoreCase));
			Assert.IsTrue(issue1.Fingerprints[0].Type.Contains("UObjectBase.h", StringComparison.OrdinalIgnoreCase));

			IIssue issue2 = issues[1];
			Assert.AreEqual(issue2.Fingerprints.Count, 1);
			Assert.IsTrue(issue2.Fingerprints[0].Type.Contains("AddressSanitizer", StringComparison.OrdinalIgnoreCase));
			Assert.IsTrue(issue2.Fingerprints[0].Type.Contains("heap-use-after-free", StringComparison.OrdinalIgnoreCase));
			Assert.IsTrue(issue2.Fingerprints[0].Type.Contains("UObjectBase.h", StringComparison.OrdinalIgnoreCase));
		}

		[TestMethod]
		public async Task AutoSdkWarningTestAsync()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);

				IReadOnlyList<IIssue> openIssues = await IssueCollection.FindIssuesAsync(resolved: false);
				Assert.AreEqual(0, openIssues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120 in AutoSDK file
			// Expected: Creates issue, identifies source file correctly
			{
				string[] lines =
				{
					FileReference.Combine(_autoSdkDir, @"HostWin64\GDK\200604\Microsoft GDK\200604\GRDK\ExtensionLibraries\Xbox.Game.Chat.2.Cpp.API\DesignTime\CommonConfiguration\Neutral\Include\GameChat2Impl.h").FullName + @"(90): warning C5043: 'xbox::services::game_chat_2::chat_manager::set_memory_callbacks': exception specification does not match previous declaration",
					FileReference.Combine(_autoSdkDir, @"HostWin64\GDK\200604\Microsoft GDK\200604\GRDK\ExtensionLibraries\Xbox.Game.Chat.2.Cpp.API\DesignTime\CommonConfiguration\Neutral\Include\GameChat2.h").FullName + @"(2083): note: see declaration of 'xbox::services::game_chat_2::chat_manager::set_memory_callbacks'",
				};

				IJob job = CreateJob(_mainStreamId, 120, "Test Build", _graph);
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Warnings);

				ILog? log = await LogCollection.GetAsync(job.Batches[0].Steps[0].LogId!.Value, CancellationToken.None);
				Assert.IsNotNull(log);
				List<ILogAnchor> anchors = await log.GetAnchorsAsync();
				Assert.AreEqual(1, anchors.Count);
				Assert.AreEqual(2, anchors[0].LineCount);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				IIssue issue = issues[0];
				IReadOnlyList<IIssueSpan> spans = await IssueCollection.FindSpansAsync(issue.Id);
				Assert.AreEqual(1, spans.Count);
				Assert.AreEqual(1, issue.Fingerprints.Count);
				Assert.AreEqual("Compile", issue.Fingerprints[0].Type);
				Assert.AreEqual("Compile warnings in GameChat2Impl.h", issue.Summary);
			}
		}

		[TestMethod]
		public async Task EnsureWarningTestAsync()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);

				IReadOnlyList<IIssue> openIssues = await IssueCollection.FindIssuesAsync(resolved: false);
				Assert.AreEqual(0, openIssues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120 in Gauntlet
			// Expected: Creates issue
			{
				IJob job = CreateJob(_mainStreamId, 120, "Test Build", _graph);
				await AddEventAsync(job, 0, 0, LogLevel.Warning, id: KnownLogEvents.Gauntlet_TestEvent.Id);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Warnings);

				ILog? log = await LogCollection.GetAsync(job.Batches[0].Steps[0].LogId!.Value, CancellationToken.None);
				Assert.IsNotNull(log);

				List<ILogAnchor> anchors = await log.GetAnchorsAsync();
				Assert.AreEqual(1, anchors.Count);
				Assert.AreEqual(1, anchors[0].LineCount);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
			}
		}

		[TestMethod]
		public async Task GenericErrorTestAsync()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);

				IReadOnlyList<IIssue> openIssues = await IssueCollection.FindIssuesAsync(resolved: false);
				Assert.AreEqual(0, openIssues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120
			// Expected: Creates issue, blames submitters at CL 110, 115, 120
			{
				string[] lines =
				{
					FileReference.Combine(_workspaceDir, "fog.cpp").FullName + @"(78): error C2664: 'FDelegateHandle TBaseMulticastDelegate&lt;void,FChaosScene *&gt;::AddUObject&lt;AFortVehicleManager,&gt;(const UserClass *,void (__cdecl AFortVehicleManager::* )(FChaosScene *) const)': cannot convert argument 2 from 'void (__cdecl AFortVehicleManager::* )(FPhysScene *)' to 'void (__cdecl AFortVehicleManager::* )(FChaosScene *)'",
				};

				IJob job = CreateJob(_mainStreamId, 120, "Test Build", _graph);
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				IIssue issue = issues[0];
				IReadOnlyList<IIssueSpan> spans = await IssueCollection.FindSpansAsync(issue.Id);
				Assert.AreEqual(1, spans.Count);
				Assert.AreEqual(1, issue.Fingerprints.Count);
				Assert.AreEqual("Compile", issue.Fingerprints[0].Type);
				Assert.AreEqual("Compile errors in fog.cpp", issue.Summary);

				IIssueSpan stream = spans[0];
				Assert.AreEqual(105, stream.LastSuccess?.CommitId.GetPerforceChange());
				Assert.AreEqual(null, stream.NextSuccess?.CommitId);

				IReadOnlyList<IIssueSuspect> suspects = await IssueCollection.FindSuspectsAsync(issue);
				suspects = suspects.OrderBy(x => x.Id).ToList();
				Assert.AreEqual(3, suspects.Count);

				Assert.AreEqual(75 /*115*/, suspects[1].CommitId.GetPerforceChange());
				Assert.AreEqual(_jerryId, suspects[1].AuthorId);
				//				Assert.AreEqual(75, Suspects[1].OriginatingChange);

				Assert.AreEqual(110, suspects[2].CommitId.GetPerforceChange());
				Assert.AreEqual(_bobId, suspects[2].AuthorId);
				//			Assert.AreEqual(null, Suspects[2].OriginatingChange);

				Assert.AreEqual(120, suspects[0].CommitId.GetPerforceChange());
				Assert.AreEqual(_timId, suspects[0].AuthorId);
				//				Assert.AreEqual(null, Suspects[0].OriginatingChange);

				IReadOnlyList<IIssue> openIssues = await IssueCollection.FindIssuesAsync(resolved: false);
				Assert.AreEqual(1, openIssues.Count);
			}

			// #3
			// Scenario: Job step succeeds at CL 110
			// Expected: Issue is updated to vindicate change at CL 110
			{
				IJob job = CreateJob(_mainStreamId, 110, "Test Build", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				IIssue issue = issues[0];
				IReadOnlyList<IIssueSpan> spans = await IssueCollection.FindSpansAsync(issue.Id);
				Assert.AreEqual(spans.Count, 1);

				IIssueSpan stream = spans[0];
				Assert.AreEqual(110, stream.LastSuccess?.CommitId.GetPerforceChange());
				Assert.AreEqual(null, stream.NextSuccess?.CommitId);

				IReadOnlyList<IIssueSuspect> suspects = (await IssueCollection.FindSuspectsAsync(issue)).OrderByDescending(x => x.CommitId).ToList();
				Assert.AreEqual(2, suspects.Count);

				Assert.AreEqual(120, suspects[0].CommitId.GetPerforceChange());
				Assert.AreEqual(_timId, suspects[0].AuthorId);

				Assert.AreEqual(75, suspects[1].CommitId.GetPerforceChange());
				Assert.AreEqual(_jerryId, suspects[1].AuthorId);

				IReadOnlyList<IIssue> openIssues = await IssueCollection.FindIssuesAsync(resolved: false);
				Assert.AreEqual(1, openIssues.Count);
			}

			// #4
			// Scenario: Job step succeeds at CL 125
			// Expected: Issue is updated to narrow range to 115, 120
			{
				IJob job = CreateJob(_mainStreamId, 125, "Test Build", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync(resolved: true);
				Assert.AreEqual(1, issues.Count);

				IIssue issue = issues[0];
				IReadOnlyList<IIssueSpan> spans = await IssueCollection.FindSpansAsync(issue.Id);
				Assert.AreEqual(spans.Count, 1);

				IIssueSpan stream = spans[0];
				Assert.AreEqual(110, stream.LastSuccess?.CommitId.GetPerforceChange());
				Assert.AreEqual(125, stream.NextSuccess?.CommitId.GetPerforceChange());

				IReadOnlyList<IIssue> openIssues = await IssueCollection.FindIssuesAsync(resolved: false);
				Assert.AreEqual(0, openIssues.Count);
			}

			// #5
			// Scenario: Additional error in same node at 115
			// Expected: Event is merged into existing issue
			{
				string[] lines =
				{
					FileReference.Combine(_workspaceDir, "fog.cpp").FullName + @"(114): error C2664: 'FDelegateHandle TBaseMulticastDelegate&lt;void,FChaosScene *&gt;::AddUObject&lt;AFortVehicleManager,&gt;(const UserClass *,void (__cdecl AFortVehicleManager::* )(FChaosScene *) const)': cannot convert argument 2 from 'void (__cdecl AFortVehicleManager::* )(FPhysScene *)' to 'void (__cdecl AFortVehicleManager::* )(FChaosScene *)'",
				};

				IJob job = CreateJob(_mainStreamId, 115, "Test Build", _graph);
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync(resolved: true);
				Assert.AreEqual(1, issues.Count);

				IIssue issue = issues[0];
				IReadOnlyList<IIssueSpan> spans = await IssueCollection.FindSpansAsync(issue.Id);
				Assert.AreEqual(1, spans.Count);

				IIssueSpan span = spans[0];
				Assert.AreEqual(110, span.LastSuccess?.CommitId.GetPerforceChange());
				Assert.AreEqual(125, span.NextSuccess?.CommitId.GetPerforceChange());

				IReadOnlyList<IIssueStep> steps = await IssueCollection.FindStepsAsync(span.Id);
				Assert.AreEqual(2, steps.Count);
			}

			// #5
			// Scenario: Additional error in different node at 115
			// Expected: New issue is created
			{
				IJob job = CreateJob(_mainStreamId, 115, "Test Build", _graph);
				await AddEventAsync(job, 0, 1, LogLevel.Error);
				await UpdateCompleteStepAsync(job, 0, 1, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> resolvedIssues = await IssueCollection.FindIssuesAsync(resolved: true);
				Assert.AreEqual(1, resolvedIssues.Count);

				IReadOnlyList<IIssue> unresolvedIssues = await IssueCollection.FindIssuesAsync(resolved: false);
				Assert.AreEqual(1, unresolvedIssues.Count);
			}
		}

		[TestMethod]
		public async Task DefaultOwnerTestAsync()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Compile Test", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120
			// Expected: Creates issue, blames submitters at CL 110, 115, 120
			{
				string[] lines =
				{
					FileReference.Combine(_workspaceDir, "foo.cpp").FullName + @"(78): error C2664: 'FDelegateHandle TBaseMulticastDelegate&lt;void,FChaosScene *&gt;::AddUObject&lt;AFortVehicleManager,&gt;(const UserClass *,void (__cdecl AFortVehicleManager::* )(FChaosScene *) const)': cannot convert argument 2 from 'void (__cdecl AFortVehicleManager::* )(FPhysScene *)' to 'void (__cdecl AFortVehicleManager::* )(FChaosScene *)'",
				};

				_perforce.Changes[_mainStreamId][110].Files.Add("/Engine/Source/Boo.cpp");
				_perforce.Changes[_mainStreamId][115].Files.Add("/Engine/Source/Foo.cpp");
				_perforce.Changes[_mainStreamId][120].Files.Add("/Engine/Source/Foo.cpp");

				IJob job = CreateJob(_mainStreamId, 120, "Compile Test", _graph);
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.IsTrue(issues[0].Promoted);

				IReadOnlyList<IIssueSuspect> suspects = await IssueCollection.FindSuspectsAsync(issues[0]);

				List<UserId> primarySuspects = suspects.Select(x => x.AuthorId).ToList();
				Assert.AreEqual(2, primarySuspects.Count);
				Assert.IsTrue(primarySuspects.Contains(_jerryId)); // 115
				Assert.IsTrue(primarySuspects.Contains(_timId)); // 120
			}

			// #3
			// Scenario: Job step succeeds at CL 115
			// Expected: Creates issue, blames submitter at CL 120
			{
				IJob job = CreateJob(_mainStreamId, 115, "Compile Test", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				IIssue issue = issues[0];
				Assert.AreEqual(_timId, issue.OwnerId);

				// Also check updating an issue doesn't clear the owner
				Assert.IsTrue(await IssueService.UpdateIssueAsync(issue.Id));
				Assert.AreEqual(_timId, issue!.OwnerId);
			}
		}

		[TestMethod]
		public async Task ManualPromotionTestAsync()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Compile Test", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120
			// Expected: Creates issue, blames submitters at CL 110, 115, 120
			{
				string[] lines =
				{
					FileReference.Combine(_workspaceDir, "foo.cpp").FullName + @"(78): error C2664: 'FDelegateHandle TBaseMulticastDelegate&lt;void,FChaosScene *&gt;::AddUObject&lt;AFortVehicleManager,&gt;(const UserClass *,void (__cdecl AFortVehicleManager::* )(FChaosScene *) const)': cannot convert argument 2 from 'void (__cdecl AFortVehicleManager::* )(FPhysScene *)' to 'void (__cdecl AFortVehicleManager::* )(FChaosScene *)'",
				};

				_perforce.Changes[_mainStreamId][110].Files.Add("/Engine/Source/Boo.cpp");
				_perforce.Changes[_mainStreamId][115].Files.Add("/Engine/Source/Foo.cpp");
				_perforce.Changes[_mainStreamId][120].Files.Add("/Engine/Source/Foo.cpp");

				IJob job = CreateJob(_mainStreamId, 120, "Compile Test", _graph, promoteByDefault: false);
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.IsFalse(issues[0].Promoted);

				await IssueService.UpdateIssueAsync(issues[0].Id, promoted: true);

				issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.IsTrue(issues[0].Promoted);
			}
		}

		[TestMethod]
		public async Task DefaultPromotionTestAsync()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Compile Test", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120
			// Expected: Issue is promoted
			{
				string[] lines =
				{
					FileReference.Combine(_workspaceDir, "foo.cpp").FullName + @"(78): error C2664: 'FDelegateHandle TBaseMulticastDelegate&lt;void,FChaosScene *&gt;::AddUObject&lt;AFortVehicleManager,&gt;(const UserClass *,void (__cdecl AFortVehicleManager::* )(FChaosScene *) const)': cannot convert argument 2 from 'void (__cdecl AFortVehicleManager::* )(FPhysScene *)' to 'void (__cdecl AFortVehicleManager::* )(FChaosScene *)'",
				};

				_perforce.Changes[_mainStreamId][110].Files.Add("/Engine/Source/Boo.cpp");
				_perforce.Changes[_mainStreamId][115].Files.Add("/Engine/Source/Foo.cpp");
				_perforce.Changes[_mainStreamId][120].Files.Add("/Engine/Source/Foo.cpp");

				IJob job = CreateJob(_mainStreamId, 120, "Compile Test", _graph, promoteByDefault: true);
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.IsTrue(issues[0].Promoted);

			}
		}

		[TestMethod]
		public async Task CallstackTestAsync()
		{
			string[] lines1 =
			{
				@"LogWindows: Error: begin: stack for UAT",
				@"LogWindows: Error: === Critical error: ===",
				@"LogWindows: Error:",
				@"LogWindows: Error: Fatal error!",
				@"LogWindows: Error:",
				@"LogWindows: Error: Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x0000000000000070",
				@"LogWindows: Error:",
				@"LogWindows: Error: [Callstack] 0x00007ffdaea6afc8 UnrealEditor-Landscape.dll!ALandscape::ALandscape() []",
				@"LogWindows: Error: [Callstack] 0x00007ffdc005d375 UnrealEditor-CoreUObject.dll!StaticConstructObject_Internal() []",
				@"LogWindows: Error: [Callstack] 0x00007ffdbfe7f2af UnrealEditor-CoreUObject.dll!FLinkerLoad::CreateExport() []",
				@"LogWindows: Error: [Callstack] 0x00007ffdbfe7fb7b UnrealEditor-CoreUObject.dll!FLinkerLoad::CreateExportAndPreload() []",
				@"LogWindows: Error: [Callstack] 0x00007ffdbfea9141 UnrealEditor-CoreUObject.dll!FLinkerLoad::LoadAllObjects() []",
				@"LogWindows: Error:",
				@"LogWindows: Error: end: stack for UAT",
				@"Took 70.6962389s to run UnrealEditor-Cmd.exe, ExitCode=3",
				@"Copying crash data to d:\build\U5M+Inc\Sync\Engine\Programs\AutomationTool\Saved\Logs\Crashes\UECC-Windows-D7C3D5AD4E079F5DF8FD00B69907CD38_0000...",
				@"Editor terminated with exit code 3 while running Cook for D:\build\U5M+Inc\Sync\Samples\Games\Lyra\Lyra.uproject; see log d:\build\U5M+Inc\Sync\Engine\Programs\AutomationTool\Saved\Logs\Cook-2022.08.19-17.34.05.txt",
			};

			IJob job1 = CreateJob(_mainStreamId, 120, "Compile Test", _graph);
			await ParseEventsAsync(job1, 0, 0, lines1);
			await UpdateCompleteStepAsync(job1, 0, 0, JobStepOutcome.Failure);

			IReadOnlyList<IIssue> issues1 = await IssueCollection.FindIssuesAsync();
			Assert.AreEqual(1, issues1.Count);

			IIssue issue1 = issues1[0];
			Assert.AreEqual(issue1.Fingerprints.Count, 1);
			Assert.AreEqual(issue1.Fingerprints[0].Type, "Hashed");

			// SAME ERROR BUT DIFFERENT CALLSTACK ADDRESSES

			string[] lines2 =
			{
				@"LogWindows: Error: begin: stack for UAT",
				@"LogWindows: Error: === Critical error: ===",
				@"LogWindows: Error:",
				@"LogWindows: Error: Fatal error!",
				@"LogWindows: Error:",
				@"LogWindows: Error: Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x0000000000000070",
				@"LogWindows: Error:",
				@"LogWindows: Error: [Callstack] 0x00007ff95973afc8 UnrealEditor-Landscape.dll!ALandscape::ALandscape() []",
				@"LogWindows: Error: [Callstack] 0x00007ff963ced375 UnrealEditor-CoreUObject.dll!StaticConstructObject_Internal() []",
				@"LogWindows: Error: [Callstack] 0x00007ff963b0f2af UnrealEditor-CoreUObject.dll!FLinkerLoad::CreateExport() []",
				@"LogWindows: Error: [Callstack] 0x00007ff963b0fb7b UnrealEditor-CoreUObject.dll!FLinkerLoad::CreateExportAndPreload() []",
				@"LogWindows: Error: [Callstack] 0x00007ff963b39141 UnrealEditor-CoreUObject.dll!FLinkerLoad::LoadAllObjects() []",
				@"LogWindows: Error:",
				@"LogWindows: Error: end: stack for UAT",
				@"Took 67.0103214s to run UnrealEditor-Cmd.exe, ExitCode=3",
				@"Copying crash data to d:\build\U5M+Inc\Sync\Engine\Programs\AutomationTool\Saved\Logs\Crashes\UECC-Windows-5F2FFCFE4EAAFE5DDD3E0EAB04336EA0_0000...",
				@"Editor terminated with exit code 3 while running Cook for D:\build\U5M+Inc\Sync\Samples\Games\Lyra\Lyra.uproject; see log d:\build\U5M+Inc\Sync\Engine\Programs\AutomationTool\Saved\Logs\Cook-2022.08.19-16.53.28.txt",
			};

			IJob job2 = CreateJob(_mainStreamId, 120, "Compile Test", _graph);
			await ParseEventsAsync(job2, 0, 0, lines2);
			await UpdateCompleteStepAsync(job2, 0, 0, JobStepOutcome.Failure);

			IReadOnlyList<IIssue> issues2 = await IssueCollection.FindIssuesAsync();
			Assert.AreEqual(1, issues2.Count);
			Assert.AreEqual(issues2[0].Id, issues1[0].Id);
		}

		[TestMethod]
		public async Task CompileTypeTestAsync()
		{
			string[] lines =
			{
				FileReference.Combine(_workspaceDir, "FOO.CPP").FullName + @"(170) : warning C6011: Dereferencing NULL pointer 'CurrentProperty'. : Lines: 159, 162, 163, 169, 170, 174, 176, 159, 162, 163, 169, 170",
				FileReference.Combine(_workspaceDir, "foo.cpp").FullName + @"(170) : warning C6011: Dereferencing NULL pointer 'CurrentProperty'. : Lines: 159, 162, 163, 169, 170, 174, 176, 159, 162, 163, 169, 170"
			};

			IJob job = CreateJob(_mainStreamId, 120, "Compile Test", _graph);
			await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);
			await UpdateCompleteStepAsync(job, 0, 1, JobStepOutcome.Success);
			await UpdateCompleteStepAsync(job, 0, 2, JobStepOutcome.Success);
			await UpdateCompleteStepAsync(job, 0, 3, JobStepOutcome.Success);
			await ParseEventsAsync(job, 0, 4, lines);
			await UpdateCompleteStepAsync(job, 0, 4, JobStepOutcome.Failure);

			IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
			Assert.AreEqual(1, issues.Count);

			IIssue issue = issues[0];
			Assert.AreEqual("Static analysis warnings in FOO.CPP", issue.Summary);
		}

		[TestMethod]
		public async Task CompileIssueTestAsync()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Compile Test", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120
			// Expected: Creates issue, blames submitters at CL 110, 115, 120
			{
				string[] lines =
				{
					FileReference.Combine(_workspaceDir, "FOO.CPP").FullName + @"(170) : warning C6011: Dereferencing NULL pointer 'CurrentProperty'. : Lines: 159, 162, 163, 169, 170, 174, 176, 159, 162, 163, 169, 170",
					FileReference.Combine(_workspaceDir, "foo.cpp").FullName + @"(170) : warning C6011: Dereferencing NULL pointer 'CurrentProperty'. : Lines: 159, 162, 163, 169, 170, 174, 176, 159, 162, 163, 169, 170"
				};

				IJob job = CreateJob(_mainStreamId, 120, "Compile Test", _graph);
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				IIssue issue = issues[0];
				Assert.AreEqual(1, issue.Fingerprints.Count);

				IIssueFingerprint fingerprint = issue.Fingerprints[0];
				Assert.AreEqual("Compile", fingerprint.Type);
				Assert.AreEqual(1, fingerprint.Keys.Count);
				Assert.AreEqual(new IssueKey("FOO.CPP", IssueKeyType.File), fingerprint.Keys.First());
			}
		}

		[TestMethod]
		public async Task MaskedEventTestAsync()
		{
			string[] lines =
			{
				FileReference.Combine(_workspaceDir, "foo.cpp").FullName + @"(78): error C2664: 'FDelegateHandle TBaseMulticastDelegate&lt;void,FChaosScene *&gt;::AddUObject&lt;AFortVehicleManager,&gt;(const UserClass *,void (__cdecl AFortVehicleManager::* )(FChaosScene *) const)': cannot convert argument 2 from 'void (__cdecl AFortVehicleManager::* )(FPhysScene *)' to 'void (__cdecl AFortVehicleManager::* )(FChaosScene *)'",
				"Error executing d:\\build\\AutoSDK\\Sync\\HostWin64\\Win64\\VS2019\\14.29.30145\\bin\\HostX64\\x64\\cl.exe (tool returned code: 2)",
				"BUILD FAILED: Command failed (Result:1): C:\\Program Files (x86)\\IncrediBuild\\xgConsole.exe \"d:\\build\\++UE5\\Sync\\Engine\\Programs\\AutomationTool\\Saved\\Logs\\UAT_XGE.xml\" /Rebuild /NoLogo /ShowAgent /ShowTime /no_watchdog_thread. See logfile for details: 'xgConsole-2022.06.09-15.14.03.txt'",
				"BUILD FAILED",
			};

			IJob job = CreateJob(_mainStreamId, 120, "Compile Test", _graph);
			await ParseEventsAsync(job, 0, 0, lines);
			await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

			IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
			Assert.AreEqual(1, issues.Count);

			IIssueFingerprint fingerprint = issues[0].Fingerprints[0];
			Assert.AreEqual("Compile", fingerprint.Type);
			Assert.AreEqual(1, fingerprint.Keys.Count);
			Assert.AreEqual(new IssueKey("foo.cpp", IssueKeyType.File), fingerprint.Keys.First());
		}

		[TestMethod]
		public async Task DeprecationTestAsync()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Compile Test", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120
			// Expected: Creates issue, blames submitter at CL 115 that introduced deprecation message
			{
				string[] lines =
				{
					FileReference.Combine(_workspaceDir, "Consumer.h").FullName + @"(22): warning C4996: 'USimpleWheeledVehicleMovementComponent': PhysX is deprecated.Use the UChaosWheeledVehicleMovementComponent from the ChaosVehiclePhysics Plugin.Please update your code to the new API before upgrading to the next release, otherwise your project will no longer compile.",
					FileReference.Combine(_workspaceDir, "Deprecater.h").FullName + @"(16): note: see declaration of 'USimpleWheeledVehicleMovementComponent'"
				};

				_perforce.Changes[_mainStreamId][110].Files.Add("/Engine/Source/Boo.cpp");
				_perforce.Changes[_mainStreamId][115].Files.Add("/Engine/Source/Deprecater.h");
				_perforce.Changes[_mainStreamId][120].Files.Add("/Engine/Source/Foo.cpp");

				IJob job = CreateJob(_mainStreamId, 120, "Compile Test", _graph);
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				IReadOnlyList<IIssueSuspect> suspects = await IssueCollection.FindSuspectsAsync(issues[0]);

				List<UserId> primarySuspects = suspects.Select(x => x.AuthorId).ToList();
				Assert.AreEqual(1, primarySuspects.Count);
				Assert.AreEqual(_jerryId, primarySuspects[0]); // 115
			}
		}

		[TestMethod]
		public async Task DeclineIssueTestAsync()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Compile Test", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120
			// Expected: Creates issue, blames submitters at CL 110, 115, 120
			{
				string[] lines =
				{
					FileReference.Combine(_workspaceDir, "foo.cpp").FullName + @"(78): error C2664: 'FDelegateHandle TBaseMulticastDelegate&lt;void,FChaosScene *&gt;::AddUObject&lt;AFortVehicleManager,&gt;(const UserClass *,void (__cdecl AFortVehicleManager::* )(FChaosScene *) const)': cannot convert argument 2 from 'void (__cdecl AFortVehicleManager::* )(FPhysScene *)' to 'void (__cdecl AFortVehicleManager::* )(FChaosScene *)'",
				};

				_perforce.Changes[_mainStreamId][110].Files.Add("/Engine/Source/Boo.cpp");
				_perforce.Changes[_mainStreamId][115].Files.Add("/Engine/Source/Foo.cpp");
				_perforce.Changes[_mainStreamId][120].Files.Add("/Engine/Source/Foo.cpp");

				IJob job = CreateJob(_mainStreamId, 120, "Compile Test", _graph);
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				IReadOnlyList<IIssueSuspect> suspects = await IssueCollection.FindSuspectsAsync(issues[0]);

				List<UserId> primarySuspects = suspects.Select(x => x.AuthorId).ToList();
				Assert.AreEqual(2, primarySuspects.Count);
				Assert.IsTrue(primarySuspects.Contains(_jerryId)); // 115
				Assert.IsTrue(primarySuspects.Contains(_timId)); // 120
			}

			// #3
			// Scenario: Tim declines the issue
			// Expected: Only suspect is Jerry, but owner is still unassigned
			{
				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				await IssueService.UpdateIssueAsync(issues[0].Id, declinedById: _timId);

				issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				IReadOnlyList<IIssueSuspect> suspects = await IssueCollection.FindSuspectsAsync(issues[0]);

				List<UserId> primarySuspects = suspects.Where(x => x.DeclinedAt == null).Select(x => x.AuthorId).ToList();
				Assert.AreEqual(1, primarySuspects.Count);
				Assert.AreEqual(_jerryId, primarySuspects[0]); // 115
			}
		}

		[TestMethod]
		public async Task ContentIssueTestAsync()
		{
			IJob job1 = CreateJob(_mainStreamId, 120, "Cook Test", _graph);
			await ParseEventsAsync(job1, 0, 0, new[]
			{
				// Note: using relative paths here, which can't be mapped to depot paths
				@"LogBlueprint: Warning: [AssetLog] ..\..\..\QAGame\Plugins\NiagaraFluids\Content\Blueprints\Phsyarum_BP.uasset: [Compiler] Fill Texture 2D : Usage of 'Fill Texture 2D' has been deprecated. This function has been replaced by object user variables on the emitter to specify render targets to fill with data."
			});
			await UpdateCompleteStepAsync(job1, 0, 0, JobStepOutcome.Failure);

			IReadOnlyList<IIssue> issues1 = await IssueCollection.FindIssuesAsync();
			Assert.AreEqual(1, issues1.Count);
			Assert.AreEqual("Warnings in Phsyarum_BP.uasset", issues1[0].Summary);

			IJob job2 = CreateJob(_mainStreamId, 125, "Cook Test", _graph);
			await ParseEventsAsync(job2, 0, 0, new[]
			{
				// Add a new warning; should create a new issue
				@"LogBlueprint: Warning: [AssetLog] ..\..\..\QAGame\Plugins\NiagaraFluids\Content\Blueprints\Phsyarum_BP.uasset: [Compiler] Fill Texture 2D : Usage of 'Fill Texture 2D' has been deprecated. This function has been replaced by object user variables on the emitter to specify render targets to fill with data.",
				@"LogBlueprint: Warning: [AssetLog] ..\..\..\QAGame\Plugins\NiagaraFluids\Content\Blueprints\Phsyarum_BP2.uasset: [Compiler] Fill Texture 2D : Usage of 'Fill Texture 2D' has been deprecated. This function has been replaced by object user variables on the emitter to specify render targets to fill with data.",
			});
			await UpdateCompleteStepAsync(job2, 0, 0, JobStepOutcome.Failure);

			IReadOnlyList<IIssue> issues2 = await IssueCollection.FindIssuesAsync();
			issues2 = issues2.OrderBy(x => x.Id).ToList();
			Assert.AreEqual(2, issues2.Count);
			Assert.AreEqual("Warnings in Phsyarum_BP.uasset", issues2[0].Summary);
			Assert.AreEqual("Warnings in Phsyarum_BP2.uasset", issues2[1].Summary);
		}

		[TestMethod]
		public async Task ExternalIssueTestAsync()
		{
			IJob job = CreateJob(_mainStreamId, 120, "Compile Test", _graph);

			await WriteEventsAsync(job, 0, 0, logger =>
			{
				logger.LogInformation("Foo");

				IssueFingerprint fingerprint = new IssueFingerprint("NewIssueType", "This is an issue with severity: {Severity}");
				fingerprint.Keys.Add(IssueKey.FromFile("foo.cpp"));

				using (IDisposable? scope = logger.BeginIssueScope(fingerprint))
				{
					logger.LogWarning("This is a warning!");
					logger.LogError("This is an error!");
				}

				logger.LogInformation("Bar");
			});
			await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

			IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
			Assert.AreEqual(1, issues.Count);

			IIssue issue = issues[0];
			Assert.AreEqual("This is an issue with severity: errors", issue.Summary);
			Assert.AreEqual(1, issue.Fingerprints.Count);
			Assert.AreEqual(1, issue.Fingerprints[0].Keys.Count);
			Assert.AreEqual(IssueKey.FromFile("foo.cpp"), issue.Fingerprints[0].Keys.First());
		}

		[TestMethod]
		public async Task HashedIssueTestAsync()
		{
			IJob job = CreateJob(_mainStreamId, 120, "Compile Test", _graph);

			await ParseEventsAsync(job, 0, 0, new[] { "Warning: This is a warning from the editor" });
			await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

			await ParseEventsAsync(job, 0, 1, new[] { "Warning: This is a warning from the editor" });
			await UpdateCompleteStepAsync(job, 0, 1, JobStepOutcome.Failure);

			IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
			Assert.AreEqual(1, issues.Count);

			IIssue issue = issues[0];
			Assert.AreEqual("Warnings in Update Version Files and Compile UnrealHeaderTool Win64", issue.Summary);
		}

		[TestMethod]
		public async Task HashedIssueTest2Async()
		{
			IJob job = CreateJob(_mainStreamId, 120, "Compile Test", _graph);

			await ParseEventsAsync(job, 0, 0, new[] { "Warning: This is a warning from the editor" });
			await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

			await ParseEventsAsync(job, 0, 1, new[] { "Warning: This is a warning from the editor2" });
			await UpdateCompleteStepAsync(job, 0, 1, JobStepOutcome.Failure);

			IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
			issues = issues.OrderBy(x => x.Id).ToList();
			Assert.AreEqual(2, issues.Count);

			Assert.AreEqual("Warnings in Update Version Files", issues[0].Summary);
			Assert.AreEqual("Warnings in Compile UnrealHeaderTool Win64", issues[1].Summary);
		}

		[TestMethod]
		public async Task HashedIssueTest3Async()
		{
			IJob job = CreateJob(_mainStreamId, 120, "Compile Test", _graph);

			await ParseEventsAsync(job, 0, 0, new[] { "Assertion failed: 1 == 2 [File:D:\\build\\++UE5\\Sync\\Engine\\Source\\Runtime\\Core\\Tests\\Misc\\AssertionMacrosTest.cpp] [Line: 119]" });
			await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

			await ParseEventsAsync(job, 0, 1, new[] { "Assertion failed: 1 == 2 [File:C:\\build\\++UE5+Inc\\Sync\\Engine\\Source\\Runtime\\Core\\Tests\\Misc\\AssertionMacrosTest.cpp] [Line: 119]" });
			await UpdateCompleteStepAsync(job, 0, 1, JobStepOutcome.Failure);

			IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
			Assert.AreEqual(1, issues.Count);

			IIssue issue = issues[0];
			Assert.AreEqual("Errors in Update Version Files and Compile UnrealHeaderTool Win64", issue.Summary);
		}

		[TestMethod]
		public async Task ScopedIssueTest1Async()
		{
			IJob job = CreateJob(_mainStreamId, 120, "Compile Test", _graph);

			await ParseEventsAsync(job, 0, 0, new[] { "LogSomething: Warning: This is a warning from the editor", "warning: some generic thing that will use fallback issue matcher" });
			await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

			await ParseEventsAsync(job, 0, 1, new[] { "LogSomething: Warning: This is a warning from the editor" });
			await UpdateCompleteStepAsync(job, 0, 1, JobStepOutcome.Failure);

			IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
			Assert.AreEqual(2, issues.Count);

			Assert.AreEqual("Hashed", issues[0].Fingerprints[0].Type);
			Assert.AreEqual("Warnings in Update Version Files", issues[0].Summary);
			Assert.AreEqual("Scoped:LogSomething", issues[1].Fingerprints[0].Type);
			Assert.AreEqual("Warnings in Update Version Files and Compile UnrealHeaderTool Win64 - LogSomething", issues[1].Summary);
		}

		[TestMethod]
		public async Task ScopedIssueTest2Async()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120 on different platforms
			// Expected: Creates single issue
			{
				IJob job;
				IReadOnlyList<IIssue> issues;

				string[] lines1 =
				{
					@"LogAnalytics: Warning: EventCache either took too long to flush (0.005 ms) or had a very large payload (111.788 KB, 1 events). Listing events in the payload for investigation:",
					@"LogAnalytics: Warning:     Editor.AssetRegistry.SynchronousScan,114458"
				};

				string[] lines2 =
				{
					@"LogAnalytics: Warning: EventCache either took too long to flush (0.005 ms) or had a very large payload (111.788 KB, 1 events). Listing events in the payload for investigation:",
					@"LogAnalytics: Warning:     Editor.AssetRegistry.SynchronousScan,114458",
					@"Some other log",
					@"LogSavePackage: Warning: /ChallengeSystem/Data/ChallengePool_Default imported Serialize:/ChallengeSystem/Data/Quests/Quest_Challenge_Resource_T1.Quest_Challenge_Resource_T1, but it was never saved as an export.",
					@"LogSavePackage: Warning: /ChallengeSystem/Data/ChallengePool_Default imported Serialize:/ChallengeSystem/Data/Quests/Quest__Challenge_Resource_T1_S, but it was never saved as an export.",
					@"Some other log",
					@"warning: some generic thing that will use fallback issue matcher",
					@"Some other log",
					@"LogSavePackage: Warning: /ChallengeSystem/Data/ChallengePool_Default imported Serialize:/ChallengeSystem/Data/Quests/Quest__Challenge_Resource_T1_M.Quest__Challenge_Resource_T1_M, but it was never saved as an export.",
					@"LogSavePackage: Warning: /ChallengeSystem/Data/ChallengePool_Default imported Serialize:/ChallengeSystem/Data/Quests/Quest__Challenge_Resource_T1_M, but it was never saved as an export."
				};

				string[] lines3 =
				{
					@"LogSavePackage: Warning: /ChallengeSystem/Data/ChallengePool_Default imported Serialize:/ChallengeSystem/Data/Quests/Quest_Challenge_Resource_T1.Quest_Challenge_Resource_T1, but it was never saved as an export.",
					@"LogSavePackage: Warning: /ChallengeSystem/Data/ChallengePool_Default imported Serialize:/ChallengeSystem/Data/Quests/Quest__Challenge_Resource_T1_S, but it was never saved as an export.",
					@"Some other log",
					@"LogUObjectLinker: Error: [CookWorker 0]: Detaching from existing linker /Game/Characters/Player/Male/Large/Bodies/M_LRG_Person_Mashup/Meshes/MLD/M_LRG_Person while object /Game/Characters/Player/Male/Large/Bodies/M_LRG_Person_Mashup/Meshes/MLD/M_LRG_Person.M_LRG_Person needs loading. Setting linker to nullptr.",
					@"LogSkeletalMesh: Display: [CookWorker 0]: Waiting for skinned assets to be ready 0/1 (M_LRG_Person) ...",
					@"LogUObjectLinker: Error: [CookWorker 0]: Detaching from existing linker /Game/Characters/Player/Male/Large/Bodies/M_LRG_Person/Meshes/MLD/M_LRG_Person_MLD while object /Game/Characters/Player/Male/Large/Bodies/M_LRG_Person/Meshes/MLD/M_LRG_Person_MLD.M_LRG_Person_MLD needs loading. Setting linker to nullptr."
				};

				int[] commits = { 105, 120, 125 };
				for (int i = 0; i < 3; i++)
				{

					job = CreateJob(_mainStreamId, commits[i], "Test Build", _graph);

					await ParseEventsAsync(job, 0, 0, lines1);
					await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Warnings);

					await ParseEventsAsync(job, 0, 1, lines2);
					await UpdateCompleteStepAsync(job, 0, 1, JobStepOutcome.Warnings);

					await ParseEventsAsync(job, 0, 2, lines3);
					await UpdateCompleteStepAsync(job, 0, 2, JobStepOutcome.Failure);

					string[] lines4 = lines1.Concat(lines2).Concat(lines3).ToArray();
					await ParseEventsAsync(job, 0, 3, lines4);
					await UpdateCompleteStepAsync(job, 0, 3, JobStepOutcome.Failure);

					issues = await IssueCollection.FindIssuesAsync();
					Assert.AreEqual(4, issues.Count);
				}

				job = CreateJob(_mainStreamId, 130, "Test Build", _graph);

				await ParseEventsAsync(job, 0, 0, lines1);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Warnings);

				await ParseEventsAsync(job, 0, 1, lines1);
				await UpdateCompleteStepAsync(job, 0, 1, JobStepOutcome.Warnings);

				await UpdateCompleteStepAsync(job, 0, 2, JobStepOutcome.Success);
				await UpdateCompleteStepAsync(job, 0, 3, JobStepOutcome.Success);

				issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
			}
		}

		[TestMethod]
		public async Task SymbolIssueTestAsync()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120
			// Expected: Creates issue, blames submitter at CL 115 due to file matching symbol name
			{
				string[] lines =
				{
					@"  Foo.cpp.obj : error LNK2019: unresolved external symbol ""__declspec(dllimport) private: static class UClass * __cdecl UE::FOO::BAR"" (__UE__FOO_BAR) referenced in function ""class UPhysicalMaterial * __cdecl ConstructorHelpersInternal::FindOrLoadObject<class UPhysicalMaterial>(class FString &,unsigned int)"" (??$FindOrLoadObject@VUPhysicalMaterial@@@ConstructorHelpersInternal@@YAPEAVUPhysicalMaterial@@AEAVFString@@I@Z)"
				};

				IJob job = CreateJob(_mainStreamId, 120, "Test Build", _graph);
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				IIssue issue = issues[0];
				IReadOnlyList<IIssueSpan> spans = await IssueCollection.FindSpansAsync(issue.Id);
				Assert.AreEqual(spans.Count, 1);
				Assert.AreEqual(issue.Fingerprints.Count, 1);
				Assert.AreEqual(issue.Fingerprints[0].Type, "Symbol");

				IIssueSpan stream = spans[0];
				Assert.AreEqual(105, stream.LastSuccess?.CommitId.GetPerforceChange());
				Assert.AreEqual(null, stream.NextSuccess?.CommitId);

				IReadOnlyList<IIssueSuspect> suspects = await IssueCollection.FindSuspectsAsync(issues[0]);

				List<UserId> primarySuspects = suspects.Select(x => x.AuthorId).ToList();
				Assert.AreEqual(1, primarySuspects.Count);
				Assert.AreEqual(_jerryId, primarySuspects[0]); // 115 = foo.cpp
			}
		}

		[TestMethod]
		public async Task SymbolIssueTest2Async()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120 on different platforms
			// Expected: Creates single issue
			{
				IJob job = CreateJob(_mainStreamId, 120, "Test Build", _graph);

				string[] lines1 =
				{
					@"Undefined symbols for architecture x86_64:",
					@"  ""Metasound::FTriggerOnThresholdOperator::DefaultThreshold"", referenced from:",
					@"      Metasound::FTriggerOnThresholdOperator::DeclareVertexInterface() in Module.MetasoundStandardNodes.cpp.o",
					@"ld: symbol(s) not found for architecture x86_64",
					@"clang: error: linker command failed with exit code 1 (use -v to see invocation)"
				};
				await ParseEventsAsync(job, 0, 0, lines1);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				// NB. This is a new step and has not succeeded before, but can still be merged with the issue above.
				string[] lines2 =
				{
					@"ld.lld: error: undefined symbol: Metasound::FTriggerOnThresholdOperator::DefaultThreshold",
					@">>> referenced by Module.MetasoundStandardNodes.cpp",
					@">>>               D:/build/++UE5/Sync/Engine/Plugins/Runtime/Metasound/Intermediate/Build/Linux/B4D820EA/UnrealEditor/Debug/MetasoundStandardNodes/Module.MetasoundStandardNodes.cpp.o:(Metasound::FTriggerOnThresholdOperator::DeclareVertexInterface())",
					@"clang++: error: linker command failed with exit code 1 (use -v to see invocation)",
				};
				await ParseEventsAsync(job, 0, 1, lines2);
				await UpdateCompleteStepAsync(job, 0, 1, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				IIssue issue = issues[0];
				IReadOnlyList<IIssueSpan> spans = await IssueCollection.FindSpansAsync(issue.Id);
				Assert.AreEqual(spans.Count, 2);
				Assert.AreEqual(issue.Fingerprints.Count, 1);
				Assert.AreEqual(issue.Fingerprints[0].Type, "Symbol");

				IIssueSpan span1 = spans[0];
				Assert.AreEqual(105, span1.LastSuccess?.CommitId.GetPerforceChange());
				Assert.AreEqual(null, span1.NextSuccess?.CommitId);

				IIssueSpan span2 = spans[1];
				Assert.AreEqual(null, span2.LastSuccess?.CommitId);
				Assert.AreEqual(null, span2.NextSuccess?.CommitId);
			}
		}

		[TestMethod]
		public async Task SymbolIssueTest3Async()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120 on different platforms
			// Expected: Creates single issue
			{
				IJob job = CreateJob(_mainStreamId, 120, "Test Build", _graph);

				string[] lines =
				{
					@"  DatasmithDirectLink.cpp.obj : error LNK2019: unresolved external symbol ""enum DirectLink::ECommunicationStatus __cdecl DirectLink::ValidateCommunicationStatus(void)"" (?ValidateCommunicationStatus@DirectLink@@YA?AW4ECommunicationStatus@1@XZ) referenced in function ""public: static int __cdecl FDatasmithDirectLink::ValidateCommunicationSetup(void)"" (?ValidateCommunicationSetup@FDatasmithDirectLink@@SAHXZ)",
					@"  Engine\Binaries\Win64\UE4Editor-DatasmithExporter.dll: fatal error LNK1120: 1 unresolved externals",
				};
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				IIssue issue = issues[0];
				IReadOnlyList<IIssueSpan> spans = await IssueCollection.FindSpansAsync(issue.Id);
				Assert.AreEqual(1, spans.Count);
				Assert.AreEqual(1, issue.Fingerprints.Count);
				Assert.AreEqual("Symbol", issue.Fingerprints[0].Type);

				IIssueSpan span = spans[0];
				Assert.AreEqual(105, span.LastSuccess?.CommitId.GetPerforceChange());
				Assert.AreEqual(null, span.NextSuccess?.CommitId);
			}

			// #3
			// Scenario: Job step fails at 125 with link error, but does not match symbol name
			// Expected: Creates new issue
			{
				IJob job = CreateJob(_mainStreamId, 125, "Test Build", _graph);

				string[] lines =
				{
					@"  Engine\Binaries\Win64\UE4Editor-DatasmithExporter.dll: fatal error LNK1120: 1 unresolved externals",
				};
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				IIssue issue = issues[0];
				IReadOnlyList<IIssueSpan> spans = await IssueCollection.FindSpansAsync(issue.Id);
				Assert.AreEqual(1, spans.Count);
				Assert.AreEqual(1, issue.Fingerprints.Count);
				Assert.AreEqual("Hashed", issue.Fingerprints[0].Type);

				IIssueSpan span = spans[0];
				Assert.AreEqual(105, span.LastSuccess?.CommitId.GetPerforceChange());
				Assert.AreEqual(null, span.NextSuccess?.CommitId);
			}
		}

		[TestMethod]
		public async Task SymbolIssueTest4Async()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120
			// Expected: Creates a linker issue with severity error due to fatal warnings 
			{
				string[] lines =
				{
					@"ld: warning: direct access in function 'void Eigen::internal::evaluateProductBlockingSizesHeuristic<Eigen::half, Eigen::half, 1, long>(long&, long&, long&, long)' from file '../../EngineTest/Intermediate/Build/Mac/x86_64/EngineTest/Development/ORT/inverse.cc.o' to global weak symbol 'guard variable for Eigen::internal::manage_caching_sizes(Eigen::Action, long*, long*, long*)::m_cacheSizes' from file '../../EngineTest/Intermediate/Build/Mac/x86_64/EngineTest/Development/DynamicMesh/Module.DynamicMesh.4_of_5.cpp.o' means the weak symbol cannot be overridden at runtime. This was likely caused by different translation units being compiled with different visibility settings.",
					@"ld: fatal warning(s) induced error (-fatal_warnings)",
					@"clang: error: linker command failed with exit code 1 (use -v to see invocation)"
				};

				IJob job = CreateJob(_mainStreamId, 120, "Test Build", _graph);
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				IIssue issue = issues[0];

				Assert.AreEqual(IssueSeverity.Error, issue.Severity);
			}
		}

		[TestMethod]
		public async Task LinkerIssueTest2Async()
		{
			string[] lines =
			{
				@"..\Intermediate\Build\Win64\UnrealEditor\Development\AdvancedPreviewScene\Default.rc2.res : fatal error LNK1123: failure during conversion to COFF: file invalid or corrupt"
			};

			IJob job = CreateJob(_mainStreamId, 120, "Linker Test", _graph);
			await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);
			await ParseEventsAsync(job, 0, 0, lines);
			await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

			IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
			Assert.AreEqual(1, issues.Count);

			IIssue issue = issues[0];
			Assert.AreEqual("Errors in Update Version Files", issue.Summary);
		}

		private static IEnumerable<JsonLogEvent> MultilineLogEvent(LogLevel level, EventId eventId, string format, Dictionary<string, object?> properties)
		{
			DateTime time = new DateTime(2024, 1, 1, 0, 0, 0, 0, DateTimeKind.Utc);
			string message = MessageTemplate.Render(format, properties!);
			LogEvent baseEvent = new LogEvent(time, level, eventId, message, format, properties, null);
			JsonLogEvent jsonLogEvent = new JsonLogEvent(baseEvent);
			ServerLogPacketBuilder writer = new ServerLogPacketBuilder();
			writer.SanitizeAndWriteEvent(jsonLogEvent);
			string[] output = Encoding.UTF8.GetString(writer.CreatePacket().Item1.Span).Split('\n');
			foreach (string outData in output)
			{
				JsonLogEvent outJsonLogEvent;
				if (JsonLogEvent.TryParse(Encoding.UTF8.GetBytes(outData), out outJsonLogEvent))
				{
					yield return outJsonLogEvent;
				}
			}
		}

		[TestMethod]
		public async Task GauntletTestAsync()
		{
			// #1
			// Scenario: Gauntlet test event with a property
			// Expected: Gauntlet fingerprint using step identifiers
			{
				IJob job = CreateJob(_mainStreamId, 110, "Test Build", _graph);
				await using (TestJsonLogger logger = await CreateLoggerAsync(job, 0, 0))
				{
					logger.LogError(KnownLogEvents.Gauntlet_TestEvent, "Test {Name} failed", "Bar.Foo.Test");
				}
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.AreEqual("Gauntlet:test", issues[0].Fingerprints[0].Type);
				Assert.AreEqual(new IssueKey($"{job.StreamId}:Update Version Files", IssueKeyType.None), issues[0].Fingerprints[0].Keys.First());
				Assert.AreEqual("Automation test errors in Update Version Files", issues[0].Summary);
			}
			// #2
			// Scenario: Gauntlet Fatal event
			// Expected: Gauntlet fingerprint using hash prefix and generate only one issue as they share the same summary
			{
				string logMessage = "Assertion failed: State.bGfxPSOSet [File:D:\\build\\U5M+Inc\\Sync\\Engine\\Source\\Runtime\\RHI\\Public\\RHIValidationContext.h] [Line: 809]\n"
				+ "A Graphics PSO has to be set to set resources into a shader!";
				string logCallstack = "	0x00007fff4b43a1f4 UnrealEditor-RHI.dll!FValidationContext::RHISetShaderParameters() [Unknown File]\n"
				+ "	0x00007fff4b3e5a01 UnrealEditor-RHI.dll!FRHICommandSetShaderParameters<FRHIGraphicsShader>::Execute() [Unknown File]\n"
				+ "	0x00007fff4b3e9a5a UnrealEditor-RHI.dll!FRHICommand<FRHICommandSetShaderParameters<FRHIGraphicsShader>,FRHICommandSetShaderParametersString1159>::ExecuteAndDestruct() [Unknown File]\n"
				+ "	0x00007fff4b3e74f7 UnrealEditor - RHI.dll!FRHICommandListBase::Execute()[Unknown File]\n"
				+ "	0x00007fff4b3f3228 UnrealEditor-RHI.dll!FRHICommandListImmediate::ExecuteAndReset() [Unknown File]\n"
				+ "	0x00007fff4b464ebf UnrealEditor-RHI.dll!FRHIComputeCommandList::SubmitCommandsHint() [Unknown File]\n"
				+ "	0x00007fff3d3066fc UnrealEditor-Renderer.dll!TBaseStaticDelegateInstance<void [Unknown File]\n"
				+ "	0x00007fff48bd7813 UnrealEditor-RenderCore.dll!FRDGBuilder::ExecutePass() [Unknown File]\n"
				+ "	0x00007fff48bd2a8a UnrealEditor-RenderCore.dll!FRDGBuilder::Execute() [Unknown File]\n"
				+ "	0x00007fff3d31fef4 UnrealEditor-Renderer.dll!FSceneRenderer::RenderThreadEnd() [Unknown File]\n"
				+ "	0x00007fff3d2ed57a UnrealEditor-Renderer.dll!`FPixelShaderUtils::AddFullscreenPass<FHZBTestPS>'::`2'::<lambda_1>::operator()() [Unknown File]\n"
				+ "	0x00007fff3d3043f9 UnrealEditor - Renderer.dll!FSceneRenderer::DoOcclusionQueries()[Unknown File]\n"
				+ "	0x00007fff3d30c888 UnrealEditor-Renderer.dll!TBaseStaticDelegateInstance<void [Unknown File]\n"
				+ "	0x00007fff501fad42 UnrealEditor-Core.dll!FNamedTaskThread::ProcessTasksNamedThread() [Unknown File]\n"
				+ "	0x00007fff501fb25e UnrealEditor-Core.dll!FNamedTaskThread::ProcessTasksUntilQuit() [Unknown File]\n"
				+ "	0x00007fff48ccbb54 UnrealEditor-RenderCore.dll!RenderingThreadMain() [Unknown File]\n"
				+ "	0x00007fff48ccff44 UnrealEditor-RenderCore.dll!FRenderingThread::Run() [Unknown File]\n"
				+ "	0x00007fff5089c862 UnrealEditor-Core.dll!FRunnableThreadWin::Run() [Unknown File]\n"
				+ "	0x00007fff5089a7bf UnrealEditor-Core.dll!FRunnableThreadWin::GuardedRun() [Unknown File]\n"
				+ "	0x00007fff8e304ed0 KERNEL32.DLL!UnknownFunction [Unknown File]\n"
				+ "	0x00007fff8f26e39b ntdll.dll!UnknownFunction [Unknown File]\n";
				string logAlternateCallstack = "	0x00007fff4b43a1b4 UnrealEditor-RHI.dll!FValidationContext::RHISetShaderParameters() [Unknown File]\n"
				+ "	0x00007fff4b3e5a01 UnrealEditor-RHI.dll!FRHICommandSetShaderParameters<FRHIGraphicsShader>::Execute() [Unknown File]\n"
				+ "	0x00007fff4b3e9a5a UnrealEditor-RHI.dll!FRHICommand<FRHICommandSetShaderParameters<FRHIGraphicsShader>,FRHICommandSetShaderParametersString1159>::ExecuteAndDestruct() [Unknown File]\n"
				+ "	0x00007fff4b3e74f7 UnrealEditor - RHI.dll!FRHICommandListBase::Execute()[Unknown File]\n";

				IJob job = CreateJob(_mainStreamId, 120, "Test Build", _graph);
				await using (TestJsonLogger logger = await CreateLoggerAsync(job, 0, 0))
				{
					logger.LogError(KnownLogEvents.Gauntlet_FatalEvent, "{Summary}\n{Callstack}", logMessage, logCallstack);
				}
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);
				await using (TestJsonLogger logger = await CreateLoggerAsync(job, 0, 1))
				{
					Dictionary<string, object?> properties = new Dictionary<string, object?>
					{
						["Summary"] = logMessage,
						["Callstack"] = logAlternateCallstack
					};
					foreach (JsonLogEvent outJsonLogEvent in MultilineLogEvent(LogLevel.Error, KnownLogEvents.Gauntlet_FatalEvent, "Engine encounter a critical failure\n{Summary}\n{Callstack}", properties))
					{
						logger.LogJsonLogEvent(outJsonLogEvent);
					}
				}
				await UpdateCompleteStepAsync(job, 0, 1, JobStepOutcome.Failure);
				await using (TestJsonLogger logger = await CreateLoggerAsync(job, 0, 2))
				{
					LogValue logSummary = new LogValue(new Utf8String("Summary"), logMessage);
					logger.LogError(KnownLogEvents.Gauntlet_FatalEvent, "{Summary}\n{Callstack}", logSummary, logAlternateCallstack);
				}
				await UpdateCompleteStepAsync(job, 0, 2, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.AreEqual(1, issues[0].Fingerprints.Count);
				Assert.AreEqual("Gauntlet:fatal:with-callstack:", issues[0].Fingerprints[0].Type.Substring(0, 30));
				Assert.AreEqual("hash:", issues[0].Fingerprints[0].Keys.First().Name.Substring(0, 5));
				Assert.AreEqual(37, issues[0].Fingerprints[0].Keys.First().Name.Length);
				Assert.AreEqual("Automation fatal errors in Update Version Files, Compile UnrealHeaderTool Win64 and Compile ShooterGameEditor Win64", issues[0].Summary);
			}
			// #3
			// Scenario: Gauntlet Test + Fatal event
			// Expected: Gauntlet Test fingerprint using hash prefix and job step salt, Gauntlet Fatal is a separated issue
			{
				string[] logErrors =
				{
				"	Expected 'SimpleValue::Get meta equality' to be true.",
				"	Expected 'SimpleValueSkipData::Get meta equality' to be true.",
				"	Expected 'SimpleValueZen::Get meta equality' to be true.",
				"	Expected 'SimpleValueZenAndDirect::Get meta equality' to be true.",
				"	Expected 'SimpleValueSkipDataZen::Get meta equality' to be true.",
				"	Expected 'SimpleValueSkipDataZenAndDirect::Get meta equality' to be true.",
				"	Expected 'SimpleValueWithMeta::Get meta equality' to be true.",
				"	Expected 'SimpleValueWithMetaSkipData::Get meta equality' to be true.",
				"	Expected 'SimpleValueWithMetaZenAndDirect::Get meta equality' to be true.",
				"	Expected 'SimpleValueWithMetaSkipDataZenAndDirect::Get meta equality' to be true."
				};

				IJob job = CreateJob(_mainStreamId, 130, "Test Build", _graph);
				await using (TestJsonLogger logger = await CreateLoggerAsync(job, 0, 0))
				{
					logger.LogError(KnownLogEvents.Gauntlet_FatalEvent, "Some critical error");
					foreach (string error in logErrors)
					{
						logger.LogError(KnownLogEvents.Gauntlet_TestEvent, "{Error}", error);
					}
				}
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);
				await UpdateCompleteStepAsync(job, 0, 1, JobStepOutcome.Success);
				await UpdateCompleteStepAsync(job, 0, 2, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(2, issues.Count);
				Assert.AreEqual("Gauntlet:test", issues[0].Fingerprints[0].Type);
				Assert.AreEqual("Gauntlet:fatal", issues[1].Fingerprints[0].Type);
				Assert.AreEqual(10, issues[0].Fingerprints[0].Keys.Count);
				Assert.AreEqual("hash:", issues[0].Fingerprints[0].Keys.First().Name.Substring(0, 5));
				Assert.AreEqual($":{job.StreamId}:Update Version Files", issues[0].Fingerprints[0].Keys.First().Name.Substring(37));
				Assert.AreEqual("Automation test errors in Update Version Files", issues[0].Summary);
			}
		}

		[TestMethod]
		public async Task MaskIssueTestAsync()
		{
			// #1
			// Scenario: Job step completes successfully at CL 105
			// Expected: No issues are created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}

			// #2
			// Scenario: Job step fails at CL 120 with compile & link error
			// Expected: Creates one issue for compile error
			{
				string[] lines =
				{
					FileReference.Combine(_workspaceDir, "foo.cpp").FullName + @"(78): error C2664: 'FDelegateHandle TBaseMulticastDelegate&lt;void,FChaosScene *&gt;::AddUObject&lt;AFortVehicleManager,&gt;(const UserClass *,void (__cdecl AFortVehicleManager::* )(FChaosScene *) const)': cannot convert argument 2 from 'void (__cdecl AFortVehicleManager::* )(FPhysScene *)' to 'void (__cdecl AFortVehicleManager::* )(FChaosScene *)'",
					@"  Foo.cpp.obj : error LNK2019: unresolved external symbol ""__declspec(dllimport) private: static class UClass * __cdecl UE::FOO::BAR"" (__UE__FOO_BAR) referenced in function ""class UPhysicalMaterial * __cdecl ConstructorHelpersInternal::FindOrLoadObject<class UPhysicalMaterial>(class FString &,unsigned int)"" (??$FindOrLoadObject@VUPhysicalMaterial@@@ConstructorHelpersInternal@@YAPEAVUPhysicalMaterial@@AEAVFString@@I@Z)"
				};

				IJob job = CreateJob(_mainStreamId, 120, "Test Build", _graph);
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				IIssue issue = issues[0];
				IReadOnlyList<IIssueSpan> spans = await IssueCollection.FindSpansAsync(issue.Id);
				Assert.AreEqual(1, spans.Count);
				Assert.AreEqual(1, issue.Fingerprints.Count);
				Assert.AreEqual("Compile", issue.Fingerprints[0].Type);
			}
		}

		[TestMethod]
		public async Task MissingCopyrightTestAsync()
		{
			string[] lines =
			{
				@"WARNING: Engine\Source\Programs\UnrealBuildTool\ProjectFiles\Rider\ToolchainInfo.cs: Missing copyright boilerplate"
			};

			IJob job = CreateJob(_mainStreamId, 120, "Test Build", _graph);
			await ParseEventsAsync(job, 0, 0, lines);
			await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

			IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
			Assert.AreEqual(1, issues.Count);

			IIssue issue = issues[0];
			IReadOnlyList<IIssueSpan> spans = await IssueCollection.FindSpansAsync(issue.Id);
			Assert.AreEqual(1, spans.Count);
			Assert.AreEqual(1, issue.Fingerprints.Count);
			Assert.AreEqual("Copyright", issue.Fingerprints[0].Type);
			Assert.AreEqual("Missing copyright notice in ToolchainInfo.cs", issue.Summary);
		}

		[TestMethod]
		public async Task AddSpanToIssueTestAsync()
		{
			// Create the first issue
			IIssue issueA;
			IIssueSpan spanA;
			{
				IJob job = CreateJob(_mainStreamId, 120, "Test Build", _graph);

				string[] lines =
				{
					@"  DatasmithDirectLink.cpp.obj : error LNK2019: unresolved external symbol ""enum DirectLink::ECommunicationStatus __cdecl DirectLink::ValidateCommunicationStatus(void)"" (?ValidateCommunicationStatus@DirectLink@@YA?AW4ECommunicationStatus@1@XZ) referenced in function ""public: static int __cdecl FDatasmithDirectLink::ValidateCommunicationSetup(void)"" (?ValidateCommunicationSetup@FDatasmithDirectLink@@SAHXZ)",
				};
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				issueA = issues[0];
				IReadOnlyList<IIssueSpan> spans = await IssueCollection.FindSpansAsync(issueA.Id);
				Assert.AreEqual(spans.Count, 1);
				spanA = spans[0];
			}

			// Create the second issue
			IIssue issueB;
			IIssueSpan spanB;
			{
				string[] lines =
				{
					@"WARNING: Engine\Source\Programs\UnrealBuildTool\ProjectFiles\Rider\ToolchainInfo.cs: Missing copyright boilerplate"
				};

				IJob job = CreateJob(_mainStreamId, 120, "Test Build", _graph);
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				issues = issues.Where(x => x.Id != issueA.Id).ToList();
				Assert.AreEqual(1, issues.Count);

				issueB = issues[0];
				IReadOnlyList<IIssueSpan> spans = await IssueCollection.FindSpansAsync(issueB.Id);
				Assert.AreEqual(1, spans.Count);
				spanB = spans[0];
			}

			// Add SpanB to IssueA
			{
				await IssueService.UpdateIssueAsync(issueA.Id, addSpanIds: new List<ObjectId> { spanB.Id });

				IIssue newIssueA = (await IssueCollection.GetIssueAsync(issueA.Id))!;
				Assert.IsNull(newIssueA.VerifiedAt);
				Assert.IsNull(newIssueA.ResolvedAt);
				Assert.AreEqual(2, newIssueA.Fingerprints.Count);
				IReadOnlyList<IIssueSpan> newSpansA = await IssueCollection.FindSpansAsync(newIssueA!.Id);
				Assert.AreEqual(2, newSpansA.Count);
				Assert.AreEqual(newIssueA.Id, newSpansA[0].IssueId);
				Assert.AreEqual(newIssueA.Id, newSpansA[1].IssueId);

				IIssue newIssueB = (await IssueCollection.GetIssueAsync(issueB.Id))!;
				Assert.IsNotNull(newIssueB.VerifiedAt);
				Assert.IsNotNull(newIssueB.ResolvedAt);
				Assert.AreEqual(0, newIssueB.Fingerprints.Count);
				IReadOnlyList<IIssueSpan> newSpansB = await IssueCollection.FindSpansAsync(newIssueB.Id);
				Assert.AreEqual(0, newSpansB.Count);
			}

			// Add SpanA and SpanB to IssueB
			{
				await IssueService.UpdateIssueAsync(issueB.Id, addSpanIds: new List<ObjectId> { spanA.Id, spanB.Id });

				IIssue newIssueA = (await IssueCollection.GetIssueAsync(issueA.Id))!;
				Assert.IsNotNull(newIssueA.VerifiedAt);
				Assert.IsNotNull(newIssueA.ResolvedAt);
				Assert.AreEqual(0, newIssueA.Fingerprints.Count);
				IReadOnlyList<IIssueSpan> newSpansA = await IssueCollection.FindSpansAsync(newIssueA.Id);
				Assert.AreEqual(0, newSpansA.Count);

				IIssue newIssueB = (await IssueCollection.GetIssueAsync(issueB.Id))!;
				Assert.IsNull(newIssueB.VerifiedAt);
				Assert.IsNull(newIssueB.ResolvedAt);
				Assert.AreEqual(2, newIssueB.Fingerprints.Count);
				IReadOnlyList<IIssueSpan> newSpansB = await IssueCollection.FindSpansAsync(newIssueB!.Id);
				Assert.AreEqual(2, newSpansB.Count);
				Assert.AreEqual(newIssueB.Id, newSpansB[0].IssueId);
				Assert.AreEqual(newIssueB.Id, newSpansB[1].IssueId);
			}
		}

		[TestMethod]
		public async Task ExplicitGroupingTestAsync()
		{
			string[] lines =
			{
				FileReference.Combine(_workspaceDir, "foo.cpp").FullName + @"(78): error C2664: 'FDelegateHandle TBaseMulticastDelegate&lt;void,FChaosScene *&gt;::AddUObject&lt;AFortVehicleManager,&gt;(const UserClass *,void (__cdecl AFortVehicleManager::* )(FChaosScene *) const)': cannot convert argument 2 from 'void (__cdecl AFortVehicleManager::* )(FPhysScene *)' to 'void (__cdecl AFortVehicleManager::* )(FChaosScene *)'",
			};

			IJob job = CreateJob(_mainStreamId, 120, "Test Build", _graph);

			// Create the first issue
			{
				await ParseEventsAsync(job, 0, 4, lines);
				await UpdateCompleteStepAsync(job, 0, 4, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.AreEqual("Compile", issues[0].Fingerprints[0].Type);
			}

			// Create the same error in a different group, check they don't merge
			{
				await ParseEventsAsync(job, 0, 5, lines);
				await UpdateCompleteStepAsync(job, 0, 5, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(2, issues.Count);

				issues = issues.OrderBy(x => x.Id).ToList();
				Assert.AreEqual("Compile", issues[0].Fingerprints[0].Type);
				Assert.AreEqual("Compile:StaticAnalysis", issues[1].Fingerprints[0].Type);
			}
		}

		[TestMethod]
		public async Task FixFailedTestAsync()
		{
			int issueId;

			// #1
			// Scenario: Warning in first step
			// Expected: Default issue is created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await AddEventAsync(job, 0, 0, LogLevel.Warning);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Warnings);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.AreEqual(IssueSeverity.Warning, issues[0].Severity);

				Assert.AreEqual("Warnings in Update Version Files", issues[0].Summary);

				issueId = issues[0].Id;
			}

			// #2
			// Scenario: Issue is marked fixed
			// Expected: Resolved time, owner is set
			{
				await IssueService.UpdateIssueAsync(issueId, resolvedById: _bobId);

				IReadOnlyList<IIssue> openIssues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, openIssues.Count);

				IIssue issue = (await IssueCollection.GetIssueAsync(issueId))!;
				Assert.IsNotNull(issue.ResolvedAt);
				Assert.AreEqual(_bobId, issue.OwnerId);
				Assert.AreEqual(_bobId, issue.ResolvedById);
			}

			// #3
			// Scenario: Issue recurs an hour later
			// Expected: Issue is still marked as resolved
			{
				IJob job = CreateJob(_mainStreamId, 110, "Test Build", _graph, TimeSpan.FromHours(1.0));
				await AddEventAsync(job, 0, 0, LogLevel.Warning);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Warnings);

				IReadOnlyList<IIssue> openIssues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, openIssues.Count);

				IIssue issue = (await IssueCollection.GetIssueAsync(issueId))!;
				Assert.IsNotNull(issue.ResolvedAt);
				Assert.AreEqual(_bobId, issue.OwnerId);
				Assert.AreEqual(_bobId, issue.ResolvedById);
			}

			// #4
			// Scenario: Issue recurs a day later at the same change
			// Expected: Issue is reopened
			{
				IJob job = CreateJob(_mainStreamId, 110, "Test Build", _graph, TimeSpan.FromHours(25.0));
				await AddEventAsync(job, 0, 0, LogLevel.Warning);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Warnings);

				IReadOnlyList<IIssue> openIssues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, openIssues.Count);

				IIssue issue = openIssues[0];
				Assert.AreEqual(issueId, issue.Id);
				Assert.IsNull(issue.ResolvedAt);
				Assert.AreEqual(_bobId, issue.OwnerId);
				Assert.IsNull(issue.ResolvedById);
			}

			// #5
			// Scenario: Issue is marked fixed again, at a particular changelist
			// Expected: Resolved time, owner is set
			{
				await IssueService.UpdateIssueAsync(issueId, resolvedById: _bobId, fixCommitId: CommitId.FromPerforceChange(115));

				IReadOnlyList<IIssue> openIssues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, openIssues.Count);

				IIssue issue = (await IssueCollection.GetIssueAsync(issueId))!;
				Assert.IsNotNull(issue.ResolvedAt);
				Assert.AreEqual(_bobId, issue.OwnerId);
				Assert.AreEqual(_bobId, issue.ResolvedById);
			}

			// #6
			// Scenario: Issue fails again at a later changelist
			// Expected: Issue is reopened
			{
				IJob job = CreateJob(_mainStreamId, 120, "Test Build", _graph, TimeSpan.FromHours(25.0));
				await AddEventAsync(job, 0, 0, LogLevel.Warning);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Warnings);

				IReadOnlyList<IIssue> openIssues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, openIssues.Count);

				IIssue issue = openIssues[0];
				Assert.AreEqual(issueId, issue.Id);
				Assert.IsNull(issue.ResolvedAt);
				Assert.AreEqual(_bobId, issue.OwnerId);
				Assert.IsNull(issue.ResolvedById);
			}

			// #7
			// Scenario: Issue is marked fixed again, at a particular changelist
			// Expected: Resolved time, owner is set
			{
				await IssueService.UpdateIssueAsync(issueId, resolvedById: _bobId, fixCommitId: CommitIdWithOrder.FromPerforceChange(125));

				IReadOnlyList<IIssue> openIssues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, openIssues.Count);

				IIssue issue = (await IssueCollection.GetIssueAsync(issueId))!;
				Assert.IsNotNull(issue.ResolvedAt);
				Assert.AreEqual(_bobId, issue.OwnerId);
				Assert.AreEqual(_bobId, issue.ResolvedById);
			}

			// #8
			// Scenario: Issue succeeds at a later changelist
			// Expected: Issue remains closed
			{
				IJob job = CreateJob(_mainStreamId, 125, "Test Build", _graph, TimeSpan.FromHours(25.0));
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> openIssues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, openIssues.Count);
			}

			// #9
			// Scenario: Issue fails at a later changelist
			// Expected: New issue is opened
			{
				IJob job = CreateJob(_mainStreamId, 130, "Test Build", _graph, TimeSpan.FromHours(25.0));
				await AddEventAsync(job, 0, 0, LogLevel.Warning);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Warnings);

				IReadOnlyList<IIssue> openIssues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, openIssues.Count);

				IIssue issue = openIssues[0];
				Assert.AreNotEqual(issueId, issue.Id);
			}
		}

		[TestMethod]
		public async Task AutoResolveTestAsync()
		{
			int issueId;

			// #1
			// Scenario: Warning in first step
			// Expected: Default issue is created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await AddEventAsync(job, 0, 0, LogLevel.Warning);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Warnings);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.AreEqual(IssueSeverity.Warning, issues[0].Severity);

				Assert.AreEqual("Warnings in Update Version Files", issues[0].Summary);

				issueId = issues[0].Id;
			}

			// #2
			// Scenario: Job succeeds
			// Expected: Issue is marked as resolved
			{
				IJob job = CreateJob(_mainStreamId, 115, "Test Build", _graph);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IIssue? issue = await IssueCollection.GetIssueAsync(issueId);
				Assert.IsNotNull(issue!.ResolvedAt);

				IReadOnlyList<IIssueSpan> spans = await IssueCollection.FindSpansAsync(issue.Id);
				Assert.AreEqual(spans.Count, 1);
			}
		}

		[TestMethod]
		public async Task QuarantineTestAsync()
		{
			int issueId;
			DateTime lastSeenAt;
			int hour = 0;

			// #1
			// Scenario: Job succeeds establishing first success
			{
				IJob job = CreateJob(_mainStreamId, 100, "Test Build", _graph, TimeSpan.FromHours(hour++));
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);
			}

			// #2
			// Scenario: Warning in first step
			// Expected: Default issue is created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph, TimeSpan.FromHours(hour++));
				await AddEventAsync(job, 0, 0, LogLevel.Warning);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Warnings);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.AreEqual(IssueSeverity.Warning, issues[0].Severity);

				Assert.AreEqual("Warnings in Update Version Files", issues[0].Summary);

				issueId = issues[0].Id;
				lastSeenAt = issues[0].LastSeenAt;

				IReadOnlyList<IIssueSpan> spans = await IssueCollection.FindSpansAsync(issues[0].Id);
				Assert.AreEqual(1, spans.Count);

				IIssueDetails details = await IssueService.GetIssueDetailsAsync(issues[0]);
				Assert.AreEqual(details.Steps.Count, 1);

			}

			// assign to bob
			await IssueService.UpdateIssueAsync(issueId, ownerId: _bobId);

			// Mark issue as quarantined
			await IssueService.UpdateIssueAsync(issueId, quarantinedById: _jerryId);

			// #3
			// Scenario: Job succeeds
			// Expected: Issue is not marked resolved, though step is added to span history
			{
				IJob job = CreateJob(_mainStreamId, 115, "Test Build", _graph, TimeSpan.FromHours(hour++));
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IIssue? issue = await IssueCollection.GetIssueAsync(issueId);
				Assert.IsNull(issue!.ResolvedAt);
				Assert.IsNull(issue!.VerifiedAt);
				Assert.AreEqual(issue!.OwnerId, _bobId);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
			}

			// #4
			// Scenario: Job fails
			// Expected: Existing issue is updated
			{
				IJob job = CreateJob(_mainStreamId, 125, "Test Build", _graph, TimeSpan.FromHours(hour++));
				await AddEventAsync(job, 0, 0, LogLevel.Warning);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Warnings);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.AreEqual(IssueSeverity.Warning, issues[0].Severity);

				Assert.AreEqual("Warnings in Update Version Files", issues[0].Summary);

				Assert.AreEqual(issueId, issues[0].Id);
				Assert.AreNotEqual(lastSeenAt, issues[0].LastSeenAt);

				// make sure 4 steps have been recorded
				IIssueDetails details = await IssueService.GetIssueDetailsAsync(issues[0]);
				Assert.AreEqual(3, details.Steps.Count);

			}

			// Mark issue as not quarantined
			await IssueService.UpdateIssueAsync(issueId, quarantinedById: UserId.Empty);

			// #5
			// Scenario: Job succeeds
			// Expected: Issue is marked resolved and closed
			{
				IJob job = CreateJob(_mainStreamId, 130, "Test Build", _graph, TimeSpan.FromHours(hour++));
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IIssue? issue = await IssueCollection.GetIssueAsync(issueId);
				Assert.IsNotNull(issue!.ResolvedAt);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);

			}
		}

		[TestMethod]
		public async Task ForceIssueCloseTestAsync()
		{
			int issueId;

			// #1
			// Scenario: Warning in first step
			// Expected: Default issue is created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await AddEventAsync(job, 0, 0, LogLevel.Warning);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Warnings);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.AreEqual(IssueSeverity.Warning, issues[0].Severity);

				Assert.AreEqual("Warnings in Update Version Files", issues[0].Summary);

				issueId = issues[0].Id;
			}

			// #2
			// Scenario: bob resolved issue
			// Expected: Issue is marked resolved, however not verified
			{
				// resolved by bob
				await IssueService.UpdateIssueAsync(issueId, resolvedById: _bobId);
				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
				IIssue issue = (await IssueCollection.GetIssueAsync(issueId))!;
				Assert.AreEqual(issue.ResolvedById, _bobId);
				Assert.IsNull(issue.VerifiedAt);
			}

			// #3
			// Scenario: Job is force closed
			// Expected: Existing issue is closed and verified, bob remains the resolver
			{
				// force closed by jerry
				await IssueService.UpdateIssueAsync(issueId, forceClosedById: _jerryId);
				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);

				IIssue issue = (await IssueCollection.GetIssueAsync(issueId))!;
				Assert.AreEqual(issue.ResolvedById, _bobId);
				Assert.AreEqual(issue.ForceClosedByUserId, _jerryId);
				Assert.IsNotNull(issue.VerifiedAt);
			}

			// #4
			// Scenario: Job fails 25 hours after it is force closed
			// Expected: A new issue is created
			{
				IJob job = CreateJob(_mainStreamId, 125, "Test Build", _graph, TimeSpan.FromHours(25));
				await AddEventAsync(job, 0, 0, LogLevel.Warning);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Warnings);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.AreEqual(IssueSeverity.Warning, issues[0].Severity);

				Assert.AreEqual("Warnings in Update Version Files", issues[0].Summary);

				Assert.AreEqual(2, issues[0].Id);
			}
		}

		[TestMethod]
		public async Task UpdateIssuesFlagTestAsync()
		{
			int hour = 0;

			// #1
			// Scenario: Job is created that doesn't update issues at CL 225 with a successful outcome
			// Expected: No new issue is created
			{
				IJob job = CreateJob(_mainStreamId, 225, "Test Build", _graph, TimeSpan.FromHours(hour++), true, false);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);
				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}

			// #2
			// Scenario: Job is created that doesn't update issues at CL 226 with a failure outcome
			// Expected: No new issue is created
			{
				IJob job = CreateJob(_mainStreamId, 226, "Test Build", _graph, TimeSpan.FromHours(hour++), true, false);
				await AddEventAsync(job, 0, 0, LogLevel.Error);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);
				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}

			// #3
			// Scenario: Job is created at an earlier CL than in #1, with a warning outcome 
			// Expected: Default issue is created
			{
				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph, TimeSpan.FromHours(hour++));
				await AddEventAsync(job, 0, 0, LogLevel.Warning);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Warnings);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.AreEqual(IssueSeverity.Warning, issues[0].Severity);

				Assert.AreEqual("Warnings in Update Version Files", issues[0].Summary);

			}

			// #4
			// Scenario: Job is run which doesn't update issues, with a failure 
			// Expected: Existing issue is not updated and remains a warning
			{
				IJob job = CreateJob(_mainStreamId, 225, "Test Build", _graph, TimeSpan.FromHours(hour++), true, false);
				await AddEventAsync(job, 0, 0, LogLevel.Error);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.AreEqual(1, issues[0].Id);
				Assert.AreEqual(IssueSeverity.Warning, issues[0].Severity);

			}

			// #5
			// Scenario: Job is run with a new failure, when an existing issue is already open with only a warning
			// Expected: A new issue is created rather than the existing issue being updated since the previous issue only contained a warning
			{
				IJob job = CreateJob(_mainStreamId, 225, "Test Build", _graph, TimeSpan.FromHours(hour++));
				await AddEventAsync(job, 0, 0, LogLevel.Error);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.AreEqual(2, issues[0].Id);
				Assert.AreEqual(IssueSeverity.Error, issues[0].Severity);
			}
		}

		[TestMethod]
		public async Task MultipleStreamIssueTestAsync()
		{
			int hours = 0;

			IUser bob = await UserCollection.FindOrAddUserByLoginAsync("Bob");
			IUser anne = await UserCollection.FindOrAddUserByLoginAsync("Anne");

			// Main Stream

			// last success
			_perforce.AddChange(_mainStreamId, 22136421, bob, "Description", new string[] { "a/b.cpp" });

			// unrelated change, no job run on it, merges to release between breakages in that stream
			_perforce.AddChange(_mainStreamId, 22136521, anne, "Description", new string[] { "a/b.cpp" });

			// change with a breakage in main
			_perforce.AddChange(_mainStreamId, 22145160, bob, "Description", new string[] { "a/b.cpp" });

			// success
			_perforce.AddChange(_mainStreamId, 22151893, bob, "Description", new string[] { "a/b.cpp" });

			// breaking change merged from release
			_perforce.AddChange(_mainStreamId, 22166000, 22165119, bob, bob, "Description", new string[] { "a/b.cpp" });

			// Release Stream

			// last success
			_perforce.AddChange(_releaseStreamId, 22133008, bob, "Description", new string[] { "a/b.cpp" });

			// unrelated change originating in main, no job run on it
			_perforce.AddChange(_releaseStreamId, 22152050, 22136521, anne, anne, "Description", new string[] { "a/b.cpp" });

			// unrelated breaking change
			_perforce.AddChange(_releaseStreamId, 22165119, bob, "Description", new string[] { "a/b.cpp" });

			string[] breakage1 =
			{
					"Error executing d:\\build\\AutoSDK\\Sync\\HostWin64\\Win64\\VS2019\\14.29.30146\\bin\\HostX64\\x64\\link.exe (tool returned code: 1123)",
					"LINK : fatal error LNK1123: failure during conversion to COFF: file invalid or corrupt"
			};

			string[] breakage2 =
			{
					"Error executing d:\\build\\AutoSDK\\Sync\\HostWin64\\Win64\\VS2019\\14.29.30146\\bin\\HostX64\\x64\\link.exe (tool returned code: 1169)",
					"D:\\build\\++UE5\\Sync\\QAGame\\Binaries\\Win64\\QAGameEditor.exe : fatal error LNK1169: one or more multiply defined symbols found",
					"Module.Core.1_of_20.cpp.obj : error LNK2005: \"int GNumForegroundWorkers\" (?GNumForegroundWorkers@@3HA) already defined in Module.UnrealEd.20_of_42.cpp.obj"
			};

			// #1
			// Job runs successfully in release stream
			{
				IJob job = CreateJob(_releaseStreamId, 22133008, "Test Build", _graph, TimeSpan.FromHours(hours++));
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);
				await UpdateCompleteStepAsync(job, 0, 1, JobStepOutcome.Success);
				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}

			// #2
			// Job runs successfully in main stream at a latest CL
			{
				IJob job = CreateJob(_mainStreamId, 22136421, "Test Build", _graph, TimeSpan.FromHours(hours++));
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);
				await UpdateCompleteStepAsync(job, 0, 1, JobStepOutcome.Success);
				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}

			// #3
			// Scenario: Job step encounters a hashed issue at CL 22145160
			// Expected: Hashed issue type is created
			{
				IJob job = CreateJob(_mainStreamId, 22145160, "Test Build", _graph, TimeSpan.FromHours(hours++));
				await ParseEventsAsync(job, 0, 0, breakage1);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);
				Assert.AreEqual(1, issues[0].Id);
				Assert.AreEqual("Errors in Update Version Files", issues[0].Summary);
			}

			// #4
			// Scenario: Job succeeds an hour later in CL 22151893
			// Expected: Existing issue is closed
			{
				IJob job = CreateJob(_mainStreamId, 22151893, "Test Build", _graph, TimeSpan.FromHours(hours++));
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Success);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(0, issues.Count);
			}

			// #5
			// Scenario: Job step encounters a failure on CL 22165119
			// Expected:New issue is created, and does not reopen the one in main
			{

				IJob job = CreateJob(_releaseStreamId, 22165119, "Test Build", _graph, TimeSpan.FromHours(hours++));
				await ParseEventsAsync(job, 0, 0, breakage2);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				// Check that new issue was created
				Assert.AreEqual(2, issues[0].Id);
				Assert.AreEqual("Errors in Update Version Files", issues[0].Summary);

			}

			// #6
			// Scenario: Job step in main encounters a failure on CL 22166000 originating from 22165119 breakage
			// Expected: Step is added to existing issue and summary is updated
			{

				IJob job = CreateJob(_mainStreamId, 22166000, "Test Build", _graph, TimeSpan.FromHours(hours++));
				await ParseEventsAsync(job, 0, 1, breakage2);
				await UpdateCompleteStepAsync(job, 0, 1, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				// Check that new issue was created
				Assert.AreEqual(2, issues[0].Id);
				Assert.AreEqual("Errors in Update Version Files and Compile UnrealHeaderTool Win64", issues[0].Summary);
			}
		}

		[TestMethod]
		public async Task SystemicIssuesTestAsync()
		{
			// #1
			// Scenario: Job step fails with systemic XGE error
			// Expected: A systemic issue is created
			{
				string[] lines =
				{
					@"BUILD FAILED: Command failed (Result:1): C:\Program Files (x86)\Incredibuild\xgConsole.exe ""d:\build\++UE5\Sync\Engine\Programs\AutomationTool\Saved\Logs\UAT_XGE.xml"" /Rebuild /NoLogo /ShowAgent /ShowTime /no_watchdog_thread. See logfile for details: 'xgConsole-2023.01.03-23.39.48.txt'"
				};

				IJob job = CreateJob(_mainStreamId, 105, "Test Build", _graph);
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				IIssue issue = issues[0];

				Assert.AreEqual(IssueSeverity.Error, issue.Severity);
				Assert.AreEqual(1, issue.Fingerprints.Count);
				Assert.AreEqual("Systemic", issue.Fingerprints[0].Type);
			}

			// #2
			// Scenario: Job step fails at CL 120 with a linker issue, additionally there is a systemic xgConsole.exe error
			// Expected: Creates a linker issue with severity error due to fatal warnings, does not create a systemic error
			{
				string[] lines =
				{
					@"ld: warning: direct access in function 'void Eigen::internal::evaluateProductBlockingSizesHeuristic<Eigen::half, Eigen::half, 1, long>(long&, long&, long&, long)' from file '../../EngineTest/Intermediate/Build/Mac/x86_64/EngineTest/Development/ORT/inverse.cc.o' to global weak symbol 'guard variable for Eigen::internal::manage_caching_sizes(Eigen::Action, long*, long*, long*)::m_cacheSizes' from file '../../EngineTest/Intermediate/Build/Mac/x86_64/EngineTest/Development/DynamicMesh/Module.DynamicMesh.4_of_5.cpp.o' means the weak symbol cannot be overridden at runtime. This was likely caused by different translation units being compiled with different visibility settings.",
					@"ld: fatal warning(s) induced error (-fatal_warnings)",
					@"clang: error: linker command failed with exit code 1 (use -v to see invocation)",
					@"BUILD FAILED: Command failed (Result:1): C:\Program Files (x86)\Incredibuild\xgConsole.exe ""d:\build\++UE5\Sync\Engine\Programs\AutomationTool\Saved\Logs\UAT_XGE.xml"" /Rebuild /NoLogo /ShowAgent /ShowTime /no_watchdog_thread. See logfile for details: 'xgConsole-2023.01.03-23.39.48.txt'"
				};

				IJob job = CreateJob(_mainStreamId, 120, "Test Build", _graph);
				await ParseEventsAsync(job, 0, 0, lines);
				await UpdateCompleteStepAsync(job, 0, 0, JobStepOutcome.Failure);

				IReadOnlyList<IIssue> issues = await IssueCollection.FindIssuesAsync();
				Assert.AreEqual(1, issues.Count);

				IIssue issue = issues[0];

				Assert.AreEqual(IssueSeverity.Error, issue.Severity);
				Assert.AreEqual(1, issue.Fingerprints.Count);
				Assert.AreEqual("Hashed", issue.Fingerprints[0].Type);
			}
		}

		//		static List<IIssueSpan> GetOriginSpans(List<IIssueSpan> spans)

		static IIssueSpanSuspect MockSuspect(int change, int? originatingChange)
		{
			Mock<IIssueSpanSuspect> suspect = new Mock<IIssueSpanSuspect>(MockBehavior.Strict);
			suspect.SetupGet(x => x.CommitId).Returns(CommitIdWithOrder.FromPerforceChange(change));
			suspect.SetupGet(x => x.SourceCommitId).Returns(CommitIdWithOrder.FromPerforceChange(originatingChange));
			return suspect.Object;
		}

		static IIssueSpan MockSpan(StreamId streamId, params IIssueSpanSuspect[] suspects)
		{
			Mock<IIssueSpan> span = new Mock<IIssueSpan>(MockBehavior.Strict);
			span.SetupGet(x => x.StreamId).Returns(streamId);
			span.SetupGet(x => x.Suspects).Returns(suspects);
			return span.Object;
		}

		[TestMethod]
		public void FindMergeOrigins()
		{
			{
				List<IIssueSpan> spans = new List<IIssueSpan>();
				spans.Add(MockSpan(new StreamId("ue5-main"), MockSuspect(201, 1), MockSuspect(202, 2)));
				spans.Add(MockSpan(new StreamId("ue5-release-staging"), MockSuspect(101, 1), MockSuspect(102, 2)));
				spans.Add(MockSpan(new StreamId("ue5-release"), MockSuspect(1, null), MockSuspect(2, null)));

				List<IIssueSpan> results = HordeServer.Issues.IssueService.FindMergeOriginSpans(spans);
				Assert.AreEqual(1, results.Count);
				Assert.AreEqual(new StreamId("ue5-release"), results[0].StreamId);
			}

			{
				List<IIssueSpan> spans = new List<IIssueSpan>();
				spans.Add(MockSpan(new StreamId("ue5-main"), MockSuspect(201, null), MockSuspect(202, 2)));
				spans.Add(MockSpan(new StreamId("ue5-release-staging"), MockSuspect(101, 1), MockSuspect(102, null)));
				spans.Add(MockSpan(new StreamId("ue5-release"), MockSuspect(1, null), MockSuspect(2, null)));

				List<IIssueSpan> results = HordeServer.Issues.IssueService.FindMergeOriginSpans(spans);
				Assert.AreEqual(1, results.Count);
				Assert.AreEqual(new StreamId("ue5-release"), results[0].StreamId);
			}

			{
				List<IIssueSpan> spans = new List<IIssueSpan>();
				spans.Add(MockSpan(new StreamId("ue5-release"), MockSuspect(1, null), MockSuspect(2, null)));
				spans.Add(MockSpan(new StreamId("ue5-release-staging"), MockSuspect(101, 1), MockSuspect(102, null)));
				spans.Add(MockSpan(new StreamId("ue5-main"), MockSuspect(201, null), MockSuspect(202, 2)));

				List<IIssueSpan> results = HordeServer.Issues.IssueService.FindMergeOriginSpans(spans);
				Assert.AreEqual(1, results.Count);
				Assert.AreEqual(new StreamId("ue5-release"), results[0].StreamId);
			}
		}
	}
}

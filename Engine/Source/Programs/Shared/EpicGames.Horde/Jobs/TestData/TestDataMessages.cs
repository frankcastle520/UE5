// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using EpicGames.Horde.Commits;

#pragma warning disable CA2227 // Change 'x' to be read-only by removing the property setter

namespace EpicGames.Horde.Jobs.TestData
{
	/// <summary>
	/// Test outcome
	/// </summary>
	public enum TestOutcome
	{
		/// <summary>
		/// The test was successful
		/// </summary>
		Success,
		/// <summary>
		/// The test failed
		/// </summary>
		Failure,
		/// <summary>
		/// The test was skipped
		/// </summary>
		Skipped,
		/// <summary>
		/// The test had an unspecified result
		/// </summary>
		Unspecified
	}

	/// <summary>
	/// Response object describing test data to store
	/// </summary>
	public class CreateTestDataRequest
	{
		/// <summary>
		/// The job which produced the data
		/// </summary>
		[Required]
		public JobId JobId { get; set; }

		/// <summary>
		/// The step that ran
		/// </summary>
		[Required]
		public JobStepId StepId { get; set; }

		/// <summary>
		/// Key used to identify the particular data
		/// </summary>
		public string Key { get; set; } = String.Empty;

		/// <summary>
		/// The data stored for this test
		/// </summary>
		[Required]
		public Dictionary<string, object> Data { get; set; } = new Dictionary<string, object>();
	}

	/// <summary>
	/// Response object describing the created document
	/// </summary>
	public class CreateTestDataResponse
	{
		/// <summary>
		/// The id for the new document
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="id">Id of the new document</param>
		public CreateTestDataResponse(string id)
		{
			Id = id;
		}
	}

	/// <summary>
	/// Response object describing test results
	/// </summary>
	public class GetTestDataResponse
	{
		/// <summary>
		/// Unique id of the test data
		/// </summary>
		public string Id { get; set; } = String.Empty;

		/// <summary>
		/// Stream that generated the test data
		/// </summary>
		public string StreamId { get; set; } = String.Empty;

		/// <summary>
		/// The template reference id
		/// </summary>
		public string TemplateRefId { get; set; } = String.Empty;

		/// <summary>
		/// The job which produced the data
		/// </summary>
		public string JobId { get; set; } = String.Empty;

		/// <summary>
		/// The step that ran
		/// </summary>
		public string StepId { get; set; } = String.Empty;

		/// <summary>
		/// The changelist number that contained the data
		/// </summary>
		[Obsolete("Use CommitId instead")]
		public int Change
		{
			get => _change ?? _commitId?.GetPerforceChangeOrMinusOne() ?? 0;
			set => _change = value;
		}
		int? _change;

		/// <summary>
		/// The changelist number that contained the data
		/// </summary>
		public CommitIdWithOrder CommitId
		{
			get => _commitId ?? CommitIdWithOrder.FromPerforceChange(_change) ?? CommitIdWithOrder.Empty;
			set => _commitId = value;
		}
		CommitIdWithOrder? _commitId;

		/// <summary>
		/// Key used to identify the particular data
		/// </summary>
		public string Key { get; set; } = String.Empty;

		/// <summary>
		/// The data stored for this test
		/// </summary>
		public Dictionary<string, object> Data { get; set; } = new Dictionary<string, object>();
	}

	/// <summary>
	/// A test emvironment running in a stream
	/// </summary>
	public class GetTestMetaResponse
	{
		/// <summary>
		/// Meta unique id for environment 
		/// </summary>
		public string Id { get; set; } = String.Empty;

		/// <summary>
		/// The platforms in the environment
		/// </summary>
		public List<string> Platforms { get; set; } = new List<string>();

		/// <summary>
		/// The build configurations being tested
		/// </summary>
		public List<string> Configurations { get; set; } = new List<string>();

		/// <summary>
		/// The build targets being tested
		/// </summary>
		public List<string> BuildTargets { get; set; } = new List<string>();

		/// <summary>
		/// The test project name
		/// </summary>	
		public string ProjectName { get; set; } = String.Empty;

		/// <summary>
		/// The rendering hardware interface being used with the test
		/// </summary>
		public string RHI { get; set; } = String.Empty;

		/// <summary>
		/// The varation of the test meta data, for example address sanitizing
		/// </summary>
		public string Variation { get; set; } = String.Empty;
	}

	/// <summary>
	/// A test that runs in a stream
	/// </summary>
	public class GetTestResponse
	{
		/// <summary>
		/// The id of the test
		/// </summary>
		public string Id { get; set; } = String.Empty;

		/// <summary>
		/// The name of the test 
		/// </summary>
		public string Name { get; set; } = String.Empty;

		/// <summary>
		/// The name of the test 
		/// </summary>
		public string? DisplayName { get; set; }

		/// <summary>
		/// The name of the test suite
		/// </summary>
		public string? SuiteName { get; set; }

		/// <summary>
		/// The meta data the test runs on
		/// </summary>
		public List<string> Metadata { get; set; } = new List<string>();
	}

	/// <summary>
	/// Get tests request
	/// </summary>
	public class GetTestsRequest
	{
		/// <summary>
		/// Test ids to get
		/// </summary>
		public List<string> TestIds { get; set; } = new List<string>();
	}

	/// <summary>
	/// A test suite that runs in a stream, contain subtests
	/// </summary>
	public class GetTestSuiteResponse
	{
		/// <summary>
		/// The id of the suite
		/// </summary>	
		public string Id { get; set; } = String.Empty;

		/// <summary>
		/// The name of the test suite
		/// </summary>
		public string Name { get; set; } = String.Empty;

		/// <summary>
		/// The meta data the test suite runs on
		/// </summary>
		public List<string> Metadata { get; set; } = new List<string>();
	}

	/// <summary>
	/// Response object describing test results
	/// </summary>
	public class GetTestStreamResponse
	{
		/// <summary>
		/// The stream id
		/// </summary>
		public string StreamId { get; set; } = String.Empty;

		/// <summary>
		/// Individual tests which run in the stream
		/// </summary>
		public List<GetTestResponse> Tests { get; set; } = new List<GetTestResponse>();

		/// <summary>
		/// Test suites that run in the stream
		/// </summary>
		public List<GetTestSuiteResponse> TestSuites { get; set; } = new List<GetTestSuiteResponse>();

		/// <summary>
		/// Test suites that run in the stream
		/// </summary>
		public List<GetTestMetaResponse> TestMetadata { get; set; } = new List<GetTestMetaResponse>();
	}

	/// <summary>
	/// Suite test data
	/// </summary>
	public class GetSuiteTestDataResponse
	{
		/// <summary>
		/// The test id
		/// </summary>
		public string TestId { get; set; } = String.Empty;

		/// <summary>
		/// The ourcome of the suite test
		/// </summary>
		public TestOutcome Outcome { get; set; }

		/// <summary>
		/// How long the suite test ran
		/// </summary>
		public TimeSpan Duration { get; set; }

		/// <summary>
		/// Test UID for looking up in test details
		/// </summary>
		public string UID { get; set; } =String.Empty;

		/// <summary>
		/// The number of test warnings generated
		/// </summary>
		public int? WarningCount { get; set; }

		/// <summary>
		/// The number of test errors generated
		/// </summary>
		public int? ErrorCount { get; set; }
	}

	/// <summary>
	/// Test details
	/// </summary>
	public class GetTestDataDetailsResponse
	{
		/// <summary>
		/// The corresponding test ref
		/// </summary>
		public string Id { get; set; } = String.Empty;

		/// <summary>
		/// The test documents for this ref
		/// </summary>
		public List<string> TestDataIds { get; set; } = new List<string>();

		/// <summary>
		/// Suite test data
		/// </summary>		
		public List<GetSuiteTestDataResponse>? SuiteTests { get; set; }
	}

	/// <summary>
	/// Data ref 
	/// </summary>
	public class GetTestDataRefResponse
	{
		/// <summary>
		/// The test ref id
		/// </summary>
		public string Id { get; set; } = String.Empty;

		/// <summary>
		/// The associated stream
		/// </summary>
		public string StreamId { get; set; } = String.Empty;

		/// <summary>
		/// The associated job id
		/// </summary>
		public string? JobId { get; set; }

		/// <summary>
		/// The associated step id
		/// </summary>
		public string? StepId { get; set; }

		/// <summary>
		/// How long the test ran
		/// </summary>
		public TimeSpan Duration { get; set; }

		/// <summary>
		/// The build changelist upon which the test ran, may not correspond to the job changelist
		/// </summary>
		[Obsolete("Use BuildCommitId instead")]
		public int BuildChangeList
		{
			get => _buildChangeList ?? _buildCommitId?.GetPerforceChangeOrMinusOne() ?? 0;
			set => _buildChangeList = value;
		}
		int? _buildChangeList;

#pragma warning disable CS0618 // Type or member is obsolete
		/// <summary>
		/// The build changelist upon which the test ran, may not correspond to the job changelist
		/// </summary>
		public CommitId BuildCommitId
		{
			get => _buildCommitId ?? CommitId.FromPerforceChange(_buildChangeList) ?? CommitId.Empty;
			set => _buildCommitId = value;
		}
#pragma warning restore CS0618 // Type or member is obsolete

		CommitId? _buildCommitId;

		/// <summary>
		/// The platform the test ran on 
		/// </summary>
		public string MetaId { get; set; } = String.Empty;

		/// <summary>
		/// The test id in stream
		/// </summary>
		public string? TestId { get; set; }

		/// <summary>
		/// The outcome of the test
		/// </summary>
		public TestOutcome? Outcome { get; set; }

		/// <summary>
		/// The if of the stream test suite
		/// </summary>
		public string? SuiteId { get; set; }

		/// <summary>
		/// Suite tests skipped
		/// </summary>
		public int? SuiteSkipCount { get; set; }

		/// <summary>
		/// Suite test warnings
		/// </summary>
		public int? SuiteWarningCount { get; set; }

		/// <summary>
		/// Suite test errors
		/// </summary>
		public int? SuiteErrorCount { get; set; }

		/// <summary>
		/// Suite test successes
		/// </summary>
		public int? SuiteSuccessCount { get; set; }
	}
}

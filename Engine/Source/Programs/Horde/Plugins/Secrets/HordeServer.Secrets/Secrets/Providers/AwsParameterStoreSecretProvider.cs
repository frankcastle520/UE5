// Copyright Epic Games, Inc. All Rights Reserved.

using Amazon.Extensions.NETCore.Setup;
using Amazon.SimpleSystemsManagement;
using Amazon.SimpleSystemsManagement.Model;

namespace HordeServer.Secrets.Providers
{
	/// <summary>
	/// Fetches secrets from the AWS parameter store
	/// </summary>
	public class AwsParameterStoreSecretProvider : ISecretProvider
	{
		/// <inheritdoc/>
		public string Name => "AwsParameterStore";

		readonly IAmazonSimpleSystemsManagement _systemsManagement;

		/// <summary>
		/// Constructor
		/// </summary>
		public AwsParameterStoreSecretProvider()
		{
			AWSOptions awsOptions = new AWSOptions();
			_systemsManagement = awsOptions.CreateServiceClient<IAmazonSimpleSystemsManagement>();
		}

		/// <inheritdoc/>
		public async Task<string> GetSecretAsync(string path, CancellationToken cancellationToken)
		{
			GetParameterResponse response = await _systemsManagement.GetParameterAsync(new GetParameterRequest { Name = path, WithDecryption = true }, cancellationToken);
			if (response.Parameter == null)
			{
				throw new InvalidOperationException($"Unable to fetch secret '{path}' from AWS parameter store ({response.HttpStatusCode})");
			}
			return response.Parameter.Value;
		}
	}
}

// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Secrets
{
	class HttpSecretCollection : ISecretCollection
	{
		class Secret : ISecret
		{
			readonly GetSecretResponse _response;

			public SecretId Id => _response.Id;
			public IReadOnlyDictionary<string, string> Data => _response.Data;

			public Secret(GetSecretResponse response)
				=> _response = response;
		}

		readonly IHordeClient _hordeClient;

		/// <summary>
		/// Constructor
		/// </summary>
		public HttpSecretCollection(IHordeClient hordeClient)
			=> _hordeClient = hordeClient;

		/// <inheritdoc/>
		public async Task<ISecret?> GetAsync(SecretId secretId, CancellationToken cancellationToken)
		{
			HordeHttpClient hordeHttpClient = _hordeClient.CreateHttpClient();
			GetSecretResponse response = await hordeHttpClient.GetSecretAsync(secretId, cancellationToken);
			return new Secret(response);
		}
	}
}

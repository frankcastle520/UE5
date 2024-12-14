// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Agents;
using HordeServer.Agents;
using Microsoft.AspNetCore.Mvc;

namespace HordeServer.Tests.Agents;

[TestClass]
public class AgentControllerDbTest : BuildTestSetup
{
	[TestMethod]
	public async Task UpdateAgentAsync()
	{
		Fixture fixture = await CreateFixtureAsync();
		IAgent fixtureAgent = fixture.Agent1;

		ActionResult<object> obj = await AgentsController.GetAgentAsync(fixtureAgent.Id);
		GetAgentResponse getRes = (obj.Value as GetAgentResponse)!;
		Assert.AreEqual(fixture!.Agent1Name.ToUpper(), getRes.Name);
		Assert.IsNull(getRes.Comment);

		UpdateAgentRequest updateReq = new(Comment: "foo bar baz");
		await AgentsController.UpdateAgentAsync(fixtureAgent.Id, updateReq);

		obj = await AgentsController.GetAgentAsync(fixtureAgent.Id);
		getRes = (obj.Value as GetAgentResponse)!;
		Assert.AreEqual("foo bar baz", getRes.Comment);
	}
}
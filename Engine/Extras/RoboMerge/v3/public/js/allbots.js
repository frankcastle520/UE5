// Copyright Epic Games, Inc. All Rights Reserved.
"use strict"

var data = undefined;
var botSelector = new BotSelector('allBotsBotselect', 'checkboxes');

function updateLink()
{
    let a = document.getElementById("share-url");
    if (a === undefined) return;

    let url = createUrlFromParameters(false);

    a.href = url;

    if (url.length > 32)
    {
        a.innerHTML = url.substring(0, 29) + "...";
    }
    else
    {
        a.innerHTML = url;
    }
    
    a.title = url;
}

function populateControlPanel(bots, allBots)
{
    const option = parseOptions(location.search);

    let hideDisconnected = document.getElementById('hideDisconnected');
    hideDisconnected.checked = option.hideDisconnected;
    hideDisconnected.addEventListener('change', controlPanelChanged);

    let noGroups = document.getElementById('noGroups');
    noGroups.checked = option.noGroups;
    noGroups.addEventListener('change', controlPanelChanged);
    
    let showOnlyForced = document.getElementById('showOnlyForced');
    showOnlyForced.checked = option.showOnlyForced;
    showOnlyForced.addEventListener('change', controlPanelChanged);

    botSelector.botselectInit(bots, allBots, controlPanelChanged)
    updateLink()
}

function controlPanelChanged()
{
    if (data === undefined ) return;
	
	let options = new FlowOptions();
    
    const hideDisconnected = document.getElementById('hideDisconnected');
	options.hideDisconnected = hideDisconnected.checked;

    const noGroups = document.getElementById('noGroups');
    options.noGroups = noGroups.checked;

    const showOnlyForced = document.getElementById('showOnlyForced');
	options.showOnlyForced = showOnlyForced.checked;

    let selectedBots = botSelector.getBotsSelection();

	$('#graph').html("");
	$('#graph').append(showFlowGraph(
        data.branches,
        { botNames: selectedBots, ...options }, 
        false)
    );
    updateLink()   
}

function clearBotsSelection()
{
    botSelector.clearBotsSelection();

    controlPanelChanged();
}

function createUrlFromParameters(reload)
{
    if (data === undefined ) return;
	
    let args = [];

    const hideDisconnected = document.getElementById('hideDisconnected');
	if (hideDisconnected.checked)
    {
        args.push("hideDisconnected");
    }

    const noGroups = document.getElementById('noGroups');
    if (noGroups.checked)
    {
        args.push("noGroups");
    }

    const showOnlyForced = document.getElementById('showOnlyForced');
    if (showOnlyForced.checked)
    {
        args.push("showOnlyForced");
    }

    let selectedBots = botSelector.getBotsSelection();
    if(selectedBots.length > 0)
    {
        args.push(`bots=${selectedBots.join(",")}`);    
    }

    if (reload)
    {
        args.push("reload");
    }

    let url = `${window.location.origin}${window.location.pathname}`;

    if (args.length > 0)
    {
        url += `?${args.join('&')}`;
    }

    return url;
}

function reloadData()
{
    let url = createUrlFromParameters(true);
    window.location.href = url;
}

function copyURL()
{
    let url = createUrlFromParameters(false);
    navigator.clipboard.writeText(url);
}
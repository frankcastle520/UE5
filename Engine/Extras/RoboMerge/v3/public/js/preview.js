// Copyright Epic Games, Inc. All Rights Reserved.
"use strict"

var data = undefined;
var botSelector = new BotSelector('previewBotselect', 'checkboxes');

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
		data.allBranches,
		{ botNames: selectedBots, ...options},
		false)
	);
	updateLink();
}

function clearBotsSelection()
{
	botSelector.clearBotsSelection();

	controlPanelChanged();
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

	if (allBots.length > 1)
	{
		botSelector.botselectInit(bots, allBots, controlPanelChanged)
		$('#botsselection').show();
	}
	updateLink();
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
        args.push(`selectedbots=${selectedBots.join(",")}`);    
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

function copyURL()
{
    let url = createUrlFromParameters(false);
    navigator.clipboard.writeText(url);
}

function reloadData()
{
	let url = createUrlFromParameters(true);
	window.location.href = url;
}


function doit(query)
{
	const clgroups = query.match(/cl=([^&]*)/);

	if (clgroups.length != 2)
	{
		const $errorPanel = $('#error-panel');
		const errorMsg = 'Missing CL # in request.'
		$('pre', $errorPanel).text(errorMsg);
		
		$errorPanel.show();

		return;	
	}

	let args = `cl=${clgroups[1]}`

	const botgroups = query.match(/bot=([^&]*)/);
	if (botgroups && botgroups.length > 1)
	{
		args += `&bot=${botgroups[1]}`
	}

	$.ajax({
		url: '/preview',
		type: 'get',
		data: args,
		dataType: 'json',
		success: requestData => {
			// save a copy of the data
			data = requestData;

			const bots = location.search.match(/selectedbots=([^&]*)/);

			const botNames = new Set(data.allBranches.map(b => b.bot));
			populateControlPanel(bots ? bots[1].split(',') : [], Array.from(botNames));

			const reload = !!location.search.match(/reload/i);
			if (reload)
			{
				document.getElementById('controlpanel').open = true;
			}

			$('#graph').append(showFlowGraph(
				data.allBranches,
				{ botNames: [], ...parseOptions(location.search)},
				false)
		);
			$('.bots').html([...botNames].map(s => `<tt>${s.toLowerCase()}</tt>`).join(', '))
			$('#success-panel').show();
			$('#control-panel').show();
			$('#controlpanel').show();
		},
		error: error => {
			const $errorPanel = $('#error-panel');
			const errorMsg = error.responseText
				? error.responseText.replace(/\t/g, '    ')
				: 'Internal error: ' + error.message;
			$('pre', $errorPanel).text(errorMsg);
			$errorPanel.show();
		}
	})
}

window.doPreview = doit;

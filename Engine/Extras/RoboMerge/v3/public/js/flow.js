// Copyright Epic Games, Inc. All Rights Reserved.

class FlowOptions{
    showOnlyForced = false;
    noGroups = false;
    hideDisconnected = false;
}


function setDefault(map, key, def) {
    const val = map.get(key);
    if (val) {
        return val;
    }
    map.set(key, def);
    return def;
}
function renderGraph(src) {
    const hpccWasm = window["@hpcc-js/wasm"];
    return hpccWasm.graphviz.layout(src, "svg", "dot");
}

function parseOptions(search) {

    let options = new FlowOptions();
    
    options.showOnlyForced = !!search.match(/showOnlyForced/i);
    options.noGroups = !!search.match(/noGroups/i);
    options.hideDisconnected = !!search.match(/hideDisconnected/i);

    return options;

}

function makeGraph(data, args) {
    const options = args;
    const singleBotName = args.singleBotName;
    if (singleBotName) {
        return (new Graph(data, options)).singleBot(singleBotName);
    }
    const botNames = args.botNames;
    if (botNames.length > 0) {
        options.botsToShow = botNames.map(s => s.toUpperCase());
    }
    return (new Graph(data, options)).allBots();
}

function getAllBots(data) {
    let allBots = new Set();

    for (branch of data) {
        allBots.add(branch.bot);
    }

    return allBots;
}

function showFlowGraph(data, args, addlegend) {
    const lines = makeGraph(data, args);
    const graphContainer = $('<div class="clearfix">');
    const flowGraph = $('<div class="flow-graph" style="display:inline-block;">').appendTo(graphContainer);
    flowGraph.append($('<div>').css('text-align', 'center').text("Building graph..."));
    renderGraph(lines.join('\n'))
        .then(svg => {

        if(addlegend)
        {
            $('#graph-key-template')
            .clone()
            .removeAttr('id')
            .css('display', 'inline-block')
            .appendTo(graphContainer);
        }
        

        $('#graph-loading-text').hide();
        const span = $('<span>').css('margin', 'auto').html(svg);
        const svgEl = $('svg', span).addClass('branch-graph').removeAttr('width');
        // scale graph to 70% of default size
        const height = Math.round(parseInt(svgEl.attr('height')) * .7);
        svgEl.attr('height', height + 'px').css('vertical-align', 'top');
        flowGraph.empty();
        flowGraph.append(span);
    });
    return graphContainer;
}

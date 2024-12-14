// Copyright Epic Games, Inc. All Rights Reserved.

class BotSelector
{
    /*
        id: unique html id for the component
        display: one of ['dropbox', 'checkboxes]
    */
    constructor(id, display)
    {
        this.id = id;
        this.display = display;
    }
    
    botselectInit(selected, bots, handler)
    {
        const div = document.getElementById(this.id);
        if(div === undefined) return;

        if(this.display === 'dropbox')
        {
            const html = this.#initBotListDropBox(selected, bots);

            var parent = div.parentNode;
            parent.removeChild(div);
            parent.innerHTML = html;
    
            this.#initCheckboxHandlers(bots, handler);
            this.#initEscapeHandler();
            document.addEventListener('click', this.#clickHandler.bind(this));
        }

        if(this.display === 'checkboxes')
        {
            const html = this.#initBotListCheckBoxes(selected, bots);

            var parent = div.parentNode;
            parent.removeChild(div);
            parent.innerHTML = html;

            this.#initCheckboxHandlers(bots, handler);
        }
    }

    getBotsSelection()
    {
        let result = [];
        for( let item of document.getElementsByClassName("botselectitem"))
        {
            if (item.id.startsWith(`${this.id}_`) && item.checked)
            {
                result.push(item.value);
            }
        }

        return result;
    }

    clearBotsSelection()
    {
        for( let item of document.getElementsByClassName("botselectitem"))
        {
            if (item.id.startsWith(`${this.id}_`) && item.checked)
            {
                item.checked = false;
            }
        }
    }

    #initBotListCheckBoxes(selected, bots)
    {
        if (this.id === undefined) return;
        if (this.display === undefined) return;
        if (selected === undefined) return;
        if (bots === undefined) return;

        // maximum 3 cols
        const groupSize = Math.ceil(bots.length / 3);

        let groups = new Map();
        for (let i = 0; i < bots.length / groupSize; i++)
        {
            groups.set(i, []);
        }

        for (let i = 0; i < bots.length; i++)
        {
            groups.get(Math.floor(i/groupSize)).push(i);
        }

        let checkboxes = '';

        const sortedBots = bots.sort();
        for(const group of groups.keys())
        {
            checkboxes += '<div class="col-3">';
            for(const col of groups.get(group))
            {
                const bot = sortedBots[col];
                checkboxes += '<div class="row form-check">';
                checkboxes += '<div class="col">';
                checkboxes += `<input class="botselectitem form-check-input" type="checkbox" id="${this.id}_${bot}" name="${bot}" value="${bot}" ${selected.includes(bot)?'checked':''}>`;
                checkboxes += `<label for="${this.id}_${bot}">${bot}</label>`
                checkboxes += '</div>';    
                checkboxes += '</div>';    
            }

            checkboxes += '</div>';
        }
        return checkboxes;    
    }

    #initBotListDropBox(selected, bots)
    {
        if (this.id === undefined) return;
        if (this.display === undefined) return;
        if (selected === undefined) return;
        if (bots === undefined) return;

        let items = []
        for( let bot of bots.sort())
        {
            items.push(`<li><label for="${this.id}_${bot}">${bot}<input class="botselectitem ml-1" type="checkbox" id="${this.id}_${bot}" name="${bot}" value="${bot}" ${selected.includes(bot)?'checked':''}></label></li>`);
        }

        return `<details class="botselect" id=${this.id}>
            <summary>Select bots</summary>
            <form>
            <fieldset>
                <legend>Bots</legend>
                <ul id="${this.id}_botselectlist">
                ${items.join('\n')}
                </ul>
            </fieldset>
            </form>
        </details>
        `;    
    }

    #initEscapeHandler()
    {
        const multiselectElement = document.getElementById(this.id);

        const handleEscape = (event) => {
            const eventTarget = event.target;
            let key;
            
            if (typeof Object.getOwnPropertyDescriptor(KeyboardEvent.prototype, 'code') === 'object') {
                key = event.key;
            } else if (typeof Object.getOwnPropertyDescriptor(KeyboardEvent.prototype, 'key') === 'object') {
                key = event.key;
            }
            
            if (key !== 'Escape') {
                return;
            }
            
            const detailsElement = eventTarget.closest('details');
            
            detailsElement.removeAttribute('open');
            //detailsElement.querySelector('summary').focus();
        };
        
        multiselectElement.addEventListener('keydown', handleEscape);
    }

    #initCheckboxHandlers(bots, handler)
    {
        for( let bot of bots)
        {
            let checkbox = document.getElementById(`${this.id}_${bot}`);
            checkbox.addEventListener('change', handler);
        }
    }

    #clickHandler(e)
    {
        const element = document.getElementById(this.id);
        if (element !== e.target && !element.contains(e.target))
        {
            element.removeAttribute('open');
        }
    }

}



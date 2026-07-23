<script lang="ts">
	import { Click, Converter } from "$lib";
	import ClickView from "./ClickView.svelte";
    import { Plus, Trash2 } from "lucide-svelte"

    export let click: Click;
    export let i: number;
    export let converter: Converter;
    export let currentIdx: number;
    export let refreshClicks: () => void;
    export let refreshInputCount: () => void;
    export let setCurrentlyHoveringOverIdx: (idx: number) => void;
    export let currentlyHoveringOverIdx: number;

    let shouldShowClickAdderBelow = false;
    let shouldShowClickAdderAbove = false;
    let isHoveringOverExtension = false;

    $: isHoveringOverSelf = currentlyHoveringOverIdx === i;

    function onEditFrame(event: Event) {
        const target = event.target as HTMLInputElement;
        const idx = parseInt(target.id.split('-')[1]);

        const actualIdx = currentIdx + idx;

        converter.replace_frame_at(actualIdx, parseInt(target.value));

        refreshClicks();
        //refreshClicks();
    }

    function onToggleClick(event: Event) {
        const target = event.target as HTMLElement;
        //console.log(target.id)
        //console.log(target.innerHTML)
        const idx = parseInt(target.id.split('-')[1]);
        const isP2 = target.id.split('-')[2] === '2';

        const actualIdx = currentIdx + idx;

        //console.log(target.id, actualIdx, isP2);

        converter.toggle_click_at(actualIdx, isP2);

        refreshClicks();
    }

    function onMouseMove(event: MouseEvent) {
        const target = event.target as HTMLElement;

        // const extensions = document.querySelectorAll('.extension');
        // let extensionsFullHeight = Array.from(extensions).map(e => e.getBoundingClientRect().height).reduce((a, b) => a + b, 0);

        // const extTexts = document.querySelectorAll('.exttext');
        // extensionsFullHeight += Array.from(extTexts).map(e => e.getBoundingClientRect().height).reduce((a, b) => a + b, 0);

        // console.log(extensionsFullHeight);
        
        // console.log(extensionsFullHeight);
        // console.log(target.getBoundingClientRect().top, target.getBoundingClientRect().bottom);
        // console.log(target.offsetHeight);

        const heightMarker = document.getElementById(`height-marker-${i}`);

        const top = heightMarker?.getBoundingClientRect().top ?? 0;
        const bottom = heightMarker?.getBoundingClientRect().bottom ?? 0;

        const height = heightMarker?.getBoundingClientRect().height ?? 0;

        const isNearTop = Math.abs(event.clientY - top) < (height / 10);
        const isNearBottom = Math.abs(event.clientY - bottom) < (height / 10);

        if (isNearTop) {
            shouldShowClickAdderAbove = true;
            shouldShowClickAdderBelow = false;
        } else if (isNearBottom) {
            shouldShowClickAdderBelow = true;
            shouldShowClickAdderAbove = false;
        } else {
            shouldShowClickAdderAbove = false;
            shouldShowClickAdderBelow = false;
        }
    }

    function onMouseEnter(event: MouseEvent) {
        setCurrentlyHoveringOverIdx(i);
    }

    function onMouseEnterExtension(event: MouseEvent) {
        isHoveringOverExtension = true;
    }

    function onMouseLeaveExtension(event: MouseEvent) {
        isHoveringOverExtension = false;
    }

    function insertClickAbove(event: Event) {
        converter.insert_empty_at(currentIdx + i, click.frame);

        refreshClicks();
        refreshInputCount();
    }

    function insertClickBelow(event: Event) {
        converter.insert_empty_at(currentIdx + i + 1, click.frame);

        refreshClicks();
        refreshInputCount();
    }

    function deleteClick(event: Event) {
        converter.remove_at(currentIdx + i);

        refreshClicks();
        refreshInputCount();
    }

    function onMouseLeave(event: MouseEvent) {
        // if (isHoveringOverExtension) {
        //     return;
        // }

        shouldShowClickAdderAbove = false;
        shouldShowClickAdderBelow = false;

        isHoveringOverSelf = false;
    }
</script>

<tr on:mousemove={onMouseMove} on:mouseenter={onMouseEnter} on:mouseleave={onMouseLeave} id={`click-row-${i}`} class={`relative select-none click-row flex text-white text-center w-full min-h-[3.5rem] h-[3.5rem] flex items-center border-b border-neutral-700/30 hover:bg-neutral-800`} style={`
`}>
    {#if shouldShowClickAdderAbove}
    <div class="extension absolute -top-1 left-0 h-1 w-full bg-neutral-800 z-10">
        <div class="relative flex items-center w-full justify-center">
            <div class="exttext absolute left-0 flex w-full justify-center -top-3">
                <div on:click={insertClickAbove} class="cursor-pointer bg-neutral-800 hover:bg-neutral-700 px-1 text-green-500 py-1 rounded-full flex justify-center items-center text-neutral-200 text-xs w-6 h-6 text-center align-middle">
                    <Plus size="24" />
                </div>
            </div>
        </div>
    </div>
    {/if}
    {#if isHoveringOverSelf}
    <div class="absolute top-1/2 left-0 h-1 w-min z-20">
        <div class="relative flex items-center w-min justify-center">
            <div class="exttext absolute left-0 flex w-min">
                <div on:click={deleteClick} class="cursor-pointer mx-4 text-red-500 bg-neutral-800 hover:bg-neutral-700 px-2 py-2 flex justify-center items-center rounded-full text-neutral-200 text-xs w-8 h-8 text-center align-middle">
                    <Trash2 size="24" />
                </div>
            </div>
        </div>
    </div>
    {/if}
    <td class="text-center w-[30%]" id={`height-marker-${i}`}>
        <input id={`frame-${i}`} class="w-full text-center bg-transparent focus:bg-neutral-950 py-2 rounded-md px-3 mx-3 focus:outline-none" bind:value={click.frame} on:input={onEditFrame}>
    </td>
    <td class="cursor-pointer text-center w-[35%] flex justify-center"><ClickView id={`toggle-${i}-1`} onClick={onToggleClick} click={click.p1} /></td>
    <td class="cursor-pointer text-center w-[35%] flex justify-center"><ClickView id={`toggle-${i}-2`} onClick={onToggleClick} click={click.p2} /></td>
    {#if shouldShowClickAdderBelow}
    <div class="extension absolute -bottom-1 left-0 h-1 w-full bg-neutral-800 z-10">
        <div class="relative flex items-center w-full justify-center">
            <div class="exttext absolute left-0 flex w-full justify-center -bottom-4">
                <div on:click={insertClickBelow} class="cursor-pointer bg-neutral-800 hover:bg-neutral-700 px-1 text-green-500 py-1 rounded-full flex justify-center items-center text-neutral-200 text-xs w-6 h-6 text-center align-middle">
                    <Plus size="24" />
                </div>
            </div>
        </div>
    </div>
    {/if}
</tr>
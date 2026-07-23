
<script lang="ts">
    import { Click, Converter } from '$lib';
	import { onMount, tick } from 'svelte';
	import ClickView from './ClickView.svelte';
	import ClickRow from './ClickRow.svelte';

    export let converter: Converter

    export let currentIdx: number;
    export let pageSize: number;

    export let setCurrentIdx: (idx: number) => void;
    export let setPageSize: (size: number) => void;
    export let settings: Settings;

    let top = 0;
    let bottom = 0;
    let clickList: HTMLElement;

    export let clicks: Click[];
    let rowHeight: number;

    let currentlyMoving: HTMLElement;
    let currentlyMovingIdx: number;
    export let refreshInputCount: () => void;
    export let refreshClicks: () => void;


    let viewport: HTMLElement;

    // async function refreshTable() {
    //     const { scrollTop } = viewport;


    // }

    async function handleScroll(event: Event) {
        const { scrollTop } = viewport;

        const previousIdx = currentIdx;

        let y = 0;

        // while (i < converter.length()) {
        //     if (y + rowHeight > scrollTop) {
        //         setCurrentIdx(i);
        //         top = y;
        //         break;
        //     }

        //     y += rowHeight;
        //     i++;
        // }

        let i = Math.ceil(scrollTop / rowHeight);
        if (i < converter.length()) {
            setCurrentIdx(i);
            top = i * rowHeight;
            y = top;
        } else {
            i = converter.length();
            y  = i * rowHeight;
        }


        while (i < converter.length()) {
            y += rowHeight;
            i++;

            if (y > scrollTop + viewport.offsetHeight) {
                break;
            }
        }

        setPageSize(i - currentIdx);

        bottom = (converter.length() - i) * rowHeight;

        refreshClicks();

        //console.log(top, bottom, currentIdx, pageSize, y);
    }

    // function onDragStart(event: DragEvent) {
    //     if (event.dataTransfer) {
    //         event.dataTransfer!.effectAllowed = 'move';
    //         event.dataTransfer!.setData('text/plain', null);

    //         currentlyMoving = event.target as HTMLElement;
            
    //     }
    // }

    // function onDragOver(event: DragEvent) {
    //     event.preventDefault();

    //     if (event.dataTransfer) {
    //         event
    //     }
    // }

    let currentlyHoveringOverIdx: number;
    function setCurrentlyHoveringOverIdx(idx: number) {
        currentlyHoveringOverIdx = idx;
    }

    onMount(async () => {
        refreshClicks();
        await tick();
        rowHeight = clickList.querySelector('.click-row')?.clientHeight ?? 0;
    })
</script>

{#if settings.plainTextEditor}
<h1 class="text-5xl text-white">not implemented LMFAO</h1>
{:else}
<table bind:this={viewport} id="click-table" on:scroll={handleScroll} class="w-[600px] h-[400px] relative bg-neutral-900 rounded-lg table-fixed border-collapse flex flex-col overflow-y-scroll justify-start scrollbar">
    <thead class="text-white font-bold px-2 text-center sticky w-full z-10 top-0 bg-neutral-900/50 backdrop-blur-md">
        <tr class="flex w-full text-xl border-b border-neutral-700 py-4">
            <th class="w-[30%]">Frame</th>
            <th class="w-[35%]">Player 1</th>
            <th class="w-[35%]">Player 2</th>
        </tr>
    </thead>
    <tbody bind:this={clickList} class="w-full text-xl px-2 flex flex-col" style={
    `
        padding-top: ${top}px;
        padding-bottom: ${bottom}px;
    `
    }>
        {#each clicks as click, i}
            <ClickRow {click} {i} {converter} {currentIdx} {refreshClicks} {refreshInputCount} {currentlyHoveringOverIdx} {setCurrentlyHoveringOverIdx} />
        {/each}
    </tbody>
</table>
{/if}


<style>
    /* .scrollbar::-webkit-scrollbar {
        display: none;
    }

    .scrollbar {
        -ms-overflow-style: none;
        scrollbar-width: none;
    } */

    ::-webkit-scrollbar-track {
        background-color: transparent;
    }

    ::-webkit-scrollbar-thumb {
        background-color: rgba(255, 255, 255, 0.2);
    }
</style>
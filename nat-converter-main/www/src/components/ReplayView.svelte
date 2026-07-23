<script lang="ts">
    import { Converter, Click, ClickType, formats, Format, GameVersion } from '$lib';
	import { writable, type Writable } from 'svelte/store';
	import ClickTable from './ClickTable.svelte';
    import { ArrowDownWideNarrow, ArrowUpDown, ChevronsDown, RefreshCcw, Trash2 } from "lucide-svelte";

    export let converter: Converter;
    export let replayData: {
        fps: number;
        format: number;
        length: number;
    }
    export let replayName: string;
    export let settings: Settings;

    $: replayFormatPretty = formats[(replayData?.format ?? 0) as keyof typeof formats][0];

    let selectedSaveFormat = Format.PlainText;

    let currentIdx = 0;
    let pageSize = 20;
    let clicks: Click[] = [];

    function setCurrentIdx(idx: number) {
        if (idx < 0) {
            currentIdx = 0;
        } else if (idx > converter.length() - pageSize) {
            currentIdx = converter.length() - pageSize;
        } else {
            currentIdx = idx;
        }
    }

    function setPageSize(size: number) {
        if (size < 1) {
            size = 1;
        } else if (size > 20) {
            size = 20;
        }
        pageSize = size;
    }

    function refreshClicks() {
        console.log('loading clicks at', currentIdx, pageSize);

        if (currentIdx + pageSize > converter.length()) {
            setCurrentIdx(converter.length() - pageSize);
        }

        clicks = converter.clicks_at_batch(currentIdx, pageSize);
        //console.log('Loaded clicks at', currentIdx, pageSize);
    }


    function saveReplay() {
        const data = converter.save(selectedSaveFormat);

        const blob = new Blob([data], {type: 'application/octet-stream'});
        const url = URL.createObjectURL(blob);

        const link = document.createElement('a');
        link.href = url;
        link.download = `${replayName}.${formats[selectedSaveFormat as keyof typeof formats][1]}`;
        link.click();

        URL.revokeObjectURL(url);

        document.body.removeChild(link);
    }

    let allowedFormats: { [key: number]: [string, string, number, boolean] } = {};
    $: refreshFormats();
    $: if (settings.crossVersionConverting || settings.legacyFormats) refreshFormats();
    $: if (!settings.crossVersionConverting && !settings.legacyFormats) refreshFormats();

    function refreshFormats() {
        if (formats[replayData.format][2] === GameVersion.Any) {
            allowedFormats = formats;
            return;
        }

        // Filter out formats that are not compatible with the game version
        const entries = Object.entries(formats);
        const allowed = entries.filter(([idx, [format, pretty, version, legacy]]) => {
            if (settings.crossVersionConverting) {
                return settings.legacyFormats || !legacy;
            }

            let isAllowed = version === formats[replayData.format][2] || version === GameVersion.Any;

            if (!settings.legacyFormats) {
                isAllowed = isAllowed && !legacy;
            }

            return isAllowed;
        })

        allowedFormats = Object.fromEntries(allowed);
    }

    function handleFPSChange(event: Event) {
        const target = event.target as HTMLInputElement;

        if (isNaN(parseInt(target.value))) {
            target.value = "60";
            return;
        }

        converter.set_fps(parseInt(target.value));
        replayData.fps = parseInt(target.value);
    }

    function refreshInputCount() {
        replayData.length = converter.length();
    }

    function cleanMacro() {
        converter.clean();
        refreshClicks();
        refreshInputCount();
    }

    function sortMacro() {
        converter.sort();
        refreshClicks();
    }

    function removeAllPlayer(p2: boolean) {
        converter.remove_all_player_inputs(p2);
        refreshClicks();
        refreshInputCount();
    }

    function flipP1P2() {
        converter.flip_p1_p2();
        refreshClicks();
    }

    function flipUpDown() {
        converter.flip_up_down();
        refreshClicks();
    }

    function checkCPS() {
        converter.check_cps();
    }

    export let offset: number;

    function handleOffsetChange(event: Event) {
        const target = event.target as HTMLInputElement;

        if (isNaN(parseInt(target.value))) {
            target.value = "0";
            return;
        }
    }

    function offsetAll() {
        converter.offset_all_by(offset);
        refreshClicks();
    }
</script>


<div class="flex gap-4">
    <ClickTable converter={converter} {refreshInputCount} {refreshClicks} {currentIdx} {pageSize} {clicks} {setCurrentIdx} {setPageSize} {settings} />
    <div class="flex flex-col text-xl">
        <div class="flex gap-2 py-1">
            <span class="text-neutral-400 font">Format</span>
            <span class="text-white font-bold">{replayFormatPretty}</span>
        </div>
        <div class="flex">
            <span class="text-neutral-400 font py-1">FPS</span>
            <input class="text-white py-1 px-1 mx-1 font-bold bg-transparent focus:bg-neutral-950 rounded-md focus:outline-none" bind:value={replayData.fps} on:change={handleFPSChange} />
        </div>
        <div class="flex gap-2 py-1">
            <span class="text-neutral-400 font">Inputs</span>
            <span class="text-white font-bold">{replayData?.length}</span>
        </div>
        <div class="h-max flex-grow"></div>
        <div class="flex flex-col gap-2">
            <div class="flex gap-2">
                <button on:click={() => removeAllPlayer(false)} class="bg-red-800 text-white rounded-md px-6 py-2 w-fit font-medium hover:bg-red-700 inline-flex gap-2 items-center">
                    Remove all P1
                </button>
                <button on:click={() => removeAllPlayer(true)} class="bg-red-800 text-white rounded-md px-6 py-2 w-fit font-medium hover:bg-red-700 inline-flex gap-2 items-center">
                    Remove all P2
                </button>
            </div>
            <div class="flex gap-2">
                <button on:click={offsetAll} class="bg-amber-800 text-white rounded-md px-6 py-2 w-fit font-medium hover:bg-amber-700 inline-flex gap-2 items-center">
                    <ChevronsDown size="20" /> Offset
                </button>
                <input class="text-white py-1 px-2 font-bold bg-neutral-900 focus:bg-neutral-800 rounded-md focus:outline-none" bind:value={offset} on:change={handleOffsetChange} />
            </div>
            <div class="flex gap-2">
                <button on:click={flipP1P2} class="bg-amber-800 text-white rounded-md px-6 py-2 w-fit font-medium hover:bg-amber-700 inline-flex gap-2 items-center">
                    <RefreshCcw size="20" /> Flip P1/P2
                </button>
                <button on:click={flipUpDown} class="bg-amber-800 text-white rounded-md px-6 py-2 w-fit font-medium hover:bg-amber-700 inline-flex gap-2 items-center">
                    <ArrowUpDown size="20" /> Flip Up/Down
                </button>
            </div>
            <div class="flex gap-2">
                <button on:click={cleanMacro} class="bg-red-800 text-white rounded-md px-6 py-2 w-fit font-medium hover:bg-red-700 inline-flex gap-2 items-center">
                    <Trash2 size="20" /> Clean
                </button>
                <button on:click={sortMacro} class="bg-blue-800 text-white rounded-md px-6 py-2 w-fit font-medium hover:bg-blue-700 inline-flex gap-2 items-center">
                    <ArrowDownWideNarrow size="20" /> Sort
                </button>
                <button on:click={checkCPS} class="bg-green-800 text-white rounded-md px-6 py-2 w-fit font-medium hover:bg-green-700 inline-flex gap-2 items-center">
                    <ArrowDownWideNarrow size="20" /> Check CPS (Console)
                </button>
            </div>
            <div class="flex gap-2">
                <button on:click={saveReplay} class="bg-green-800 text-white rounded-md px-6 py-2 font-medium hover:bg-green-700">Save</button>
                <select bind:value={selectedSaveFormat} class="bg-neutral-800 text-white rounded-md px-6 py-2 font-medium hover:bg-neutral-700">
                    {#each Object.entries(allowedFormats) as [format, pretty]}
                        <option value={format}>{pretty[0]}</option>
                    {/each}
                </select>
            </div>
        </div>
    </div>
</div>

<script lang="ts">
    import { Converter, Format, formats, Click, ClickType } from '$lib';
	import { onMount } from 'svelte';
	import ClickView from '../components/ClickView.svelte';
	import ClickTable from '../components/ClickTable.svelte';
	import ReplayView from '../components/ReplayView.svelte';
	import SettingsMenu from '../components/SettingsMenu.svelte';

    let converter: Converter;

    let filesToLoad: FileList;
    let fileToLoad: File;
    let selectedFormat: number;
    let isLoaded = false;

    let replayData: {
        fps: number;
        format: number;
        length: number;
    } | null;

    let settings: Settings = {
        autoOffset: true,
        legacyFormats: false,
        fancyJson: true,
        plainTextEditor: false,
        crossVersionConverting: false
    }

    $: replayName = fileToLoad?.name.split('.')[0];

    onMount(async () => {
        converter = new Converter();
    });

    function handleDrop(event: DragEvent) {
        event.preventDefault();

        const file = event.dataTransfer?.files[0];

        if (file) {
            fileToLoad = file;
        }
    }

    function handleDragOver(event: DragEvent) {
        event.preventDefault();
    }

    function handleChange(event: Event) {
        const target = event.target as HTMLInputElement;
        fileToLoad = target.files[0];

        // if (fileToLoad) {
        //     handleLoad();
        // }
    }

    async function handleLoad() {
        converter = new Converter();

        const contents = await fileToLoad.arrayBuffer();
        const arr = new Uint8Array(contents);

        converter.load(arr, selectedFormat);
        isLoaded = true;

        replayData = {
            fps: converter.get_fps(),
            format: selectedFormat,
            length: converter.length(),
        }
    }

</script>

<div class="bg-black min-h-screen w-full flex flex-col justify-center items-center gap-4">
    {#if isLoaded && replayData}
        <h1 class="text-white font-bold text-4xl">{replayName}</h1>
        <ReplayView converter={converter} {settings} replayData={replayData} {replayName} />
    {:else}
    <h1 class="text-white font-bold text-4xl">Load file</h1>
    <input on:change={handleChange} type="file" id="replay-input" class="fixed opacity-0 top-0 left-0" bind:files={filesToLoad} />
    <label for="replay-input" class="border-2 cursor-pointer hover:bg-neutral-950 border-dashed border-neutral-600 rounded-md min-w-80 min-h-40 flex flex-col items-center justify-center" on:drop={handleDrop} on:dragover={handleDragOver}>
        <h1 class="text-center text-white font-medium text-xl">
            {#if fileToLoad}
                {fileToLoad.name}
            {:else}
                Drop a replay file here
            {/if}
        </h1>
        <h2 class="text-center text-neutral-400 font-medium text-sm">
            {#if fileToLoad}
                {fileToLoad.size / 1024} KB
            {:else}
                Or choose a file
            {/if}
        </h2>
    </label>
    {#if fileToLoad}
        <div class="flex gap-2">
            <button class="bg-neutral-800 text-white rounded-md px-6 py-2 font-medium hover:bg-neutral-700" on:click={handleLoad}>Load</button>
            <select class="bg-neutral-800 text-white rounded-md px-6 py-2 font-medium hover:bg-neutral-700" bind:value={selectedFormat}>
                {#each Object.entries(formats) as [format, pretty]}
                    <option value={format}>{pretty[0]}</option>
                {/each}
            </select>
        </div>
    {/if}
    {/if}

    <SettingsMenu bind:settings {converter} />
</div>
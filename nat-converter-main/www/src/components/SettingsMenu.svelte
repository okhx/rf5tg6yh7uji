<script lang="ts">
	import type { Converter } from "$lib";
	import { Cog } from "lucide-svelte";
    import { fade, scale } from "svelte/transition";
    import { onMount, tick } from 'svelte';

    export let settings: Settings;
    export let converter: Converter;

    let isOpen = false;

    function toggleOpen() {
        isOpen = !isOpen;
    }

    function close() {
        console.log('blurred');
        isOpen = false;
    }

    function handleChangeLegacyFormats(event: Event) {
        const target = event.target as HTMLInputElement;
        localStorage.setItem('legacyFormats', target.checked.toString());
    }

    function handleChangePlainTextEditor(event: Event) {
        const target = event.target as HTMLInputElement;
        localStorage.setItem('plainTextEditor', target.checked.toString());
    }

    function handleChangeCrossVersionConverting(event: Event) {
        const target = event.target as HTMLInputElement;
        localStorage.setItem('crossVersionConverting', target.checked.toString());
    }

    function handleChangeBeautifiedJson(event: Event) {
        const target = event.target as HTMLInputElement;
        converter.set_setting_beautify_json(target.checked);
        localStorage.setItem('fancyJson', target.checked.toString());
    }

    function handleChangeAutoOffset(event: Event) {
        const target = event.target as HTMLInputElement;
        converter.set_setting_auto_offset(target.checked);
        localStorage.setItem('autoOffset', target.checked.toString());
    }

    onMount(async () => {
        if (localStorage.getItem('autoOffset') === null) {
            localStorage.setItem('autoOffset', 'true');
        }

        if (localStorage.getItem('legacyFormats') === null) {
            localStorage.setItem('legacyFormats', 'false');
        }

        if (localStorage.getItem('fancyJson') === null) {
            localStorage.setItem('fancyJson', 'true');
        }

        if (localStorage.getItem('plainTextEditor') === null) {
            localStorage.setItem('plainTextEditor', 'false');
        }

        if (localStorage.getItem('crossVersionConverting') === null) {
            localStorage.setItem('crossVersionConverting', 'false');
        }

        settings.autoOffset = localStorage.getItem('autoOffset') === 'true';
        settings.legacyFormats = localStorage.getItem('legacyFormats') === 'true';
        settings.fancyJson = localStorage.getItem('fancyJson') === 'true';
        settings.plainTextEditor = localStorage.getItem('plainTextEditor') === 'true';
        settings.crossVersionConverting = localStorage.getItem('crossVersionConverting') === 'true';

        while (!converter) {
            await tick();
        }

        converter.set_setting_auto_offset(settings.autoOffset);
        converter.set_setting_beautify_json(settings.fancyJson);
    })
</script>

<div class="absolute bottom-24 right-24 text-white flex flex-col items-end" on:mouseleave={close}>
    {#if isOpen}
        <div class="bg-neutral-900 text-white z-10 mb-4 rounded-md p-4 relative flex flex-col items-start w-80" transition:fade={{ duration: 100 }}>
            <h1 class="text-xl font-bold">Settings</h1>
            <h2 class="text-neutral-400 font-medium text-sm mt-2">GENERAL</h2>
            <div class="flex gap-2">
                <input on:change={handleChangeAutoOffset} type="checkbox" id="autoOffset" bind:checked={settings.autoOffset} />
                <label for="autoOffset" class="">Auto Offset</label>
            </div>
            <div class="flex gap-2">
                <input on:change={handleChangeLegacyFormats} type="checkbox" id="legacyFormats" bind:checked={settings.legacyFormats} />
                <label for="legacyFormats" class="">Legacy Formats</label>
            </div>
            <div class="flex gap-2">
                <input on:change={handleChangeBeautifiedJson} type="checkbox" id="fancyJson" bind:checked={settings.fancyJson} />
                <label for="fancyJson" class="">Beautified JSON</label>
            </div>
            <h2 class="text-neutral-400 font-medium text-sm mt-2">EXPERIMENTAL</h2>
            <h2 class="text-neutral-500 text-xs my-0.5">WARNING: These features are experimental and may heavily reduce performance or not work as expected.</h2>
            <div class="flex gap-2">
                <input on:change={handleChangeCrossVersionConverting} type="checkbox" id="crossVersionConverting" bind:checked={settings.crossVersionConverting} />
                <label for="crossVersionConverting" class="">Cross-version Converting</label>
            </div>
        </div>
    {/if}
    <button on:click={toggleOpen} class="bg-neutral-900 rounded-full p-3 hover:bg-neutral-800 cursor-pointer">
        <Cog class="w-8 h-8" />
    </button>
</div>

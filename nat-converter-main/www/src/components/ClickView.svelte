<script lang="ts">
    import { ClickType } from '$lib';

    export let click: ClickType;
    export let id: string;
    export let onClick: (event: Event) => void;

    function handleKeyDown(event: KeyboardEvent) {
        if (event.key === 'Enter') {
            onClick(event);
        }
    }
</script>

{#if click !== ClickType.Skip}
<div on:click={onClick} {id} class={`py-1 px-3 rounded-lg w-fit text-lg ${
    click === ClickType.Click ? 'bg-blue-500 hover:bg-blue-500/80' : 'bg-orange-500 hover:bg-orange-500/80'
}`} role="button" tabindex="0" on:keydown={handleKeyDown}>
    {click === ClickType.Click ? 'Down' : 'Up'}
</div>
{:else}
<div on:click={onClick} on:keydown={handleKeyDown} {id} class="py-1 px-3 rounded-lg w-fit text-lg bg-neutral-500/10 hover:bg-neutral-500/20" role="button" tabindex="0">
    -
</div>
{/if}

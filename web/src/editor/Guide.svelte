<script lang="ts" module>
  // The first-run guide (migration Phase 7a): WalkthroughPanel::words() at the
  // panel's own place. The steps are derived in C++ from live state; this
  // card only draws them, and its skip answers through chrome.skipGuide().
  export interface GuideStep {
    text: string;
    done: boolean;
  }
  export interface GuideState {
    shown: boolean;
    title?: string;
    skip?: string;
    steps?: GuideStep[];
    skipRect?: { x: number; y: number; w: number; h: number };
    x?: number;
    y?: number;
    w?: number;
    h?: number;
  }
</script>

<script lang="ts">
  import { chip } from '../actions';

  let { guide, onskip }: { guide: GuideState; onskip: () => void } = $props();
</script>

<div
  class="guide"
  data-hit
  role="region"
  aria-label={guide.title}
  style:left="{guide.x ?? 0}px"
  style:top="{guide.y ?? 0}px"
  style:width="{guide.w ?? 0}px"
  style:height="{guide.h ?? 0}px"
>
  <div class="title">{guide.title}</div>
  <ol class="steps">
    {#each guide.steps ?? [] as step, i (i)}
      <!-- The Qt panel's own string - mark, two spaces, the step - so the card
           it measured fits it here too. -->
      <li class:done={step.done}>{(step.done ? '✓' : '•') + '  ' + step.text}</li>
    {/each}
  </ol>
  {#if guide.skipRect}
    <button
      use:chip
      class="skip"
      style:left="{guide.skipRect.x}px"
      style:top="{guide.skipRect.y}px"
      style:width="{guide.skipRect.w}px"
      style:height="{guide.skipRect.h}px"
      onclick={onskip}>{guide.skip}</button
    >
  {/if}
</div>

<style>
  .guide {
    position: absolute;
    box-sizing: border-box;
    padding: 0 14px;
    /* The guide alone keeps an accent edge over the family's fill - the Qt
       panel's own outline. */
    border: 1px solid var(--accent);
    border-radius: 10px;
    background: var(--card-fill, var(--panel));
    box-shadow: var(--card-shadow, none);
  }
  .title {
    height: 30px;
    display: flex;
    align-items: center;
    font-size: var(--font-title);
    color: var(--text);
  }
  .steps {
    margin: 0;
    padding: 0;
    list-style: none;
  }
  .steps li {
    height: 26px;
    display: flex;
    align-items: center;
    white-space: nowrap;
    font-size: var(--font-body);
    color: var(--text-muted);
  }
  .steps li.done {
    color: var(--accent-text);
  }
  .skip {
    position: absolute;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 0;
    border: 0;
    background: none;
    font: inherit;
    font-size: var(--font-body);
    color: var(--text-muted);
    cursor: pointer;
  }
  .skip:hover {
    color: var(--text);
  }
</style>

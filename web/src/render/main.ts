import { mount } from 'svelte';
import '../tokens.css';
import '../themes.css';
import { applyTokens } from '../theme';
import Render from './Render.svelte';
import type { RenderApi } from './api';
import type { ThemeApi } from '../hub/api';
import { channelObjects, hosted } from '../channel';
import { mockRender, sampleLayout, sampleThumb } from './mock';
import { mockTheme } from '../hub/mock';

async function start() {
  let render: RenderApi;
  let theme: ThemeApi;
  if (hosted()) {
    const objects = await channelObjects();
    render = objects.render as RenderApi;
    theme = objects.theme as ThemeApi;
  } else {
    // The dev server: a panel with two woods and a shot to look at.
    const mock = mockRender(
      sampleLayout([
        { name: 'Oak natural', thumb: sampleThumb },
        { name: 'Walnut', thumb: sampleThumb },
      ]),
    );
    mock.state.shots = ['Front three-quarter'];
    render = mock;
    theme = mockTheme();
  }
  // The theme goes on before the first mount - see hub/main.ts - and again
  // on every Theme change.
  applyTokens(theme.tokens);
  theme.changed.connect(() => applyTokens(theme.tokens));
  mount(Render, { target: document.getElementById('app')!, props: { render } });
}

start();

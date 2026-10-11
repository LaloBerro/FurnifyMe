import { mount } from 'svelte';
import '../tokens.css';
import './overlay.css';
import App from './Overlay.svelte';

mount(App, { target: document.getElementById('app')! });

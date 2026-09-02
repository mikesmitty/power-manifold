// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

// https://astro.build/config
export default defineConfig({
	site: 'https://mikesmitty.github.io',
	base: '/power-manifold',
	integrations: [
		starlight({
			title: 'Power Manifold',
			description: 'A modular six-port USB-C smart PDU for single-board computers.',
			social: [
				{ icon: 'github', label: 'GitHub', href: 'https://github.com/mikesmitty/power-manifold' },
			],
			editLink: {
				baseUrl: 'https://github.com/mikesmitty/power-manifold/edit/main/docs/',
			},
			sidebar: [
				{ label: 'Setup', items: [{ autogenerate: { directory: 'setup' } }] },
				{ label: 'Hardware', items: [{ autogenerate: { directory: 'hardware' } }] },
				{ label: 'Firmware', items: [{ autogenerate: { directory: 'firmware' } }] },
			],
		}),
	],
});

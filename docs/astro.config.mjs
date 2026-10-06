// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

// https://astro.build/config
export default defineConfig({
	site: 'https://docs.powermanifold.io',
	integrations: [
		starlight({
			title: 'Power Manifold',
			description: 'A six-port USB-C power supply with per-port control, power sharing and Home Assistant support.',
			social: [
				{ icon: 'github', label: 'GitHub', href: 'https://github.com/mikesmitty/power-manifold' },
			],
			editLink: {
				baseUrl: 'https://github.com/mikesmitty/power-manifold/edit/main/docs/',
			},
			customCss: ['./src/styles/custom.css'],
			sidebar: [
				{ label: 'Getting started', items: [{ autogenerate: { directory: 'setup' } }] },
				{ label: 'Using Power Manifold', items: [{ autogenerate: { directory: 'guide' } }] },
				{ label: 'Integrations', items: [{ autogenerate: { directory: 'integrations' } }] },
				{
					label: 'For developers',
					collapsed: true,
					items: [{ autogenerate: { directory: 'developers' } }],
				},
			],
		}),
	],
});

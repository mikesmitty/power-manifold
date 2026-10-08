// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
import starlightOpenAPI, { createOpenAPISidebarGroup } from 'starlight-openapi';

// The API reference pages, generated from the controller's OpenAPI
// description, sit at the end of the Integrations group.
const apiReference = createOpenAPISidebarGroup();

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
			plugins: [
				starlightOpenAPI([
					{
						base: 'integrations/api',
						schema: '../firmware/controller/docs/openapi.yaml',
						sidebar: { group: apiReference, label: 'API reference', operations: { badges: true } },
					},
				]),
			],
			sidebar: [
				{ label: 'Getting started', items: [{ autogenerate: { directory: 'setup' } }] },
				{ label: 'Using Power Manifold', items: [{ autogenerate: { directory: 'guide' } }] },
				{ label: 'Integrations', items: [{ autogenerate: { directory: 'integrations' } }, apiReference] },
				{
					label: 'For developers',
					collapsed: true,
					items: [{ autogenerate: { directory: 'developers' } }],
				},
			],
		}),
	],
});

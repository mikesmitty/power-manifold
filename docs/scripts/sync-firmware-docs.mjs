// Copies the firmware developer docs (firmware/*/docs/*.md) into the site's
// "For developers" section, so each page has one source that reads well on
// GitHub and on the site. Runs before `astro dev` and `astro build`.
//
// The source files are plain GitHub Markdown: an H1 title, relative links, and
// GitHub alerts. On the way in, this script turns the H1 into frontmatter,
// relative links into site or GitHub URLs, and alerts into Starlight asides.
// The output directories are generated and ignored by git.
//
// It also validates the controller's OpenAPI description and copies it to
// the site root as /openapi.yaml, for API clients and code generators. The
// reference pages themselves come from the same file through
// starlight-openapi, which reads it without validating it.

import { copyFileSync, mkdirSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { dirname, join, posix, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { validate } from '@readme/openapi-parser';

const REPO = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const OUT = join(REPO, 'docs/src/content/docs/developers');
const BASE = '/';
const SITE = 'https://docs.powermanifold.io/';
const GITHUB = 'https://github.com/mikesmitty/power-manifold';

// Source file (repo-relative) → site slug under developers/, sidebar order and
// description. Add a page here when a new file appears under firmware/*/docs.
const PAGES = [
	{
		src: 'firmware/controller/docs/building.md',
		slug: 'controller/building',
		order: 1,
		description:
			'Toolchain, board targets, network options, the fake-blade simulator and the host-side tests for the RP2350 controller firmware.',
	},
	{
		src: 'firmware/controller/docs/internals.md',
		slug: 'controller/internals',
		order: 2,
		description:
			'How the RP2350 controller firmware is put together: cores, GPIO map, blade generations, warm starts, bus monitoring, wired Ethernet, the UPS link, the fan and the logs.',
	},
	{
		src: 'firmware/controller/docs/flash-and-updates.md',
		slug: 'controller/flash-and-updates',
		order: 3,
		description:
			'Partition table, first programming, BOOTSEL and OTA updates, signed images, try-before-you-buy, and how the controller programs the gen-3 blades.',
	},
	{
		src: 'firmware/controller/docs/console.md',
		slug: 'controller/console',
		order: 4,
		description: "The controller's maintenance console: how to reach it and every command it takes.",
	},
	{
		src: 'firmware/charger-module/docs/overview.md',
		slug: 'charger-module/overview',
		order: 5,
		description:
			'Firmware for the STM32G071 on the gen-3 charger blade: what the blade is, how the port runs, building, flashing and bring-up.',
	},
	{
		src: 'firmware/charger-module/docs/register-map.md',
		slug: 'charger-module/register-map',
		order: 6,
		description:
			"The gen-3 blade's backplane register map and how the controller programs its firmware over the backplane.",
	},
];

const bySrc = new Map(PAGES.map((p) => [p.src, p]));

// GitHub alert kind → Starlight aside type.
const ASIDES = { NOTE: 'note', TIP: 'tip', IMPORTANT: 'note', WARNING: 'caution', CAUTION: 'caution' };

function rewriteLink(url, src) {
	if (url.startsWith(SITE)) return BASE + url.slice(SITE.length);
	if (/^([a-z]+:|#|\/)/i.test(url)) return url;
	const [path, anchor] = url.split('#');
	const target = posix.normalize(posix.join(posix.dirname(src), path));
	const hash = anchor ? `#${anchor}` : '';
	const page = bySrc.get(target);
	if (page) return `${BASE}developers/${page.slug}/${hash}`;
	let kind = 'blob';
	try {
		if (statSync(join(REPO, target)).isDirectory()) kind = 'tree';
	} catch {
		throw new Error(`${src}: link to missing file ${url}`);
	}
	return `${GITHUB}/${kind}/main/${target.replace(/\/$/, '')}${hash}`;
}

function convertAlerts(body) {
	return body.replace(/^> \[!(\w+)\]\n((?:>.*\n?)*)/gm, (_, kind, block) => {
		let lines = block.replace(/\n$/, '').split('\n').map((l) => l.replace(/^> ?/, ''));
		let title = '';
		const t = lines[0]?.match(/^\*\*(.+)\*\*$/);
		if (t) {
			title = `[${t[1]}]`;
			lines = lines.slice(1);
			if (lines[0] === '') lines = lines.slice(1);
		}
		const type = ASIDES[kind.toUpperCase()] ?? 'note';
		return `:::${type}${title}\n${lines.join('\n')}\n:::\n`;
	});
}

function convert(page) {
	const text = readFileSync(join(REPO, page.src), 'utf8');
	const h1 = text.match(/^# (.+)\n+/);
	if (!h1) throw new Error(`${page.src}: must start with an H1 title`);
	let body = text.slice(h1[0].length);
	body = body.replace(/\]\(([^)\s]+)\)/g, (_, url) => `](${rewriteLink(url, page.src)})`);
	body = convertAlerts(body);
	const fm = [
		'---',
		`title: ${JSON.stringify(h1[1].trim())}`,
		`description: ${JSON.stringify(page.description)}`,
		`editUrl: ${GITHUB}/edit/main/${page.src}`,
		'sidebar:',
		`  order: ${page.order}`,
		'---',
		'',
		`<!-- Generated from ${page.src} by docs/scripts/sync-firmware-docs.mjs. Edit that file instead. -->`,
		'',
		'',
	].join('\n');
	return fm + body;
}

for (const dir of new Set(PAGES.map((p) => p.slug.split('/')[0]))) {
	rmSync(join(OUT, dir), { recursive: true, force: true });
}
for (const page of PAGES) {
	const out = join(OUT, `${page.slug}.md`);
	mkdirSync(dirname(out), { recursive: true });
	writeFileSync(out, convert(page));
}
console.log(`sync-firmware-docs: ${PAGES.length} pages into ${relative(REPO, OUT)}/`);

const API = 'firmware/controller/docs/openapi.yaml';
const checked = await validate(join(REPO, API));
if (!checked.valid) {
	for (const e of checked.errors) console.error(`${API}: ${e.message}`);
	process.exit(1);
}
for (const w of checked.warnings) console.warn(`${API}: ${w.message}`);
copyFileSync(join(REPO, API), join(REPO, 'docs/public/openapi.yaml'));
console.log(`sync-firmware-docs: ${API} validated, copied to docs/public/`);

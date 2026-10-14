// Render the modgui screenshot + thumbnail without mod-ui: the template is
// filled like mod-ui does it (namespaces, default values of dopplerit.ttl,
// knob film frames, selected options) and captured with Chromium.
//
//   node tools/render_modgui.mjs
//
// tools/make_screenshots.mjs captures the same from a running mod-ui.
// Needs playwright and Pillow (python3) for the thumbnail.

import { chromium } from 'playwright';
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const GUI = path.join(ROOT, 'dopplerit.lv2', 'modgui');
const FONTS = path.join(ROOT, 'juce', 'Assets', 'fonts');

// control ports of dopplerit.ttl: symbol, default, range, log scale, render format
const ttl = fs.readFileSync(path.join(ROOT, 'dopplerit.lv2', 'dopplerit.ttl'), 'utf8');
const ports = {};
for (const block of ttl.split(/\n    \[\n/).slice(1)) {
  const sym = /lv2:symbol "([^"]+)"/.exec(block);
  const def = /lv2:default ([-\d.]+)/.exec(block);
  if (!sym || !def || !/ControlPort/.test(block)) continue;
  const render = /units:render "([^"]+)"/.exec(block);
  ports[sym[1]] = {
    value: parseFloat(def[1]),
    min: parseFloat(/lv2:minimum ([-\d.]+)/.exec(block)[1]),
    max: parseFloat(/lv2:maximum ([-\d.]+)/.exec(block)[1]),
    log: /pprops:logarithmic/.test(block),
    render: render ? render[1] : (/units:s\b/.test(block) ? '%.2f s' : '%.2f'),
  };
}

const format = (fmt, v) => fmt
  .replace(/%\.(\d)f/, (_, d) => v.toFixed(parseInt(d, 10)))
  .replace('%%', '%');

let html = fs.readFileSync(path.join(GUI, 'icon-dopplerit.html'), 'utf8');
let css = fs.readFileSync(path.join(GUI, 'stylesheet-dopplerit.css'), 'utf8');
const cns = '_dopplerit';
html = html.replaceAll('{{{cns}}}', cns).replace(/\{\{#effect[\s\S]*?\{\{\/effect\.ports\.audio\.(input|output)\}\}/g, '');
css = css.replaceAll('{{{cns}}}', cns).replaceAll('{{{ns}}}', '')
  .replace(/url\(\/resources\/([^)]+)\)/g, (_, f) => `url(file://${path.join(GUI, f)})`);

const page = `<!doctype html><html><head><meta charset="utf-8"><style>
@font-face { font-family: "cooper hewitt"; font-weight: normal; src: url(file://${FONTS}/CooperHewitt-Book.ttf); }
@font-face { font-family: "cooper hewitt"; font-weight: bold; src: url(file://${FONTS}/CooperHewitt-Semibold.ttf); }
body { margin: 0; background: transparent; }
* { font-family: "cooper hewitt" !important; }
.mod-pedal { left: 0; top: 0; }
${css}</style></head><body>${html}</body></html>`;
const tmp = path.join(ROOT, 'build', 'modgui-render.html');
fs.mkdirSync(path.dirname(tmp), { recursive: true });
fs.writeFileSync(tmp, page);

const browser = await chromium.launch(process.env.CHROMIUM ? { executablePath: process.env.CHROMIUM } : {});
const tab = await browser.newPage({ viewport: { width: 400, height: 700 }, deviceScaleFactor: 1 });
await tab.goto('file://' + tmp);
await tab.evaluate((ports) => {
  const frames = 65;
  for (const el of document.querySelectorAll('[mod-role="input-control-port"]')) {
    const p = ports[el.getAttribute('mod-port-symbol')];
    if (!p) continue;
    if (el.classList.contains('dp-knob-image')) {
      const n = p.log ? Math.log(p.value / p.min) / Math.log(p.max / p.min) : (p.value - p.min) / (p.max - p.min);
      el.style.backgroundPosition = `${-Math.round(n * (frames - 1)) * 64}px 0px`;
    }
    for (const opt of el.querySelectorAll('[mod-role="enumeration-option"]'))
      opt.classList.toggle('selected', parseFloat(opt.getAttribute('mod-port-value')) === p.value);
  }
  for (const el of document.querySelectorAll('[mod-role="input-control-value"]')) {
    const p = ports[el.getAttribute('mod-port-symbol')];
    if (p) el.textContent = p.text;
  }
  document.querySelector('[mod-role="bypass"]').classList.add('on');
  document.querySelector('[mod-role="bypass-light"]').classList.add('on');
}, Object.fromEntries(Object.entries(ports).map(([k, p]) => [k, { ...p, text: format(p.render, p.value) }])));
await tab.evaluate(() => document.fonts.ready);
// freeze the beacon animation on its blue phase
await tab.addStyleTag({ content: '* { animation-play-state: paused !important; animation-delay: 0s !important; }' });
await tab.waitForTimeout(300);

const shot = path.join(GUI, 'screenshot-dopplerit.png');
await (await tab.$('.mod-pedal')).screenshot({ path: shot, omitBackground: true });
execFileSync('python3', ['-c',
  'import sys; from PIL import Image; im = Image.open(sys.argv[1]); ' +
  'h = 128; w = round(im.width * h / im.height); im.resize((w, h), Image.LANCZOS).save(sys.argv[2], optimize=True)',
  shot, path.join(GUI, 'thumbnail-dopplerit.png')]);
fs.unlinkSync(tmp);
console.log('rendered', shot);
await browser.close();

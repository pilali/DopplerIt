// Capture the modgui screenshot + thumbnail from a running mod-ui.
//
//   1. run mod-ui in dev mode with the built bundle in its LV2 path
//      (MOD_DEV_ENVIRONMENT=1, see README), listening on MODUI (default :8888)
//   2. node tools/make_screenshots.mjs
//
// Needs playwright (npm i -g playwright) and Pillow (python3) for resizing.

import { chromium } from 'playwright';
import { execFileSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const MODUI = process.env.MODUI || 'http://localhost:8888';
const OUT = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', 'dopplerit.lv2', 'modgui');

const browser = await chromium.launch(process.env.CHROMIUM ? { executablePath: process.env.CHROMIUM } : {});
const page = await browser.newPage({ viewport: { width: 1600, height: 1000 }, deviceScaleFactor: 1 });
await page.goto(MODUI + '/', { waitUntil: 'networkidle' });
await page.waitForTimeout(2000);

const inst = 'dopplerit_shot';
await page.evaluate(async (inst) => {
  const uri = encodeURIComponent('https://github.com/pilali/dopplerit');
  await fetch(`/effect/add//graph/${inst}?uri=${uri}&x=300&y=150`);
}, inst);
await page.waitForTimeout(3000);
// capture at 1:1 scale
await page.evaluate(() => desktop.pedalboard.pedalboard('zoom', 1, 0, 0, 0, 0, 0));
await page.waitForTimeout(1000);
const pedal = await page.$(`[mod-instance="/graph/${inst}"] .mod-pedal`);
const shot = path.join(OUT, 'screenshot-dopplerit.png');
await pedal.screenshot({ path: shot, omitBackground: true });
execFileSync('python3', ['-c',
  'import sys; from PIL import Image; im = Image.open(sys.argv[1]); ' +
  'h = 128; w = round(im.width * h / im.height); im.resize((w, h), Image.LANCZOS).save(sys.argv[2], optimize=True)',
  shot, path.join(OUT, 'thumbnail-dopplerit.png')]);
await page.evaluate(async (inst) => { await fetch(`/effect/remove//graph/${inst}`); }, inst);
console.log('captured', shot);
await browser.close();

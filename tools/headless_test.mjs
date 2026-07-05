import { chromium } from 'playwright-core';
import fs from 'fs';

const URL = process.env.TEST_URL || 'http://127.0.0.1:8090/?debug=1';
const WAIT_MS = parseInt(process.env.WAIT_MS || '45000', 10);
const SHOT = process.env.SHOT || '/Users/q8j/Documents/scpcb/shots/headless.png';
const CLICKS = (process.env.CLICKS || '').split(';').filter(Boolean);
const LOG = process.env.LOG || '/tmp/headless-console.log';

const browser = await chromium.launch({
  channel: 'chrome',
  headless: true,
  args: [
    '--headless=new',
    '--mute-audio',
    '--enable-unsafe-webgpu',
    '--enable-gpu',
    '--use-angle=metal',
    '--window-size=1600,1000',
  ],
});

const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
const lines = [];
page.on('console', (m) => lines.push(`[${m.type()}] ${m.text()}`));
page.on('pageerror', (e) => lines.push(`[pageerror] ${e.message}`));

await page.goto(URL, { waitUntil: 'domcontentloaded' });

const gpu = await page.evaluate(async () => {
  if (!navigator.gpu) return 'NO navigator.gpu';
  const a = await navigator.gpu.requestAdapter().catch((e) => null);
  return a ? 'adapter OK' : 'NO adapter';
});
lines.push(`[harness] webgpu: ${gpu}`);

const deadline = Date.now() + WAIT_MS;
while (Date.now() < deadline) {
  const done = await page.evaluate(() => {
    const l = document.getElementById('loading');
    return !l || l.style.display === 'none';
  }).catch(() => false);
  if (done) { lines.push('[harness] loading done'); break; }
  await page.waitForTimeout(1000);
}
await page.waitForTimeout(4000);

for (const c of CLICKS) {
  const [x, y, delay] = c.split(',').map(Number);
  await page.mouse.move(x, y);
  await page.waitForTimeout(200);
  await page.mouse.down(); await page.waitForTimeout(80); await page.mouse.up();
  lines.push(`[harness] clicked ${x},${y}`);
  await page.waitForTimeout(delay || 3000);
}

await page.screenshot({ path: SHOT });
lines.push(`[harness] screenshot ${SHOT}`);
fs.writeFileSync(LOG, lines.join('\n') + '\n');
console.log(lines.filter(l => !l.startsWith('[log] @@')).slice(-80).join('\n'));
await browser.close();

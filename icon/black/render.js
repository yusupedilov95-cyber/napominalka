const { chromium } = require('playwright'); const fs = require('fs'); const path = require('path');
(async () => {
  const dir = process.argv[2]; const svg = fs.readFileSync(path.join(dir, 'black.svg'), 'utf8');
  const b = await chromium.launch(); const p = await b.newPage();
  for (const s of [16, 20, 24, 32, 40, 48, 64, 128, 256]) {
    await p.setViewportSize({ width: s, height: s });
    await p.setContent(`<html><body style="margin:0;background:transparent">${svg.replace('width="256" height="256"', `width="${s}" height="${s}"`)}</body></html>`);
    await p.screenshot({ path: path.join(dir, `${s}.png`), omitBackground: true, clip: { x: 0, y: 0, width: s, height: s } });
  }
  await b.close();
})();

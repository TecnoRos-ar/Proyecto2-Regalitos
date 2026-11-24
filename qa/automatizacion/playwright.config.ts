import { defineConfig, devices } from '@playwright/test';

/* Chrome instalado en el equipo: no se descargan navegadores. */
const chrome = { channel: 'chrome', launchOptions: { chromiumSandbox: false } };

export default defineConfig({
  testDir: './tests',
  fullyParallel: true,
  forbidOnly: true,
  retries: 0,
  timeout: 30_000,
  expect: { timeout: 5_000 },
  outputDir: 'test-results',
  reporter: [
    ['list'],
    ['json', { outputFile: 'test-results/resultados.json' }],
    ['html', { outputFolder: 'playwright-report', open: 'never' }],
  ],
  use: {
    locale: 'es-AR',
    timezoneId: 'America/Argentina/Buenos_Aires',
    screenshot: 'only-on-failure',
    trace: 'retain-on-failure',
  },
  projects: [
    { name: 'escritorio', use: { ...devices['Desktop Chrome'], ...chrome, viewport: { width: 1366, height: 768 } } },
    { name: 'movil', grep: /@movil/, use: { ...devices['Pixel 7'], ...chrome, viewport: { width: 390, height: 844 } } },
  ],
});

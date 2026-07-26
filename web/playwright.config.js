import { defineConfig, devices } from '@playwright/test';
import { REAL_E2E } from './e2e/protocol-interaction/real/test_config.js';

const backend = process.env.PLAYWRIGHT_BACKEND === 'real' ? 'real' : 'mock';
const mockBaseURL = 'http://127.0.0.1:4173';
const realBaseURL = REAL_E2E.baseURL;
const isReal = backend === 'real';
const planE2E = process.env.PLAYWRIGHT_PLAN === '1';
const reuseExistingServer = process.env.CI
    ? false
    : process.env.PLAYWRIGHT_REUSE_EXISTING_SERVER !== 'false';
const outputDir = process.env.PLAYWRIGHT_OUTPUT_DIR
    || `test-results/${backend}-${Date.now()}-${process.pid}`;

export default defineConfig({
    testDir: './e2e',
    timeout: 30_000,
    expect: {
        timeout: 5_000,
    },
    fullyParallel: false,
    workers: 1,
    reporter: 'list',
    outputDir,
    preserveOutput: 'always',
    use: {
        baseURL: isReal ? realBaseURL : mockBaseURL,
        trace: 'retain-on-failure',
        screenshot: 'only-on-failure',
        video: 'retain-on-failure',
    },
    projects: [
        {
            name: `chromium-desktop-${backend}`,
            use: {
                ...devices['Desktop Chrome'],
                viewport: { width: 1440, height: 900 },
            },
            testMatch: planE2E
                ? '**/*.spec.js'
                : (isReal
                    ? '**/protocol-interaction/real/**/*.spec.js'
                    : '**/protocol-interaction/{mock,browser}/**/*.spec.js'),
        },
    ],
    testIgnore: planE2E ? '**/protocol-interaction/**/*.spec.js' : undefined,
    webServer: isReal
        ? {
            command: 'bash ./e2e/protocol-interaction/real/start-isolated-backend.sh',
            url: `${realBaseURL}/html/login.html`,
            cwd: '.',
            reuseExistingServer,
            timeout: 120_000,
        }
        : {
            command: 'python3 -m http.server 4173 -d .',
            url: `${mockBaseURL}/html/login.html?apiMode=mock`,
            reuseExistingServer,
        },
});

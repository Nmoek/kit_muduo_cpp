import { test, expect } from '@playwright/test';
import https from 'node:https';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { createHttpsServiceAndProtocol, loginHttpsAdmin } from '../https_flow.js';

function requestHttps(port, target, ca) {
    return new Promise((resolve, reject) => {
        const request = https.get({ hostname: '127.0.0.1', port, path: target, ca, rejectUnauthorized: true }, response => {
            let body = '';
            response.setEncoding('utf8');
            response.on('data', chunk => { body += chunk; });
            response.on('end', () => resolve({ status: response.statusCode, body }));
        });
        request.setTimeout(5000, () => request.destroy(new Error('HTTPS request timeout')));
        request.on('error', reject);
    });
}

/**
 * 测试思路：把页面配置、原生 TLS 请求、WebSocket 和交互展示串起来，使用隔离 DB 和测试 CA。
 * 示例：页面创建 HTTPS -> 保存并上线 GET /https/health -> curl --cacert 200 -> 抽屉 HTTPS matched。
 * 再修改路径，验证旧路径 404、新路径 200；最终停止并删除本轮项目，不能写仓库数据库。
 */
test('真实 HTTPS 证书校验、协议修改和交互展示', async ({ page }) => {
    test.setTimeout(60_000);
    await loginHttpsAdmin(page);
    const { projectId, protocolId, port } = await createHttpsServiceAndProtocol(page);
    const caFile = path.join(process.env.PLAYWRIGHT_REAL_HTTPS_CERT_DIR, 'ca.pem');
    const ca = readFileSync(caFile);
    try {
        await page.getByTestId('protocol-interaction-open').click();
        const drawer = page.getByTestId('protocol-interaction-drawer');
        await drawer.getByTestId('protocol-interaction-connect').click();
        await expect(drawer.getByTestId('protocol-interaction-status')).toContainText('实时');
        // curl 验收显式使用 CA 和 IP SAN，禁止以 -k 掩盖证书问题。
        const status = execFileSync('curl', ['--silent', '--show-error', '--max-time', '5',
            '--cacert', caFile, '--output', '/dev/null', '--write-out', '%{http_code}',
            `https://127.0.0.1:${port}/https/health`], { encoding: 'utf8' });
        expect(status).toBe('200');
        await expect.poll(async () => page.evaluate(() => {
            const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
            return client?.getState().visibleRecords.some(record => record.protocol_type === 'https' && record.result === 'matched');
        })).toBe(true);
        await expect(drawer.locator('.interaction-record-item').first()).toContainText('/https/health');
        await drawer.getByTestId('protocol-interaction-close').click();
        await page.getByRole('button', { name: '展开协议项详情', exact: true }).click();
        await page.locator('.protocol-item .path').click();
        const editor = page.locator('.protocol-item .path');
        await editor.locator('input').fill('/https/updated');
        await editor.locator('input').press('Enter');
        await expect(editor.locator('.value')).toHaveText('/https/updated');
        expect((await requestHttps(port, '/https/health', ca)).status).toBe(404);
        expect((await requestHttps(port, '/https/updated', ca)).status).toBe(200);
        const detail = await page.evaluate(async id => window.KitProxy.api.getProtocolEditDetail(id), protocolId);
        expect(String(detail.type).toLowerCase()).toBe('https');
        expect(detail.req_cfg.path).toBe('/https/updated');
    } finally {
        if (!page.isClosed()) {
            await page.evaluate(async id => {
                await window.KitProxy.api.setProjectRuntimeState(id, false);
                await window.KitProxy.api.deleteProject(id);
            }, projectId);
        }
    }
});

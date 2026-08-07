import { Buffer } from 'node:buffer';
import http from 'node:http';
import { test, expect } from '@playwright/test';
import { REAL_E2E, ensureRealProjectRunning, protocolItemSelector, protocolItemsPath } from './test_config.js';

async function loginRealAdmin(page, context) {
    await context.clearCookies();
    await page.goto('/html/login.html');
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill(REAL_E2E.adminNote);
    await page.getByLabel('密码').fill(REAL_E2E.adminPassword);
    await Promise.all([
        page.waitForURL(/\/html\/main\.html/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
}

function sendHttpProtocolRequest(port = REAL_E2E.attachmentHttpPort) {
    return new Promise((resolve, reject) => {
        const request = http.request({
            host: REAL_E2E.attachmentHttpHost,
            port,
            path: REAL_E2E.attachmentHttpPath,
            method: 'GET',
        }, response => {
            const chunks = [];
            response.on('data', chunk => chunks.push(Buffer.from(chunk)));
            response.once('end', () => resolve({
                statusCode: response.statusCode,
                headers: response.headers,
                body: Buffer.concat(chunks),
            }));
        });
        const timer = setTimeout(() => {
            request.destroy();
            reject(new Error(`HTTP request timeout: ${REAL_E2E.attachmentHttpHost}:${port}${REAL_E2E.attachmentHttpPath}`));
        }, 5_000);
        request.once('error', error => {
            clearTimeout(timer);
            reject(error);
        });
        request.once('close', () => clearTimeout(timer));
        request.end();
    });
}

/**
 * 测试思路：
 *
 * 测什么：
 * 验证 Binary Body 的字段配置会在真实 HTTP ProjectServer 中组装为精确响应
 * 字节，并以十六进制正文保存至同一条 interaction。HTTP Binary Body 是协议
 * 配置数据，不是文件附件，记录不应额外产生 WebSocket binary sidecar。
 *
 * 为什么这么测：
 * 字段 JSON 到运行态字节的转换只能由真实 C++ ProjectServer 验证。此前只在
 * 浏览器和领域单测中检查序列化，无法发现响应被原始配置 JSON、零填充或错误
 * MIME 类型替代的回归；本用例同时覆盖真实 HTTP 响应和 WebSocket 观察记录。
 *
 * 怎么测：
 * 1. 启动隔离真实后端，HTTP protocol 4 的 response body 使用固定 05 06 07 08。
 * 2. 登录并打开 project 2/protocol 4 实时抽屉，等待 `live_ready(open)`。
 * 3. 向真实 HTTP ProjectServer 发送 GET /api/test4，等待页面出现新的 protocol matched 记录。
 * 4. 断言 HTTP 客户端精确收到 `05 06 07 08` 和 `application/octet-stream`。
 * 5. 从生产 LiveClient 读取该记录，断言 Binary Body 的大小、十六进制文本和
 *    预期类型均正确，且 attachments 为空。
 *
 * 示例：
 * fields[].spec/value -> HTTP response [05 06 07 08]
 * -> interaction.response.body.text = H05 06 07 08。
 */
test('真实 Binary Body 字段配置输出精确 HTTP 响应并记录十六进制正文', async ({ page, context }) => {
    await loginRealAdmin(page, context);
    await ensureRealProjectRunning(page, REAL_E2E.attachmentProjectId);
    await page.goto(protocolItemsPath(REAL_E2E.attachmentProjectId));
    await page.locator(protocolItemSelector(REAL_E2E.attachmentProtocolId))
        .getByTestId('protocol-interaction-open').click();
    const drawer = page.getByTestId('protocol-interaction-drawer');
    await expect(drawer).toBeVisible();

    await drawer.getByTestId('protocol-interaction-connect').click();
    await expect(drawer.getByTestId('protocol-interaction-state')).toHaveText('实时', { timeout: 10_000 });

    const response = await sendHttpProtocolRequest();
    expect(response.statusCode).toBe(200);
    expect(response.headers['content-type']).toBe('application/octet-stream');
    expect(Array.from(response.body)).toEqual([5, 6, 7, 8]);

    await expect.poll(() => page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        if (!client) return false;
        const state = client.getState();
        return state.visibleRecords.concat(state.pendingRecords).some(record => record.scope === 'protocol'
            && record.result === 'matched'
            && record.response && record.response.body
            && record.response.body.text === 'H05 06 07 08');
    }), { timeout: 10_000 }).toBe(true);

    const interactionState = await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const state = client.getState();
        const record = state.visibleRecords.concat(state.pendingRecords).find(item => item.scope === 'protocol'
            && item.result === 'matched'
            && item.response && item.response.body
            && item.response.body.text === 'H05 06 07 08');
        if (!record) return null;
        return {
            key: record._key,
            kind: record.response.body.kind,
            expectedKind: record.response.body.expect_kind,
            size: record.response.body.size,
            capturedSize: record.response.body.captured_size,
            text: record.response.body.text,
            attachments: record.response.body.attachments,
        };
    });

    expect(interactionState).toEqual({
        key: expect.any(String),
        kind: 'unknown',
        expectedKind: 'binary',
        size: 4,
        capturedSize: 4,
        text: 'H05 06 07 08',
        attachments: [],
    });
});

import net from 'node:net';
import http from 'node:http';
import { test, expect } from '@playwright/test';
import { REAL_E2E, protocolItemSelector, protocolItemsPath } from './test_config.js';

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
            response.resume();
            response.once('end', resolve);
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
 * 验证真实 C++ 后端在发送一条 interaction JSON 后，会继续发送带 4 字节
 * attachment Header 的 WebSocket binary frame；前端能够校验 Header 中的
 * captured_size、按 attachment_id 关联到正确记录，并把 payload 保存为可读取
 * 的 attachment，不把 binary frame 当成普通 JSON。
 *
 * 为什么这么测：
 * 二进制帧的边界、JSON Header、payload 长度和异步到达顺序是 Mock transport
 * 无法证明的真实协议风险。B2/B3 只验证文本 interaction 和状态命令，本用例
 * 专门验证后端 `MessageGroup(text + binary)` 到 Chromium WebSocket、LiveClient
 * 和详情数据的完整链路。
 *
 * 怎么测：
 * 1. 启动隔离真实后端，HTTP protocol 4 的 response body 使用固定 05 06 07 08。
 * 2. 登录并打开 project 2/protocol 4 实时抽屉，监听 WebSocket `framereceived`。
 * 3. 向真实 HTTP ProjectServer 发送 GET /api/test4，等待页面出现新的 protocol matched 记录。
 * 4. 断言 WebSocket 先收到 interaction text frame，再收到 binary frame。
 * 5. 从生产 LiveClient 读取该记录的 attachment 引用和 payload，断言大小、
 *    attachment id、完整性和四个字节均正确。
 * 6. 刷新页面，断言 IndexedDB hydration 恢复同一 record key、attachment id
 *    和四个 payload 字节。
 * 7. 清理记录并断言 attachment payload 不再保留，防止 Blob/object URL 泄漏。
 *
 * 示例：
 * interaction JSON -> attachment binary Header+payload -> record incomplete
 * -> payload matched -> record complete。
 */
test('真实二进制附件帧关联记录并完成 payload 恢���', async ({ page, context }) => {
    await loginRealAdmin(page, context);
    await page.goto(protocolItemsPath(REAL_E2E.attachmentProjectId));
    await page.locator(protocolItemSelector(REAL_E2E.attachmentProtocolId))
        .getByTestId('protocol-interaction-open').click();
    const drawer = page.getByTestId('protocol-interaction-drawer');
    await expect(drawer).toBeVisible();

    const websocketPromise = page.waitForEvent('websocket');
    await drawer.getByTestId('protocol-interaction-connect').click();
    const websocket = await websocketPromise;
    const receivedFrameKinds = [];
    websocket.on('framereceived', frame => {
        receivedFrameKinds.push(typeof frame.payload === 'string' ? 'text' : 'binary');
    });
    await expect(drawer.getByTestId('protocol-interaction-state')).toHaveText('实时', { timeout: 10_000 });

    await sendHttpProtocolRequest();
    await expect.poll(async () => page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        return client.getState().visibleRecords.filter(record => record.scope === 'protocol'
            && record.result === 'matched').length;
    }), { timeout: 10_000 }).toBeGreaterThan(0);
    await expect.poll(() => receivedFrameKinds.includes('binary'), { timeout: 10_000 }).toBe(true);
    const binaryFrameIndex = receivedFrameKinds.indexOf('binary');
    expect(binaryFrameIndex).toBeGreaterThan(0);
    expect(receivedFrameKinds[binaryFrameIndex - 1]).toBe('text');

    const attachmentState = await page.evaluate(async () => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const record = client.getState().visibleRecords.find(item => item.scope === 'protocol'
            && item.result === 'matched'
            && item.response && item.response.body && item.response.body.attachments
            && item.response.body.attachments.length);
        if (!record) return null;
        const ref = record.response.body.attachments[0];
        const payload = client.getAttachment(record._key, ref.attachment_id);
        await client.flushPersistence();
        const state = client.getState();
        const persistedEntry = Array.from(state.persistedRecords.values())
            .concat(Array.from(state.pendingPersistenceRecords.values()))
            .find(entry => entry.recordKey === record._key);
        return {
            key: record._key,
            attachmentId: ref.attachment_id,
            expectedSize: ref.captured_size,
            completionState: persistedEntry && persistedEntry.completionState,
            capturedSize: payload && payload.capturedSize,
            bytes: payload && Array.from(payload.bytes),
        };
    });

    expect(attachmentState).toMatchObject({
        expectedSize: 4,
        capturedSize: 4,
        bytes: [5, 6, 7, 8],
        completionState: 'complete',
    });
    expect(attachmentState.attachmentId).toContain('response.body');

    await page.reload();
    const restoredDrawer = page.getByTestId('protocol-interaction-drawer');
    await expect(restoredDrawer).toBeVisible();
    await expect(restoredDrawer.getByTestId('protocol-interaction-state')).toHaveText('实时', { timeout: 10_000 });
    const restoredAttachment = await expect.poll(() => page.evaluate(({ key, attachmentId }) => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        if (!client) return null;
        const state = client.getState();
        const record = state.visibleRecords.concat(state.pendingRecords)
            .find(item => item._key === key);
        if (!record) return null;
        const payload = client.getAttachment(record, attachmentId);
        return payload ? {
            key: record._key,
            bytes: Array.from(payload.bytes),
            capturedSize: payload.capturedSize,
            objectUrl: payload.objectUrl,
        } : null;
    }, { key: attachmentState.key, attachmentId: attachmentState.attachmentId }), {
        timeout: 10_000,
    }).not.toBeNull().then(() => page.evaluate(({ key, attachmentId }) => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const state = client.getState();
        const record = state.visibleRecords.concat(state.pendingRecords).find(item => item._key === key);
        const payload = client.getAttachment(record, attachmentId);
        return {
            key: record._key,
            bytes: Array.from(payload.bytes),
            capturedSize: payload.capturedSize,
            objectUrl: payload.objectUrl,
        };
    }, { key: attachmentState.key, attachmentId: attachmentState.attachmentId }));
    expect(restoredAttachment).toMatchObject({
        key: attachmentState.key,
        bytes: [5, 6, 7, 8],
        capturedSize: 4,
    });
    expect(restoredAttachment.objectUrl).toMatch(/^blob:/);

    await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        client.clearRecords();
    });
    await expect.poll(() => page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        return client.getState().attachmentPayloads.size;
    })).toBe(0);
});

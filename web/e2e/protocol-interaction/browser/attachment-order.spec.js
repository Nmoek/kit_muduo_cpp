import { test, expect } from '@playwright/test';

async function loginAndOpenDrawer(page, context) {
    await context.clearCookies();
    await page.goto('/html/login.html?apiMode=mock&returnTo=%2Fhtml%2Fprotocol_items.html%3FapiMode%3Dmock%26projectId%3D1');
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill('admin');
    await page.getByLabel('密码').fill('admin123');
    await Promise.all([
        page.waitForURL(/\/html\/protocol_items\.html\?/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
    await page.locator('.protocol-item[data-protocol-id="1"]')
        .getByTestId('protocol-interaction-open').click();
    const drawer = page.getByTestId('protocol-interaction-drawer');
    await drawer.getByTestId('protocol-interaction-connect').click();
    await expect(drawer.getByTestId('protocol-interaction-state')).toHaveText('实时');
    await expect.poll(() => drawer.getByTestId('protocol-interaction-record-item').count())
        .toBeGreaterThanOrEqual(4);
    return drawer;
}

/**
 * 测试思路：
 *
 * 测什么：
 * 验证真实桌面 Chromium 中，A/B 两条 interaction JSON 已登记后以
 * binary B -> binary A 顺序到达时，生产 LiveClient 依然按完整附件键
 * 精确关联 payload，DOM 显示两条记录，持久化状态都为 complete，
 * 清理时回收两个 Blob URL。
 *
 * 为什么这么测：
 * Vitest 可验证映射逻辑，但无法证明 Chromium 的 Blob URL、抽屉渲染、
 * 记录选中和 IndexedDB 异步写入一起工作时不会串记录或泄漏资源。
 *
 * 怎么测：
 * 1. 打开 Mock 抽屉，保留一条合法记录作为模板后清空列表。
 * 2. 注入 seq=8101/attachment-a 和 seq=8102/attachment-b 两条 JSON。
 * 3. 按 B -> A 注入不同字节的 binary frame，等待列表渲染和持久化完成。
 * 4. 断言两条 DOM、payload bytes、recordKey、completionState 和 objectUrl 一一对应。
 * 5. 清空记录，断言附件 Map 为空且两个 URL 均被 revoke。
 *
 * 示例：
 * JSON A -> JSON B -> binary B[3,4,5] -> binary A[1,2]
 *        -> A=attachment-a/[1,2], B=attachment-b/[3,4,5]。
 */
test('多记录附件交错到达时 DOM、Blob 和持久化保持精确关联', async ({ page, context }) => {
    const drawer = await loginAndOpenDrawer(page, context);
    const injected = await page.evaluate(async () => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const source = client.getState().visibleRecords.find(record => record.scope === 'protocol');
        if (!source) throw new Error('没有可用的 protocol 记录模板');
        const originalRevoke = window.URL.revokeObjectURL.bind(window.URL);
        window.__t5RevokedObjectUrls = [];
        window.URL.revokeObjectURL = url => {
            window.__t5RevokedObjectUrls.push(url);
            originalRevoke(url);
        };
        client.clearRecords();
        client.setMergeSpeed('fast');

        const makeRecord = (seq, attachmentId, size) => {
            const record = JSON.parse(JSON.stringify(source));
            record.seq = seq;
            record.time_ms = Date.now() + seq;
            record.request.body = {
                kind: 'binary', expect_kind: 'binary', size, captured_size: size,
                truncated: false, sha1: '', text: '', error_message: '',
                attachments: [{
                    attachment_id: attachmentId, side: 'request', flag: 'request.body',
                    kind: 'binary', size, captured_size: size, truncated: false,
                    binary_available: true, sha1: '',
                }],
            };
            record.response.body = {
                kind: 'empty', expect_kind: 'unknown', size: 0, captured_size: 0,
                truncated: false, sha1: '', text: '', error_message: '', attachments: [],
            };
            return record;
        };
        const makeFrame = (record, attachmentId, bytes) => {
            const headerBytes = new TextEncoder().encode(JSON.stringify({
                type: 'attachment', scope: record.scope, project_id: record.project_id,
                protocol_id: record.protocol_id, cache_instance_id: record.cache_instance_id,
                record_seq: record.seq, attachment_id: attachmentId,
                captured_size: bytes.length,
            }));
            const frame = new Uint8Array(4 + headerBytes.length + bytes.length);
            new DataView(frame.buffer).setUint32(0, headerBytes.length, false);
            frame.set(headerBytes, 4);
            frame.set(bytes, 4 + headerBytes.length);
            return frame;
        };

        const recordA = makeRecord(8101, 'attachment-a', 2);
        const recordB = makeRecord(8102, 'attachment-b', 3);
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'catch_up', record: recordA }));
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'catch_up', record: recordB }));
        client.receiveBinary(makeFrame(recordB, 'attachment-b', new Uint8Array([3, 4, 5])));
        client.receiveBinary(makeFrame(recordA, 'attachment-a', new Uint8Array([1, 2])));
        await client.flushPersistence();
        return {
            keyA: `protocol:${recordA.cache_instance_id}:${recordA.seq}`,
            keyB: `protocol:${recordB.cache_instance_id}:${recordB.seq}`,
        };
    });

    const itemA = drawer.locator(`[data-testid="protocol-interaction-record-item"][data-record-key="${injected.keyA}"]`);
    const itemB = drawer.locator(`[data-testid="protocol-interaction-record-item"][data-record-key="${injected.keyB}"]`);
    await expect(itemA).toBeVisible({ timeout: 5_000 });
    await expect(itemB).toBeVisible({ timeout: 5_000 });
    await itemA.click();
    await expect(drawer.getByTestId('protocol-interaction-detail-content')).not.toBeEmpty();
    await itemB.click();
    await expect(drawer.getByTestId('protocol-interaction-detail-content')).not.toBeEmpty();

    const attachmentState = await page.evaluate(keys => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const state = client.getState();
        const recordA = state.visibleRecords.concat(state.pendingRecords).find(record => record._key === keys.keyA);
        const recordB = state.visibleRecords.concat(state.pendingRecords).find(record => record._key === keys.keyB);
        const payloadA = client.getAttachment(recordA, 'attachment-a');
        const payloadB = client.getAttachment(recordB, 'attachment-b');
        const entries = Array.from(state.persistedRecords.values())
            .concat(Array.from(state.pendingPersistenceRecords.values()));
        return {
            a: { bytes: Array.from(payloadA.bytes), recordKey: payloadA.recordKey, objectUrl: payloadA.objectUrl },
            b: { bytes: Array.from(payloadB.bytes), recordKey: payloadB.recordKey, objectUrl: payloadB.objectUrl },
            completionA: entries.find(entry => entry.recordKey === keys.keyA).completionState,
            completionB: entries.find(entry => entry.recordKey === keys.keyB).completionState,
        };
    }, injected);
    expect(attachmentState.a).toMatchObject({ bytes: [1, 2], recordKey: injected.keyA });
    expect(attachmentState.b).toMatchObject({ bytes: [3, 4, 5], recordKey: injected.keyB });
    expect(attachmentState.a.objectUrl).toMatch(/^blob:/);
    expect(attachmentState.b.objectUrl).toMatch(/^blob:/);
    expect(attachmentState.a.objectUrl).not.toBe(attachmentState.b.objectUrl);
    expect(attachmentState.completionA).toBe('complete');
    expect(attachmentState.completionB).toBe('complete');

    await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        client.clearRecords();
    });
    await expect(drawer.getByTestId('protocol-interaction-record-item')).toHaveCount(0);
    expect(await page.evaluate(urls => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        return {
            attachmentCount: client.getState().attachmentPayloads.size,
            revoked: urls.every(url => window.__t5RevokedObjectUrls.includes(url)),
        };
    }, [attachmentState.a.objectUrl, attachmentState.b.objectUrl])).toEqual({
        attachmentCount: 0,
        revoked: true,
    });
});

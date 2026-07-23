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

async function injectTwoScopesAndReset(page, resetScope) {
    return page.evaluate(async scope => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const initial = client.getState();
        const records = initial.visibleRecords.concat(initial.pendingRecords);
        const protocolTemplate = records.find(record => record.scope === 'protocol');
        const projectTemplate = records.find(record => record.scope === 'project');
        if (!protocolTemplate || !projectTemplate) throw new Error('缺少双 scope 测试模板');
        const originalRevoke = window.URL.revokeObjectURL.bind(window.URL);
        window.__t5ScopeResetRevokedUrls = [];
        window.URL.revokeObjectURL = url => {
            window.__t5ScopeResetRevokedUrls.push(url);
            originalRevoke(url);
        };
        client.clearRecords();
        await client.flushPersistence();
        client.setMergeSpeed('fast');

        const makeRecord = (template, attachmentId, bytes) => {
            const record = JSON.parse(JSON.stringify(template));
            record.seq = 7201;
            record.time_ms = Date.now() + (record.scope === 'project' ? 1 : 0);
            record.request.body = {
                kind: 'binary', expect_kind: 'binary', size: bytes.length,
                captured_size: bytes.length, truncated: false, sha1: '', text: '', error_message: '',
                attachments: [{
                    attachment_id: attachmentId, side: 'request', flag: 'request.body',
                    kind: 'binary', size: bytes.length, captured_size: bytes.length,
                    truncated: false, binary_available: true, sha1: '',
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
                record_seq: record.seq, attachment_id: attachmentId, captured_size: bytes.length,
            }));
            const frame = new Uint8Array(4 + headerBytes.length + bytes.length);
            new DataView(frame.buffer).setUint32(0, headerBytes.length, false);
            frame.set(headerBytes, 4);
            frame.set(bytes, 4 + headerBytes.length);
            return frame;
        };
        const protocolRecord = makeRecord(protocolTemplate, 'protocol-reset-attachment', new Uint8Array([1, 2]));
        const projectRecord = makeRecord(projectTemplate, 'project-reset-attachment', new Uint8Array([3, 4]));
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: protocolRecord }));
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: projectRecord }));
        client.receiveBinary(makeFrame(protocolRecord, 'protocol-reset-attachment', new Uint8Array([1, 2])));
        client.receiveBinary(makeFrame(projectRecord, 'project-reset-attachment', new Uint8Array([3, 4])));
        await client.flushPersistence();
        const beforeReset = client.getState();
        const targetKey = scope === 'protocol'
            ? `protocol:${protocolRecord.cache_instance_id}:${protocolRecord.seq}`
            : `project:${projectRecord.cache_instance_id}:${projectRecord.seq}`;
        const survivorKey = scope === 'protocol'
            ? `project:${projectRecord.cache_instance_id}:${projectRecord.seq}`
            : `protocol:${protocolRecord.cache_instance_id}:${protocolRecord.seq}`;
        const targetPayload = Array.from(beforeReset.attachmentPayloads.values())
            .find(payload => payload.recordKey === targetKey);

        client.receiveText(JSON.stringify({
            type: 'live_ready', trigger: 'open', project_id: 1, protocol_id: 1,
            session_id: beforeReset.sessionId,
            protocol_cache_info: {
                cache_instance_id: scope === 'protocol'
                    ? protocolRecord.cache_instance_id + 100 : protocolRecord.cache_instance_id,
                last_seq: scope === 'protocol' ? 0 : protocolRecord.seq,
                catch_up_count: 0, catch_up_gap: false, cursor_reset: scope === 'protocol',
            },
            project_cache_info: {
                cache_instance_id: scope === 'project'
                    ? projectRecord.cache_instance_id + 100 : projectRecord.cache_instance_id,
                last_seq: scope === 'project' ? 0 : projectRecord.seq,
                catch_up_count: 0, catch_up_gap: false, cursor_reset: scope === 'project',
            },
        }));
        await client.flushPersistence();
        const state = client.getState();
        const persistence = window.KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createIndexedDbPersistenceAdapter();
        const loaded = await adapter.loadWorkspace(client.getPersistenceState().snapshotKey);
        await adapter.close();
        return {
            targetKey,
            survivorKey,
            targetObjectUrl: targetPayload && targetPayload.objectUrl,
            recordKeys: Array.from(state.recordKeys),
            attachmentKeys: Array.from(state.attachmentPayloads.keys()),
            pendingAttachmentKeys: Array.from(state.pendingAttachmentRefs.keys()),
            protocolDurableCursor: state.protocolDurableCursor,
            projectDurableCursor: state.projectDurableCursor,
            revokedUrls: window.__t5ScopeResetRevokedUrls.slice(),
            workspaceRecordKeys: loaded.workspace.records.map(entry => entry.recordKey),
            workspaceAttachmentRecordKeys: loaded.workspace.attachments.map(entry => entry.recordKey),
        };
    }, resetScope);
}

async function assertScopeReset(page, drawer, resetScope) {
    const result = await injectTwoScopesAndReset(page, resetScope);
    const survivorScope = resetScope === 'protocol' ? 'project' : 'protocol';
    expect(result.recordKeys).toEqual([result.survivorKey]);
    expect(result.attachmentKeys).toHaveLength(1);
    expect(result.attachmentKeys[0].startsWith(`${survivorScope}:`)).toBe(true);
    expect(result.pendingAttachmentKeys.some(key => key.startsWith(`${resetScope}:`))).toBe(false);
    expect(result.workspaceRecordKeys).toEqual([result.survivorKey]);
    expect(result.workspaceAttachmentRecordKeys).toEqual([result.survivorKey]);
    expect(result.targetObjectUrl).toMatch(/^blob:/);
    expect(result.revokedUrls).toContain(result.targetObjectUrl);
    expect(result[resetScope === 'protocol' ? 'protocolDurableCursor' : 'projectDurableCursor']).toBeNull();
    expect(result[survivorScope === 'protocol' ? 'protocolDurableCursor' : 'projectDurableCursor']).toBeTruthy();

    const targetItem = drawer.locator(`[data-record-key="${result.targetKey}"]`);
    const survivorItem = drawer.locator(`[data-record-key="${result.survivorKey}"]`);
    await expect(targetItem).toHaveCount(0);
    await expect(survivorItem).toBeVisible({ timeout: 5_000 });
    await survivorItem.click();
    await expect(drawer.getByTestId('protocol-interaction-detail-content')).not.toBeEmpty();
}

/**
 * 测试思路：
 *
 * 测什么：验证 protocol-only cursor reset 在真实 Chromium 中只删除
 * protocol 记录、附件、Blob URL、durable cursor 和 IndexedDB 条目，完整保留
 * project Notice 及其附件。
 * 为什么这么测：单元状态断言不能证明抽屉 DOM、Blob 回收和浏览器
 * IndexedDB 在 reset 事件后保持同一个 scope 边界。
 * 怎么测：注入双域记录和附件，再注入仅 protocol `cursor_reset=true`
 * 的 live_ready，同时检查 client、DOM、revoke 列表和 IndexedDB workspace。
 *
 * 示例：protocol reset -> remove protocol:cache:7201 -> keep project:cache:7201。
 */
test('protocol-only cursor reset 只清理协议域并保留 project Notice', async ({ page, context }) => {
    const drawer = await loginAndOpenDrawer(page, context);
    await assertScopeReset(page, drawer, 'protocol');
});

/**
 * 测试思路：
 *
 * 测什么：验证 project-only cursor reset 在真实 Chromium 中只删除
 * project Notice、附件、Blob URL、durable cursor 和 IndexedDB 条目，完整保留
 * protocol interaction 及其附件。
 * 为什么这么测：真实后端当前没有 project-only cache 重建接口，必须
 * 通过生产 LiveClient 和真实浏览器持久化层单独锁定这一对称语义。
 * 怎么测：使用独立 workspace 注入双域记录和附件，再注入仅
 * project `cursor_reset=true` 的 live_ready，检查内存、DOM、Blob 和 IndexedDB。
 *
 * 示例：project reset -> remove project:cache:7201 -> keep protocol:cache:7201。
 */
test('project-only cursor reset 只清理 Notice 域并保留协议记录', async ({ page, context }) => {
    const drawer = await loginAndOpenDrawer(page, context);
    await assertScopeReset(page, drawer, 'project');
});

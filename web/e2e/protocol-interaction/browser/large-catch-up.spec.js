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

async function injectLargeCatchUp(page) {
    return page.evaluate(async () => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const initial = client.getState().visibleRecords.concat(client.getState().pendingRecords);
        const protocolTemplate = initial.find(record => record.scope === 'protocol');
        const projectTemplate = initial.find(record => record.scope === 'project');
        if (!protocolTemplate || !projectTemplate) throw new Error('缺少 protocol/project 注入模板');
        client.clearRecords();
        await client.flushPersistence();
        client.pauseMerge();

        const withoutAttachments = (template, seq, timeOffset) => {
            const record = JSON.parse(JSON.stringify(template));
            record.seq = seq;
            record.time_ms = 1_780_000_000_000 + timeOffset;
            if (record.request && record.request.body) record.request.body.attachments = [];
            if (record.response && record.response.body) record.response.body.attachments = [];
            if (record.request && record.request.raw_packet) record.request.raw_packet.attachments = [];
            if (record.response && record.response.raw_packet) record.response.raw_packet.attachments = [];
            return record;
        };

        const startedAt = performance.now();
        for (let seq = 1; seq <= 100; seq += 1) {
            client.receiveText(JSON.stringify({
                type: 'interaction', delivery: 'catch_up',
                record: withoutAttachments(protocolTemplate, seq, seq * 2),
            }));
            client.receiveText(JSON.stringify({
                type: 'interaction', delivery: 'catch_up',
                record: withoutAttachments(projectTemplate, seq, seq * 2 + 1),
            }));
        }
        const injectionElapsedMs = performance.now() - startedAt;
        await client.flushPersistence();
        const state = client.getState();
        return {
            injectionElapsedMs,
            protocolCacheInstanceId: protocolTemplate.cache_instance_id,
            projectCacheInstanceId: projectTemplate.cache_instance_id,
            recordCount: state.visibleRecords.length + state.pendingRecords.length,
            visibleCount: state.visibleRecords.length,
            pendingCount: state.pendingRecords.length,
            protocolCount: state.visibleRecords.concat(state.pendingRecords)
                .filter(record => record.scope === 'protocol').length,
            projectCount: state.visibleRecords.concat(state.pendingRecords)
                .filter(record => record.scope === 'project').length,
            protocolVisibleCount: state.visibleRecords.filter(record => record.scope === 'protocol').length,
            projectVisibleCount: state.visibleRecords.filter(record => record.scope === 'project').length,
            protocolPendingCount: state.pendingQueues.protocol.length,
            projectPendingCount: state.pendingQueues.notice.length,
            protocolCursor: state.protocolCursor,
            projectCursor: state.projectCursor,
            protocolDurableCursor: state.protocolDurableCursor,
            projectDurableCursor: state.projectDurableCursor,
        };
    });
}

/**
 * 测试思路：
 *
 * 测什么：
 * 验证真实桌面 Chromium 一次接收 100 条 protocol 和 100 条 project
 * catch-up 时，双 live/durable cursor 都连续到 100，重放最新唯一键不增加
 * 记录，选中详情不被重复帧夺走，抽屉仍能切换全屏。
 *
 * 为什么这么测：
 * 少量记录无法同时压到去重、双队列、持久化串行链和容量淘汰。
 * 即使独立容量断言失败，也需要单独证明游标和交互链路是否正常。
 *
 * 怎么测：
 * 1. 连接 Mock 抽屉，清空基准记录并暂停淡入消费。
 * 2. 以固定 cache instance 交替注入两个 scope 的 seq=1..100，并 flush IndexedDB。
 * 3. 断言注入本身在 5 秒内完成，四个游标的 seq 都为 100。
 * 4. 选中最新 protocol 记录，重放 protocol/project seq=100，断言总数和选中不变。
 * 5. 切换全屏并还原，断言控件可响应且无 pageerror/关键 console error。
 *
 * 示例：
 * protocol 1..100 + project 1..100 -> persist -> cursors 100/100
 * -> replay two seq=100 -> count unchanged, selection unchanged。
 */
test('大批量双 scope catch-up 保持游标、去重、详情和页面响应', async ({ page, context }) => {
    const pageErrors = [];
    const consoleErrors = [];
    page.on('pageerror', error => pageErrors.push(error.message));
    page.on('console', message => {
        if (message.type() === 'error') consoleErrors.push(message.text());
    });
    const drawer = await loginAndOpenDrawer(page, context);
    const injected = await injectLargeCatchUp(page);

    expect(injected.injectionElapsedMs).toBeLessThan(5_000);
    expect(injected.protocolCursor).toEqual({
        cacheInstanceId: injected.protocolCacheInstanceId,
        seq: 100,
    });
    expect(injected.projectCursor).toEqual({
        cacheInstanceId: injected.projectCacheInstanceId,
        seq: 100,
    });
    expect(injected.protocolDurableCursor).toEqual(injected.protocolCursor);
    expect(injected.projectDurableCursor).toEqual(injected.projectCursor);

    const beforeReplay = await page.evaluate(({ protocolCacheInstanceId, projectCacheInstanceId }) => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const state = client.getState();
        const records = state.visibleRecords.concat(state.pendingRecords);
        const protocol = records.find(record => record._key === `protocol:${protocolCacheInstanceId}:100`);
        const project = records.find(record => record._key === `project:${projectCacheInstanceId}:100`);
        if (!protocol || !project || !client.selectRecord(protocol._key)) {
            throw new Error('无法选中大批量测试的最新记录');
        }
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'catch_up', record: protocol }));
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'catch_up', record: project }));
        const after = client.getState();
        return {
            count: records.length,
            afterCount: after.visibleRecords.length + after.pendingRecords.length,
            selected: after.selectedRecordKey,
            selectedKey: protocol._key,
        };
    }, injected);
    expect(beforeReplay.afterCount).toBe(beforeReplay.count);
    expect(beforeReplay.selected).toBe(beforeReplay.selectedKey);
    await expect(drawer.getByTestId('protocol-interaction-detail-content')).not.toBeEmpty();

    await drawer.getByTestId('protocol-interaction-fullscreen').click();
    await expect(drawer).toHaveClass(/is-fullscreen/);
    await drawer.getByTestId('protocol-interaction-fullscreen').click();
    await expect(drawer).not.toHaveClass(/is-fullscreen/);
    expect(pageErrors).toEqual([]);
    expect(consoleErrors).toEqual([]);
});

/**
 * 测试思路：
 *
 * 测什么：
 * 验证抽屉对 protocol/project 两个 scope 分别使用容量上限，
 * 普通模式每个 scope 为 10 visible + 20 pending（总量 30），全屏模式每个 scope 为
 * 20 visible + 20 pending（总量 40）；两个 scope 合计达到 60/80 仍然合法。
 *
 * 为什么这么测：
 * 现有 Vitest 的 40 条容量用例只注入单一 protocol scope，无法发现
 * trim 逻辑是否错误地对每个 scope 分别应用 40 条上限。
 *
 * 怎么测：
 * 1. 在普通抽屉注入 100+100 条并暂停消费。
 * 2. 断言公开 limits 为 10/20，两个 scope 各自 pending 不超过 20 且各自 visible+pending 不超过 30。
 * 3. 切换全屏，断言 limits 为 20/20，两个 scope 重平衡后各自总数仍不超过 40。
 * 4. 同时记录两个 scope 的实际数量，确认一侧达到上限不会淘汰另一侧。
 *
 * 示例：
 * protocol=40 + project=40 是两个独立预算同时达到上限的合法结果；每侧分别 <= 40。
 */
test('双 scope 大批量记录分别遵守独立容量预算', async ({ page, context }) => {
    const drawer = await loginAndOpenDrawer(page, context);
    const normal = await injectLargeCatchUp(page);
    const normalLimits = await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const state = client.getState();
        return {
            maxVisibleRecords: state.maxVisibleRecords,
            maxPendingRecords: state.maxPendingRecords,
            visibleCount: state.visibleRecords.length,
            pendingCount: state.pendingRecords.length,
        };
    });
    expect(normalLimits).toMatchObject({ maxVisibleRecords: 10, maxPendingRecords: 20 });
    expect(normal.protocolPendingCount).toBeLessThanOrEqual(20);
    expect(normal.projectPendingCount).toBeLessThanOrEqual(20);
    expect(normal.protocolCount).toBeLessThanOrEqual(30);
    expect(normal.projectCount).toBeLessThanOrEqual(30);
    expect(normal.protocolCount).toBeGreaterThan(0);
    expect(normal.projectCount).toBeGreaterThan(0);

    await drawer.getByTestId('protocol-interaction-fullscreen').click();
    await expect(drawer).toHaveClass(/is-fullscreen/);
    const fullscreen = await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const state = client.getState();
        return {
            maxVisibleRecords: state.maxVisibleRecords,
            maxPendingRecords: state.maxPendingRecords,
            protocolVisibleCount: state.visibleRecords.filter(record => record.scope === 'protocol').length,
            projectVisibleCount: state.visibleRecords.filter(record => record.scope === 'project').length,
            protocolPendingCount: state.pendingQueues.protocol.length,
            projectPendingCount: state.pendingQueues.notice.length,
            protocolCount: state.visibleRecords.filter(record => record.scope === 'protocol').length
                + state.pendingQueues.protocol.length,
            projectCount: state.visibleRecords.filter(record => record.scope === 'project').length
                + state.pendingQueues.notice.length,
        };
    });
    expect(fullscreen).toMatchObject({ maxVisibleRecords: 20, maxPendingRecords: 20 });
    expect(fullscreen.protocolPendingCount).toBeLessThanOrEqual(20);
    expect(fullscreen.projectPendingCount).toBeLessThanOrEqual(20);
    expect(fullscreen.protocolCount).toBeLessThanOrEqual(40);
    expect(fullscreen.projectCount).toBeLessThanOrEqual(40);
    expect(fullscreen.protocolCount).toBeGreaterThan(0);
    expect(fullscreen.projectCount).toBeGreaterThan(0);
});

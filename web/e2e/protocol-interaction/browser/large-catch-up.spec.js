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
    // 大批量注入用例需要稳定的固定样本，周期 Mock 流由专门的 live-records 用例覆盖。
    await page.evaluate(() => {
        window.KitProxy.protocolInteractionLive.setMockScenario(1, 1, {
            liveEnabled: false,
        });
    });
    await drawer.getByTestId('protocol-interaction-connect').click();
    await expect(drawer.getByTestId('protocol-interaction-state')).toHaveText('实时');
    // 首批项目 Notice 与协议记录按合并队列渲染；本文件后续会自行注入并校验
    // 大批量双 scope 数据，准备阶段只需要已有可选中的记录。
    await expect.poll(() => drawer.getByTestId('protocol-interaction-record-item').count())
        .toBeGreaterThanOrEqual(3);
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
 * 普通模式和全屏模式都为 20 visible + 20 pending（每个 scope 总量 40）；
 * 普通模式只隐藏第二列，不再通过缩减 visible 容量触发重排。
 *
 * 为什么这么测：
 * 现有 Vitest 的 40 条容量用例只注入单一 protocol scope，无法发现
 * trim 逻辑是否错误地对每个 scope 分别应用 40 条上限。
 *
 * 怎么测：
 * 1. 在普通抽屉注入 100+100 条并暂停消费。
 * 2. 断言公开 limits 为 20/20，两个 scope 各自 pending 不超过 20 且各自 visible+pending 不超过 40。
 * 3. 切换全屏，断言 limits 保持 20/20，两个 scope 的记录无需重新加载即可进入双列。
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
    expect(normalLimits).toMatchObject({ maxVisibleRecords: 20, maxPendingRecords: 20 });
    expect(normal.protocolPendingCount).toBeLessThanOrEqual(20);
    expect(normal.projectPendingCount).toBeLessThanOrEqual(20);
    expect(normal.protocolCount).toBeLessThanOrEqual(40);
    expect(normal.projectCount).toBeLessThanOrEqual(40);
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

/**
 * 测试思路：
 *
 * 测什么：
 * 验证低高度桌面窗口中，常规抽屉保留 20 条记录，但只显示第一列的 10 条，
 * 可见记录保持可读且互不重叠。
 *
 * 为什么这么测：
 * 现场 1920x583 的抽屉一次补发 10 条 project Notice 和 10 条 protocol 记录。
 * 普通模式也保留两列容量，只隐藏第二列；列表不能只定义固定的少量 grid 行，
 * 否则隐式行会挤压前面的卡片并造成卡片文本相互覆盖。
 *
 * 怎么测：
 * 1. 使用与现场一致的低高度桌面视口，连接 Mock 抽屉。
 * 2. 注入 100 + 100 条 catch-up，使两个 scope 各保留 20 条可见记录。
 * 3. 测量第一列可见卡片的边界，断言卡片最小高度、两两无交集；第二列记录仍在 DOM 中但隐藏。
 *
 * 示例：
 * project(10) + protocol(10) -> 第一列 10 张可见卡片 + 第二列 10 张保留卡片 -> no overlap。
 */
test('低高度桌面下 20 条双 scope 补发记录保持完整且不重叠', async ({ page, context }, testInfo) => {
    await page.setViewportSize({ width: 1920, height: 583 });
    const drawer = await loginAndOpenDrawer(page, context);
    await injectLargeCatchUp(page);

    await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        if (!client) throw new Error('没有找到当前 Mock LiveClient');
        client.setMergeSpeed('fast');
        client.resumeMerge();
    });

    await expect.poll(() => drawer.getByTestId('protocol-interaction-record-item').count(), {
        timeout: 20_000,
    }).toBe(20);

    const layout = await page.evaluate(() => {
        const list = document.querySelector('[data-testid="protocol-interaction-record-list"]');
        if (!list) throw new Error('缺少交互记录列表');
        const cards = Array.from(list.querySelectorAll('.interaction-record-item')).map(card => {
            const rect = card.getBoundingClientRect();
            return {
                key: card.dataset.recordKey || '',
                top: rect.top,
                bottom: rect.bottom,
                left: rect.left,
                right: rect.right,
                height: rect.height,
                visible: rect.width > 0 && rect.height > 0,
            };
        });
        const visibleCards = cards.filter(card => card.visible);
        const overlaps = [];
        for (let index = 0; index < visibleCards.length; index += 1) {
            for (let next = index + 1; next < visibleCards.length; next += 1) {
                const left = visibleCards[index];
                const right = visibleCards[next];
                const intersects = left.left < right.right && left.right > right.left
                    && left.top < right.bottom && left.bottom > right.top;
                if (intersects) overlaps.push([left.key, right.key]);
            }
        }
        return {
            clientHeight: list.clientHeight,
            scrollHeight: list.scrollHeight,
            cards,
            visibleCards,
            overlaps,
        };
    });

    expect(layout.cards).toHaveLength(20);
    expect(layout.visibleCards).toHaveLength(10);
    // 普通模式卡片按可用高度均分，低高度视口下约 30px；重点验证稳定尺寸和无重叠。
    expect(layout.visibleCards.every(card => card.height >= 28)).toBe(true);
    expect(layout.overlaps).toEqual([]);
    await page.screenshot({
        path: testInfo.outputPath('protocol-interaction-low-height-catch-up.png'),
        fullPage: true,
    });
});

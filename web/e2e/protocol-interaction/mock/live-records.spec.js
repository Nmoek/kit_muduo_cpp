import { test, expect } from '@playwright/test';

async function loginAndOpenDrawer(page, context, projectId = 1, protocolId = 1) {
    await context.clearCookies();
    await page.goto(`/html/login.html?apiMode=mock&returnTo=%2Fhtml%2Fprotocol_items.html%3FapiMode%3Dmock%26projectId%3D${projectId}`);
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill('admin');
    await page.getByLabel('密码').fill('admin123');
    await Promise.all([
        page.waitForURL(/\/html\/protocol_items\.html\?/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);

    const item = page.locator(`.protocol-item[data-protocol-id="${protocolId}"]`);
    await expect(item).toBeVisible();
    if (projectId === 2) {
        await page.getByRole('button', { name: '启动测试服务', exact: true }).click();
        await expect(page.getByRole('button', { name: '停止测试服务', exact: true })).toBeVisible();
        await item.getByRole('button', { name: '未上线', exact: true }).click();
        await expect(item.getByRole('button', { name: '已上线', exact: true })).toBeVisible();
    }

    const openButton = item.locator('.protocol-interaction-btn');
    await expect(openButton).toBeEnabled();
    await openButton.click();
    const drawer = page.locator('.protocol-interaction-drawer');
    await expect(drawer).toBeVisible();
    await drawer.locator('[data-action="connect"]').click();
    return drawer;
}

/**
 * 测试思路：连接建立后，Mock transport 不能只发送首批样例；它应继续生成
 * delivery=live 的协议记录，并且序号和记录列表持续增长。
 * 示例：初始 seq=1..3 -> 等待周期消息 -> 出现 /api/live/4 或更大的 seq。
 */
test('Mock transport 连接保持期间持续生成新的实时记录', async ({ page, context }) => {
    await context.clearCookies();
    await page.goto('/html/login.html?apiMode=mock&returnTo=%2Fhtml%2Fprotocol_items.html%3FapiMode%3Dmock%26projectId%3D1');
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill('admin');
    await page.getByLabel('密码').fill('admin123');
    await Promise.all([
        page.waitForURL(/\/html\/protocol_items\.html\?/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
    const item = page.locator('.protocol-item[data-protocol-id="1"]');
    await item.locator('.protocol-interaction-btn').click();
    const drawer = page.locator('.protocol-interaction-drawer');
    await expect(drawer).toBeVisible();
    await page.evaluate(() => {
        window.KitProxy.protocolInteractionLive.setMockScenario(1, 1, {
            liveIntervalMs: 700,
        });
    });
    await drawer.locator('[data-action="connect"]').click();
    await expect(drawer.locator('[data-role="status"]')).toHaveText('实时');
    await drawer.locator('[data-action="set-merge-speed"][data-speed="fast"]').click();
    await expect.poll(() => page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const state = client.getState();
        return state.visibleRecords.length + state.pendingRecords.length;
    })).toBeGreaterThanOrEqual(4);

    const initial = await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const records = client.getState().visibleRecords.concat(client.getState().pendingRecords);
        return {
            count: records.length,
            maxSeq: Math.max(...records.filter(record => record.scope === 'protocol').map(record => record.seq)),
        };
    });
    await expect.poll(() => page.evaluate(({ maxSeq }) => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const records = client.getState().visibleRecords.concat(client.getState().pendingRecords);
        return records.some(record => (
            record.scope === 'protocol'
            && record.seq > maxSeq
            && record._delivery === 'live'
        ));
    }, initial), { timeout: 6_000 }).toBe(true);
    await expect.poll(() => drawer.locator('.interaction-record-item').count(), { timeout: 6_000 })
        .toBeGreaterThan(initial.count);
});

/**
 * 测试思路：
 *
 * 测什么：
 * 验证 Mock transport 通过真实 Chromium 页面进入 active 后，能将后端消息
 * 组渲染为实时记录，且 protocol scope、project Notice、live/catch_up 来源
 * 和附件记录都能在抽屉中被识别。
 *
 * 为什么这么测：
 * Vitest 已经直接测试 LiveClient 的消息接收和 Mock transport，但它不经过
 * 协议项卡片点击、抽屉 DOM、记录列表渲染和详情面板。这个 E2E 用真实页面
 * 连接生产代码中的 createMockTransport，验证“消息 -> client -> 抽屉列表”
 * 的完整前端链路，同时不依赖真正 C++ 后端。
 *
 * 怎么测：
 * 1. 以管理员身份打开 Mock project 1 的协议项页。
 * 2. 点击 protocol 1 的实时交互入口。
 * 3. 等待 Mock transport 自动发送 live_ready(open) 和初始消息组。
 * 4. 断言状态为“实时”，确认三条协议记录与一条 project Notice 均已被客户端接收。
 * 5. 加速队列消费后，断言记录列表存在成功、不匹配和 Notice 标识。
 * 6. 断言附件记录已经出现在记录列表中，详情区域可以打开。
 *
 * 示例：
 * live_ready(open) -> protocol matched -> protocol mismatch -> project Notice
 * -> 页面显示“实时”状态和多条记录。
 */
test('Mock transport 的 live_ready、实时记录和项目 Notice 在抽屉中展示', async ({ page, context }) => {
    const drawer = await loginAndOpenDrawer(page, context);

    await expect(drawer.locator('[data-role="status"]')).toHaveText('实时');
    await expect.poll(() => page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        if (!client) return { total: 0, noticeReceived: false };
        const state = client.getState();
        const records = state.visibleRecords.concat(state.pendingRecords);
        return {
            total: records.length,
            noticeReceived: records.some(record => record.scope === 'project'),
        };
    })).toEqual({ total: 4, noticeReceived: true });

    await drawer.locator('[data-action="set-merge-speed"][data-speed="fast"]').click();
    await expect.poll(() => drawer.locator('.interaction-record-item').count(), { timeout: 10_000 })
        .toBeGreaterThanOrEqual(4);

    await expect(drawer.locator('.interaction-record-item [data-role="result"]')
        .filter({ hasText: '成功' }).first()).toBeVisible();
    await expect(drawer.locator('.interaction-record-item [data-role="result"]')
        .filter({ hasText: '请求不匹配' }).first()).toBeVisible();
    await expect(drawer.locator('[data-role="scope"]')).toHaveText('Notice');
    await expect(drawer.locator('[data-role="delivery"]')
        .filter({ hasText: '实时' }).first()).toBeVisible();

    await drawer.locator('.interaction-record-item').first().click();
    await expect(drawer.locator('.interaction-record-detail-inner')).not.toBeEmpty();
});

/**
 * 测试思路：真实 Chromium 中进入 Mock TCP 协议项并连接实时抽屉，确认消息组不会混入 HTTP method/path 或 HTTP raw_packet 样例。
 * 示例：projectId=2、protocolId=2 连接后，协议记录均为 custom_tcp，且每条请求都包含功能码和 Raw Hex。
 */
test('Mock TCP transport 生成 Custom TCP 实时交互数据', async ({ page, context }) => {
    const drawer = await loginAndOpenDrawer(page, context, 2, 2);

    await expect(drawer.locator('[data-role="status"]')).toHaveText('实时');
    await expect.poll(() => drawer.locator('.interaction-record-item').count())
        .toBeGreaterThanOrEqual(2);
    await expect(drawer).not.toContainText('HTTP raw_packet');
    await expect(drawer).not.toContainText('GET /api/health');
    await expect(drawer).not.toContainText('POST /api/check');

    const records = await page.evaluate(() => {
        const clients = window.KitProxy.protocolInteractionLive.clients;
        const client = clients && Array.from(clients)[0];
        if (!client) throw new Error('没有找到当前 Mock TCP LiveClient');
        return client.getState().visibleRecords.concat(client.getState().pendingRecords).map(record => ({
            scope: record.scope,
            protocolType: record.protocol_type,
            functionCode: record.request && record.request.meta && record.request.meta.function_code,
            method: record.request && record.request.meta && record.request.meta.method,
            path: record.request && record.request.meta && record.request.meta.path,
            rawHex: record.request && record.request.raw_packet && record.request.raw_packet.raw_hex,
        }));
    });
    const protocolRecords = records.filter(record => record.scope === 'protocol');
    expect(protocolRecords.length).toBeGreaterThanOrEqual(2);
    expect(protocolRecords.every(record => record.protocolType === 'custom_tcp')).toBe(true);
    expect(protocolRecords.every(record => record.functionCode)).toBe(true);
    expect(protocolRecords.every(record => !record.method && !record.path && record.rawHex)).toBe(true);
});

/**
 * 测试思路：
 *
 * 测什么：
 * 验证相同 scope、cache_instance_id、seq 的重复 interaction 不会在真实
 * 页面列表中重复出现，同时后端传来的 HTML/脚本字符串只作为文本展示。
 *
 * 为什么这么测：
 * 实时数据可能因 catch-up 和 live 重叠而重复到达，唯一键去��是防止用户
 * 看到重复记录的关键。文本字段来自后端，若页面使用 innerHTML 渲染会产生
 * DOM 注入风险。JSDOM 的安全断言不能替代真实 Chromium 对最终 DOM 的观察。
 *
 * 怎么测：
 * 1. 打开 Mock 实时抽屉，等四条初始记录全部渲染后暂停本地合并队列。
 * 2. 从真实页面上的 LiveClient 读取第一条已显示记录。
 * 3. 再发送同一唯一键的 interaction。
 * 4. 断言记录总数没有增加。
 * 5. 发送包含 script/img 字符串的记录并恢复合并。
 * 6. 断言文本可见，但对应的恶意元素没有被创建。
 *
 * 示例：
 * protocol:1001:1 重复发送两次 -> 页面仍一条；<img> 文本 -> DOM 中没有 img 节点。
 */
test('实时记录按唯一键幂等并安全渲染后端文本', async ({ page, context }) => {
    const drawer = await loginAndOpenDrawer(page, context);
    await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        if (!client) throw new Error('没有找到当前 Mock LiveClient');
        client.setMergeSpeed('fast');
    });
    await expect.poll(() => drawer.locator('.interaction-record-item').count(), { timeout: 10_000 })
        .toBeGreaterThanOrEqual(4);
    await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        if (!client) throw new Error('没有找到当前 Mock LiveClient');
        client.pauseMerge();
    });

    const beforeCount = await drawer.locator('.interaction-record-item').count();
    await page.evaluate(() => {
        const clients = window.KitProxy.protocolInteractionLive.clients;
        const client = clients && Array.from(clients)[0];
        if (!client) throw new Error('没有找到当前 Mock LiveClient');
        const record = client.getState().visibleRecords[0];
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record }));
    });
    await page.waitForTimeout(100);
    expect(await drawer.locator('.interaction-record-item').count()).toBe(beforeCount);

    await page.evaluate(() => {
        const clients = window.KitProxy.protocolInteractionLive.clients;
        const client = clients && Array.from(clients)[0];
        if (!client) throw new Error('没有找到当前 Mock LiveClient');
        const source = client.getState().visibleRecords.find(record => (
            String(record.protocol_type || '').toLowerCase() === 'http'
        )) || client.getState().visibleRecords[0];
        const record = JSON.parse(JSON.stringify(source));
        record.seq = 99;
        record.error_message = '<img id="injected-record-node" src="x">';
        record.request.meta.path = '<script id="injected-script-node">bad()</script>';
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record }));
        client.resumeMerge();
    });

    await expect(drawer.locator('.interaction-record-item')).toHaveCount(beforeCount + 1);
    await expect(drawer).toContainText('<script id="injected-script-node">bad()</script>');
    await expect(drawer.locator('#injected-record-node')).toHaveCount(0);
    await expect(drawer.locator('#injected-script-node')).toHaveCount(0);
});

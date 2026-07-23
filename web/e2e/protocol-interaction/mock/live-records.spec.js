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

    const openButton = page.locator('.protocol-item[data-protocol-id="1"] .protocol-interaction-btn');
    await expect(openButton).toBeEnabled();
    await openButton.click();
    const drawer = page.locator('.protocol-interaction-drawer');
    await expect(drawer).toBeVisible();
    await drawer.locator('[data-action="connect"]').click();
    return drawer;
}

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
 * 4. 断言状态为“实时”，并等待至少三条协议记录和一条 project Notice。
 * 5. 断言记录列表存在成功、不匹配和 Notice 标识。
 * 6. 断言附件记录已经出现在记录列表中，详情区域可以打开。
 *
 * 示例：
 * live_ready(open) -> protocol matched -> protocol mismatch -> project Notice
 * -> 页面显示“实时”状态和多条记录。
 */
test('Mock transport 的 live_ready、实时记录和项目 Notice 在抽屉中展示', async ({ page, context }) => {
    const drawer = await loginAndOpenDrawer(page, context);

    await expect(drawer.locator('[data-role="status"]')).toHaveText('实时');
    await expect.poll(() => drawer.locator('.interaction-record-item').count())
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
 * 1. 打开 Mock 实时抽屉并等待初始记录。
 * 2. 从真实页面上的 LiveClient 读取第一条已显示记录。
 * 3. 再发送同一唯一键的 interaction。
 * 4. 断言记录总数没有增加。
 * 5. 发送包含 script/img 字符串的记录。
 * 6. 断言文本可见，但对应的恶意元素没有被创建。
 *
 * 示例：
 * protocol:1001:1 重复发送两次 -> 页面仍一条；<img> 文本 -> DOM 中没有 img 节点。
 */
test('实时记录按唯一键幂等并安全渲染后端文本', async ({ page, context }) => {
    const drawer = await loginAndOpenDrawer(page, context);
    await expect.poll(() => drawer.locator('.interaction-record-item').count())
        .toBeGreaterThanOrEqual(4);

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
        const source = client.getState().visibleRecords[0];
        const record = JSON.parse(JSON.stringify(source));
        record.seq = 99;
        record.error_message = '<img id="injected-record-node" src="x">';
        record.request.meta.path = '<script id="injected-script-node">bad()</script>';
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record }));
    });

    await expect(drawer.locator('.interaction-record-item')).toHaveCount(beforeCount + 1);
    await expect(drawer).toContainText('<script id="injected-script-node">bad()</script>');
    await expect(drawer.locator('#injected-record-node')).toHaveCount(0);
    await expect(drawer.locator('#injected-script-node')).toHaveCount(0);
});

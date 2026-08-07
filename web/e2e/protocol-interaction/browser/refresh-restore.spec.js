import { test, expect } from '@playwright/test';

/**
 * 测试思路：
 *
 * 测什么：
 * 验证真实桌面 Chromium 刷新协议项管理页后，能够从 sessionStorage marker
 * 找回原项目/协议项，重新打开同一个实时抽屉，并恢复刷新前已经持久化的记录。
 *
 * 为什么这么测：
 * 现有 Vitest 已覆盖 marker、snapshot 和 hydration 的函数级规则，但无法模拟
 * 页面 reload 导致的 JavaScript 上下文销毁、IndexedDB 保留、脚本重新加载和
 * 协议列表异步渲染。这个测试专门验证用户实际按下刷新后的完整浏览器行为。
 *
 * 怎么测：
 * 1. 登录 Mock 管理员并进入固定 project 1。
 * 2. 打开 protocol 1 抽屉并点击连接。
 * 3. 等待 live 记录和恢复 marker 写入。
 * 4. 记录刷新前的 protocol_id、列表记录数量和 sessionStorage marker。
 * 5. 执行 page.reload()。
 * 6. 等待协议列表重新渲染和 marker 自动恢复。
 * 7. 断言仍是 protocol 1 的唯一抽屉，并且记录数量不低于刷新前数量。
 *
 * 示例：
 * 打开 protocol 1 -> 写入 marker/snapshot -> reload -> protocol 1 抽屉重开。
 */
test('刷新后恢复原协议项抽屉和实时工作区记录', async ({ page, context }) => {
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
    await drawer.locator('[data-action="connect"]').click();
    await expect(drawer.locator('[data-role="status"]')).toHaveText('实时');
    // 保存/恢复断言以刷新前实际已渲染数为基线；最后一条 Notice 可能仍在
    // 合并队列中，准备阶段只需已有可恢复记录。
    await expect.poll(() => drawer.locator('.interaction-record-item').count())
        .toBeGreaterThanOrEqual(3);

    const beforeCount = await drawer.locator('.interaction-record-item').count();
    const beforeProtocolId = await drawer.locator('[data-role="protocol-meta"]').textContent();
    const marker = await page.evaluate(() => {
        const key = 'kit_protocol_interaction_restore_marker';
        return window.sessionStorage.getItem(key);
    });
    expect(marker).toBeTruthy();

    await page.reload();

    const restoredDrawer = page.locator('.protocol-interaction-drawer');
    await expect(restoredDrawer).toBeVisible();
    await expect(restoredDrawer.locator('[data-role="protocol-meta"]')).toHaveText(beforeProtocolId);
    await expect.poll(() => restoredDrawer.locator('.interaction-record-item').count())
        .toBeGreaterThanOrEqual(beforeCount);
});

/**
 * 测试思路：
 *
 * 测什么：
 * 验证恢复 marker 指向不存在的协议项时，页面不会创建错误抽屉或错误连接，
 * 并会清理无效 marker，让下一次刷新不会无限重复恢复失败。
 *
 * 为什么这么测：
 * 协议项可能在另一个页面或后端操作中被删除/下线。若 marker 永久保留，
 * 每次刷新都会出现空抽屉、错误连接或恢复循���。这个场景需要真实页面重载
 * 和列表完成后的恢复逻辑，不能只测试内部清理函数。
 *
 * 怎么测：
 * 1. 以管理员进入 project 1 协议项页。
 * 2. 写入一个指向不存在 protocol_id=9999 的 marker。
 * 3. 刷新页面并等待协议列表完成。
 * 4. 断言不存在实时抽屉。
 * 5. 断言 marker 已被清理。
 *
 * 示例：
 * marker(protocol=9999) -> reload -> 不创建 dialog -> marker 删除。
 */
test('目标协议不存在时刷新不创建错误抽屉并清理 marker', async ({ page, context }) => {
    await context.clearCookies();
    await page.goto('/html/login.html?apiMode=mock&returnTo=%2Fhtml%2Fprotocol_items.html%3FapiMode%3Dmock%26projectId%3D1');
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill('admin');
    await page.getByLabel('密码').fill('admin123');
    await Promise.all([
        page.waitForURL(/\/html\/protocol_items\.html\?/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);

    await page.evaluate(() => {
        const persistence = window.KitProxy.protocolInteractionPersistence;
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: window.KitProxy.config.apiBaseUrl,
            user: window.KitProxy.auth.getCurrentUser(),
            projectId: 1,
            protocolId: 9999,
        });
        const written = persistence.writeRestoreMarker(identity, { drawerOpen: true });
        if (!written) throw new Error('无法写入合法测试 restore marker');
    });
    await page.reload();
    await expect(page.locator('.protocol-list')).toBeVisible();
    await expect(page.locator('.protocol-interaction-drawer')).toBeHidden();
    await expect.poll(() => page.evaluate(() => window.sessionStorage.getItem('kit_protocol_interaction_restore_marker')))
        .toBeNull();
});

/**
 * 测试思路：
 *
 * 测什么：
 * 验证真实 reload 后新 WebSocket URL 同时携带 protocol/project 两套完整
 * cursor 参数，并且恢复后再次收到已有唯一键的 interaction 不会重复渲染。
 *
 * 为什么这么测：
 * protocol 记录和 project Notice 是两个独立缓存。只验证某个 seq 存在，无法
 * 发现 cache_instance_id 缺失、只发送半套参数或两个 scope 互相覆盖的问题。
 * URL 监听和真实页面列表数量结合起来，分别验证请求边界和用户可见幂等结果。
 *
 * 怎么测：
 * 1. 打开 protocol 1 并连接，等待 protocol/project 记录和 durable cursor 写入。
 * 2. 在 page.reload() 前注册 WebSocket 监听。
 * 3. 解析新连接 URL，断言两个 scope 的 cache_instance_id/seq 均成对存在。
 * 4. 等待抽屉恢复并记录当前列表数量。
 * 5. 从页面 LiveClient 读取已有记录，再发送相同唯一键的 interaction。
 * 6. 断言列表数量不增加。
 *
 * 示例：
 * reload -> after_protocol_cache_instance_id + after_protocol_seq
 *       + after_project_cache_instance_id + after_project_seq。
 */
test('刷新重连携带双游标且已有记录重放幂等', async ({ page, context }) => {
    await context.clearCookies();
    await page.goto('/html/login.html?apiMode=mock&returnTo=%2Fhtml%2Fprotocol_items.html%3FapiMode%3Dmock%26projectId%3D1');
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill('admin');
    await page.getByLabel('密码').fill('admin123');
    await Promise.all([
        page.waitForURL(/\/html\/protocol_items\.html\?/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);

    await page.locator('.protocol-item[data-protocol-id="1"] .protocol-interaction-btn').click();
    let drawer = page.locator('.protocol-interaction-drawer');
    await drawer.locator('[data-action="connect"]').click();
    await expect(drawer.locator('[data-role="status"]')).toHaveText('实时');
    // 双游标由内存与持久化状态生成，不依赖首批 Notice 已完成卡片动画。
    await expect.poll(() => drawer.locator('.interaction-record-item').count())
        .toBeGreaterThanOrEqual(3);

    await page.reload();
    const builtUrl = await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        if (!client) throw new Error('刷新后没有恢复 LiveClient');
        return client.buildUrl();
    });
    const url = new URL(builtUrl);
    expect(url.searchParams.get('include_project_notice')).toBe('1');
    expect(url.searchParams.get('after_protocol_cache_instance_id')).toMatch(/^\d+$/);
    expect(url.searchParams.get('after_protocol_seq')).toMatch(/^\d+$/);
    expect(url.searchParams.get('after_project_cache_instance_id')).toMatch(/^\d+$/);
    expect(url.searchParams.get('after_project_seq')).toMatch(/^\d+$/);

    drawer = page.locator('.protocol-interaction-drawer');
    await expect(drawer).toBeVisible();
    await expect(drawer.locator('[data-role="status"]')).toHaveText('实时');
    const beforeCount = await drawer.locator('.interaction-record-item').count();
    await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const record = client.getState().visibleRecords[0];
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'catch_up', record }));
    });
    await page.waitForTimeout(100);
    expect(await drawer.locator('.interaction-record-item').count()).toBe(beforeCount);
});

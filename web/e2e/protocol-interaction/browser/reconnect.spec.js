import { test, expect } from '@playwright/test';

async function loginAndOpen(page, context) {
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
    const drawer = page.locator('.protocol-interaction-drawer');
    await expect(drawer).toBeVisible();
    return drawer;
}

/**
 * 测试思路：
 *
 * 测什么：
 * 验证异常 WebSocket 关闭会让页面进入等待重连状态，按照退避策略创建
 * 新 session，并最终恢复为实时；同时确认新 session 不复用旧 session_id。
 *
 * 为什么这么测：
 * 断线重连涉及连接对象、定时器、session_id 和连接意图。Vitest 已覆盖
 * LiveClient 的假 transport，但真实 Chromium 还需要验证抽屉状态展示和页面
 * 连接按钮是否与 client 状态同步。
 *
 * 怎么测：
 * 1. 打开抽屉但先不连接。
 * 2. 通过页面生产 API 设置 failNextConnection。
 * 3. 点击连接并等待第一次 live active。
 * 4. 记录第一次 session_id。
 * 5. 等待 Mock transport 模拟 1006 异常关闭。
 * 6. 断言显示等待重连。
 * 7. 等待退避时间后断言回到实时且 session_id 改变。
 *
 * 示例：
 * session A active -> close(1006) -> reconnect_wait -> session B active。
 */
test('异常断开按退避策略重连并建立新 session', async ({ page, context }) => {
    const drawer = await loginAndOpen(page, context);
    await page.evaluate(() => {
        window.KitProxy.protocolInteractionLive.setMockScenario(1, 1, {
            failNextConnection: true,
        });
    });
    await drawer.locator('[data-action="connect"]').click();
    await expect(drawer.locator('[data-role="status"]')).toHaveText('实时');
    const firstSessionId = await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        return client.getState().sessionId;
    });

    await expect(drawer.locator('[data-role="status"]')).toHaveText('等待重连', { timeout: 5_000 });
    await expect(drawer.locator('[data-role="status"]')).toHaveText('实时', { timeout: 5_000 });
    const secondSessionId = await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        return client.getState().sessionId;
    });
    expect(secondSessionId).not.toBe(firstSessionId);
});

/**
 * 测试思路：
 *
 * 测什么：
 * 验证用户点击“断开”后进入 disconnected，连接意图变为主动断开，等待
 * 超过重连窗口后也不会自动建立新连接。
 *
 * 为什么这么测：
 * 异常断开应重连，但主动断开不应重连。若两者没有区分，用户关闭查看后
 * 页面仍会后台创建连接，造成资源泄漏、重复推送和退出页面后回调异常。
 *
 * 怎么测：
 * 1. 打开并连接 Mock 抽屉。
 * 2. 点击断开。
 * 3. 断言断开按钮动作完成后状态为未连接或断开中再转为未连接。
 * 4. 等待超过默认重连时间。
 * 5. 断言状态仍为未连接，且 client 的 session_id 保持清零，不会创建新 session。
 *
 * 示例：
 * active -> 用户断开 -> disconnected/session_id=0 -> 等待 1.5 秒 -> 仍 disconnected。
 */
test('主动断开不触发自动重连', async ({ page, context }) => {
    const drawer = await loginAndOpen(page, context);
    await drawer.locator('[data-action="connect"]').click();
    await expect(drawer.locator('[data-role="status"]')).toHaveText('实时');
    const clientStateBefore = await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        return client.getState().sessionId;
    });

    await drawer.locator('[data-action="disconnect"]').click();
    await expect(drawer.locator('[data-role="status"]')).toHaveText('未连接');
    await page.waitForTimeout(1_500);
    await expect(drawer.locator('[data-role="status"]')).toHaveText('未连接');
    const clientStateAfter = await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        return client.getState().sessionId;
    });
    expect(clientStateBefore).toBeGreaterThan(0);
    expect(clientStateAfter).toBe(0);
});

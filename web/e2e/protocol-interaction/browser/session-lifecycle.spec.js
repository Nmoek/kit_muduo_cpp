import { test, expect } from '@playwright/test';

async function loginAndOpenConnected(page, context) {
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
    await drawer.locator('[data-action="connect"]').click();
    await expect(drawer.locator('[data-role="status"]')).toHaveText('实时');
    await expect.poll(() => drawer.locator('.interaction-record-item').count())
        .toBeGreaterThanOrEqual(4);
    return drawer;
}

/**
 * 测试思路：
 *
 * 测什么：
 * 验证 session 过期后的被动登录跳转不会删除实时 workspace marker，重新
 * 登录后仍能返回原协议项页面并恢复抽屉。
 *
 * 为什么这么测：
 * 被动 401 和主动退出有相反的清理语义。被动过期时用户只是需要重新认证，
 * 不应丢失可恢复工作区；只有显式退出才清理当前用户数据。该流程还涉及
 *真实 reload、Cookie、sessionStorage 和登录 returnTo，必须用浏览器验证。
 *
 * 怎么测：
 * 1. 打开连接并等待 marker 写入。
 * 2. 清除浏览器 Cookie，模拟 session 过期。
 * 3. reload，断言跳转 login.html。
 * 4. 断言 marker 仍存在。
 * 5. 重新登录，断言回到 protocol_items.html。
 * 6. 断言原 protocol 1 抽屉恢复。
 *
 * 示例：
 * active + marker -> cookie expired -> login -> marker 保留 -> 登录 -> 抽屉恢复。
 */
test('被动 session 过期保留 workspace 并在重新登录后恢复', async ({ page, context }) => {
    const drawer = await loginAndOpenConnected(page, context);
    await expect(drawer).toBeVisible();
    const markerBefore = await page.evaluate(() => window.sessionStorage.getItem('kit_protocol_interaction_restore_marker'));
    expect(markerBefore).toBeTruthy();

    await context.clearCookies();
    await page.reload();
    await expect(page).toHaveURL(/\/html\/login\.html\?/);
    const markerDuringLogin = await page.evaluate(() => window.sessionStorage.getItem('kit_protocol_interaction_restore_marker'));
    expect(markerDuringLogin).toBeTruthy();

    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill('admin');
    await page.getByLabel('密码').fill('admin123');
    await Promise.all([
        page.waitForURL(/\/html\/protocol_items\.html\?/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
    await expect(page.locator('.protocol-interaction-drawer')).toBeVisible();
    await expect(page.locator('.protocol-interaction-drawer [data-role="protocol-meta"]'))
        .toContainText('protocol_id=1');
});

/**
 * 测试思路：
 *
 * 测什么：
 * 验证用户主动点击退出登录后，当前用户的恢复 marker 被清除，之后刷新
 * 不会自动重新打开旧的协议项抽屉。
 *
 * ��什么这么测：
 * 主动退出表示用户明确结束当前身份，继续保留 marker 会让下一个登录用户
 * 或同一浏览器后续页面错误恢复旧工作区，形成状态泄露风险。这个场景和
 * 被动 401 必须分别验证，不能共享一个“跳转登录”断言。
 *
 * 怎么测：
 * 1. 打开连接并确认 marker 存在。
 * 2. 先收起抽屉但不销毁工作区，避免抽屉遮挡页面顶部的退出按钮。
 * 3. 点击页面的退出登录按钮。
 * 4. 等待跳转 login.html。
 * 5. 断言 marker 已删除。
 * 6. 刷新登录页或重新访问协议项页，断言不出现旧抽屉。
 *
 * 示例：
 * active + marker -> 退出登录 -> marker 删除 -> 后续访问无旧抽屉。
 */
test('主动退出清理当前 workspace marker', async ({ page, context }) => {
    await loginAndOpenConnected(page, context);
    await expect.poll(() => page.evaluate(() => window.sessionStorage.getItem('kit_protocol_interaction_restore_marker')))
        .not.toBeNull();

    await page.locator('.protocol-interaction-drawer [data-action="close"]').click();
    await expect(page.locator('.protocol-interaction-drawer')).toBeHidden();
    await page.getByRole('button', { name: '退出登录' }).click();
    await expect(page).toHaveURL(/\/html\/login\.html/);
    await expect.poll(() => page.evaluate(() => window.sessionStorage.getItem('kit_protocol_interaction_restore_marker')))
        .toBeNull();

    await page.goto('/html/protocol_items.html?apiMode=mock&projectId=1');
    await expect(page).toHaveURL(/\/html\/login\.html/);
    await expect(page.locator('.protocol-interaction-drawer')).toBeHidden();
});

/**
 * 测试思路：
 *
 * 测什么：
 * 验证退出接口抛出异常时，页面仍然清理当前用户的恢复 marker 并跳转登录页。
 *
 * 为什么这么测：
 * 显式退出的安全边界不能依赖后端接口成功。网络失败、服务端 5xx 或连接中断
 * 都不应该让当前用户的工作区继续留在浏览器中，否则下一个会话可能恢复旧状态。
 * Vitest 已覆盖 auth.logout 的 finally 分支，这里补充真实 Chromium 中按钮点击、
 * 异步 IndexedDB 清理和页面跳转的组合行为。
 *
 * 怎么测：
 * 1. 打开实时抽屉并确认 marker 存在。
 * 2. 在页面内将 logout API 替换为必定失败的函数。
 * 3. 收起抽屉后点击退出登录。
 * 4. 断言仍跳转到 login.html，且 marker 被删除。
 *
 * 示例：
 * active + marker -> logout throws -> 清理 workspace -> login.html 且无 marker。
 */
test('退出接口失败时仍清理 workspace marker', async ({ page, context }) => {
    await loginAndOpenConnected(page, context);
    await expect.poll(() => page.evaluate(() => window.sessionStorage.getItem('kit_protocol_interaction_restore_marker')))
        .not.toBeNull();

    await page.evaluate(() => {
        window.KitProxy.api.logout = async () => {
            throw new Error('模拟退出接口失败');
        };
    });
    await page.locator('.protocol-interaction-drawer [data-action="close"]').click();
    await expect(page.locator('.protocol-interaction-drawer')).toBeHidden();
    await page.getByRole('button', { name: '退出登录' }).click();

    await expect(page).toHaveURL(/\/html\/login\.html/);
    await expect.poll(() => page.evaluate(() => window.sessionStorage.getItem('kit_protocol_interaction_restore_marker')))
        .toBeNull();
});

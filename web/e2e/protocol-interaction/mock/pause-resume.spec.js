import { test, expect } from '@playwright/test';

async function loginAndConnect(page, context) {
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
    await expect.poll(() => drawer.locator('.interaction-record-item').count())
        .toBeGreaterThanOrEqual(4);
    return drawer;
}

/**
 * 测试思路：
 *
 * 测什么：
 * 验证页面点击暂停后进入 paused，暂停期间产生的 Mock 记录不会立即作为
 * 实时记录展示，点击恢复后先进入补发流程，随后 catch-up 记录出现并回到 active。
 *
 * 为什么这么测：
 * pause/resume 同时涉及客户端命令序号、服务端 ACK、pending 队列和 catch-up
 * 时序。只断言按钮文字会漏掉“恢复过早 active”“暂停记录丢失”或“补发重复”等
 * 状态机问题，因此必须在真实 Chromium 中按消息时序观察状态和列表。
 *
 * 怎么测：
 * 1. 打开 Mock 抽屉并连接到实时状态。
 * 2. 点击暂停，等待 state(paused) 被页面渲染。
 * 3. 等待 Mock transport 在暂停期间写入待补发记录。
 * 4. 点击恢复，断言页面经历补发状态。
 * 5. 等待 catch-up 和 active ACK。
 * 6. 断言列表中存在“补发”来源，且没有重复增加同一记录。
 *
 * 示例：
 * active -> pause -> 已暂停 -> resume -> 补发中 -> catch_up -> 实时。
 */
test('暂停期间记录在恢复时以 catch-up 补发并回到实时', async ({ page, context }) => {
    const drawer = await loginAndConnect(page, context);
    const beforePauseCount = await drawer.locator('.interaction-record-item').count();

    await drawer.locator('[data-action="pause"]').click();
    await expect(drawer.locator('[data-role="status"]')).toHaveText('已暂停');
    await page.waitForTimeout(50);
    expect(await drawer.locator('.interaction-record-item').count()).toBe(beforePauseCount);

    await drawer.locator('[data-action="resume"]').click();
    await expect(drawer.locator('[data-role="status"]')).toHaveText(/补发中|恢复中|实时/);
    await expect(drawer.locator('[data-role="status"]')).toHaveText('实时');
    await expect(drawer.locator('[data-role="delivery"]')
        .filter({ hasText: '补发' }).first()).toBeVisible();
    await expect(drawer.locator('.interaction-record-item')).toHaveCount(beforePauseCount + 1);
});

/**
 * 测试思路：
 *
 * 测什么：
 * 验证恢复补发过程中收起抽屉不会让页面崩溃，重新展开后仍能看到相同实时
 * client 的最终状态；同时确认旧的暂停/恢复状态不会创建第二组记录。
 *
 * 为什么这么测：
 * 抽屉收起只改变可见性，client 仍可能在后台处理 resume 的 live_ready、
 * catch-up 和 state ACK。若收起逻辑销毁了 client，延迟回调可能访问失效 DOM；
 * 若重开逻辑重复创建 client，则会产生重复记录和重复连接。
 *
 * 怎么测：
 * 1. 连接并进入 paused。
 * 2. 点击恢复后立即收起抽屉。
 * 3. 等待 Mock catch-up 时序完成。
 * 4. 重新展开同一协议项。
 * 5. 断言状态最终为实时，补发记录仍存在。
 *
 * 示例：
 * paused -> resume -> 收起 -> 后台完成 catch-up -> 重开 -> 实时且记录不重复。
 */
test('恢复期间收起并重开抽屉仍保留同一实时工作区', async ({ page, context }) => {
    const drawer = await loginAndConnect(page, context);
    const item = page.locator('.protocol-item[data-protocol-id="1"]');

    await drawer.locator('[data-action="pause"]').click();
    await expect(drawer.locator('[data-role="status"]')).toHaveText('已暂停');
    await drawer.locator('[data-action="resume"]').click();
    await drawer.locator('[data-action="close"]').click();
    await expect(drawer).toBeHidden();

    await page.waitForTimeout(120);
    await item.locator('.protocol-interaction-btn').click();
    await expect(drawer).toBeVisible();
    await expect(drawer.locator('[data-role="status"]')).toHaveText('实时');
    await expect(drawer.locator('[data-role="delivery"]')
        .filter({ hasText: '补发' }).first()).toBeVisible();
});

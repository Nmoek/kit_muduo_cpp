import { test, expect } from '@playwright/test';

const loginAsAdmin = async page => {
    await page.goto('/html/login.html?apiMode=mock&returnTo=%2Fhtml%2Fprotocol_items.html%3FapiMode%3Dmock%26projectId%3D1');
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill('admin');
    await page.getByLabel('密码').fill('admin123');
    await Promise.all([
        page.waitForURL(/\/html\/protocol_items\.html\?/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
    await expect(page.locator('.protocol-item')).toHaveCount(2);
};

/**
 * 测试思路：
 *
 * 测什么：
 * 验证运行中且已上线的协议项，其“查看协议项实时交互详情”入口可用，
 * 点击后会在桌面页面右侧创建并显示实时交互抽屉。
 *
 * 为什么这么测：
 * 入口可用性由 project runtime_state、protocol config_state 和删除状态共同
 * 决定。抽屉又是动态创建的真实 DOM，Vitest 的 JSDOM 测试不能完全证明按钮
 * 点击后 backdrop、dialog、焦点和桌面抽屉样式在 Chromium 中一起工作。
 * 这里不要求静态 Mock 服务器提供 WebSocket，连接生命周期由后续 browser/real
 * 用例验证；本用例只隔离验证入口和抽屉展示。
 *
 * 怎么测：
 * 1. 以管理员身份进入 Mock project 1 协议项页。
 * 2. 定位 protocol 1 的实时交互入口，确认按钮未禁用。
 * 3. 点击按钮，等待动态 dialog 出现。
 * 4. 断言标题、protocol_id 元信息和右侧抽屉可见。
 *
 * 示例：
 * project runtime=1 + config_state=1 -> 点击入口 -> dialog 可见。
 */
test('已上线运行中的协议项可以打开实时交互抽屉', async ({ page, context }) => {
    await context.clearCookies();
    await loginAsAdmin(page);

    const item = page.locator('.protocol-item[data-protocol-id="1"]');
    const openButton = item.locator('.protocol-interaction-btn');
    await expect(openButton).toBeEnabled();
    await openButton.click();

    const drawer = page.locator('.protocol-interaction-drawer');
    await expect(drawer).toBeVisible();
    await expect(drawer).toHaveAttribute('role', 'dialog');
    await expect(drawer.locator('#protocol-interaction-drawer-title')).toHaveText('HTTP健康检查示例');
    await expect(drawer.locator('[data-role="protocol-meta"]')).toContainText('protocol_id=1');
});

/**
 * 测试思路：
 *
 * 测什么：
 * 验证收起抽屉只改变可见性，重新展开仍能复用同一页面工作区；同时验证
 * 项目未运行或协议项未上线时入口被禁用，并显示具体原因。
 *
 * 为什么这么测：
 * 收起不等于断开是当前产品生命周期约定。若实现把收起当作销毁，用户
 * 重新展开时会丢失当前窗口状态或创建重复 client。入口禁用原因则是用户
 * 是否能正常使用实时功能的第一道条件，不能只依赖按钮颜色判断。
 *
 * 怎么测：
 * 1. 打开 project 1 的 protocol 1 抽屉。
 * 2. 点击“收起抽屉”，断言 panel 和 backdrop 隐藏。
 * 3. 再次点击相同入口，断言标题和 protocol_id 仍正确。
 * 4. 断言静态 Mock 没有 WebSocket 服务时“断开”按钮保持禁用，避免把真实
 *    WebSocket 生命周期混入本 Mock 用例。
 * 5. 访问停止中的 project 2，断言 protocol 2 入口禁用并提示先启动服务。
 *
 * 示例：
 * 打开 -> 收起 -> 重开保持同一 protocol_id���runtime=0 -> 入口 disabled。
 */
test('收起后可重新展开且不可用运行态入口显示原因', async ({ page, context }) => {
    await context.clearCookies();
    await loginAsAdmin(page);

    const item = page.locator('.protocol-item[data-protocol-id="1"]');
    const openButton = item.locator('.protocol-interaction-btn');
    await openButton.click();

    const drawer = page.locator('.protocol-interaction-drawer');
    await expect(drawer).toBeVisible();
    await drawer.locator('[data-action="close"]').click();
    await expect(drawer).toBeHidden();
    await expect(page.locator('.protocol-interaction-backdrop')).toBeHidden();

    await openButton.click();
    await expect(drawer).toBeVisible();
    await expect(drawer.locator('[data-role="protocol-meta"]')).toContainText('protocol_id=1');
    await expect(drawer.locator('[data-action="disconnect"]')).toBeDisabled();

    await page.goto('/html/protocol_items.html?apiMode=mock&projectId=2');
    await expect(page.locator('.protocol-item[data-protocol-id="2"] .protocol-interaction-btn'))
        .toBeDisabled();
    await expect(page.locator('.protocol-item[data-protocol-id="2"] .protocol-interaction-btn'))
        .toHaveAttribute('title', '请先启动测试服务');
});

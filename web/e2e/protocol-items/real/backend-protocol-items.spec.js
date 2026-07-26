import { test, expect } from '@playwright/test';
import { REAL_E2E, protocolItemSelector, protocolItemsPath } from '../../protocol-interaction/real/test_config.js';

/**
 * 测试思路：
 *
 * 测什么：真实隔离后端指定项目的协议项列表、服务上下文和核心入口。
 * 为什么这么测：Mock 已覆盖列表操作，Real 只需证明真实接口字段能驱动列表页，并保留新增入口。
 * 怎么测：真实管理员登录后访问配置项目，检查服务标题、指定协议项和新增按钮。
 * 示例：真实登录 -> protocol_items.html?projectId=1 -> protocolId=1 卡片可见。
 */
test('真实后端协议项列表读取冒烟', async ({ page, context }) => {
    await context.clearCookies();
    await page.goto('/html/login.html');
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill(REAL_E2E.adminNote);
    await page.getByLabel('密码').fill(REAL_E2E.adminPassword);
    await Promise.all([
        page.waitForURL(/\/html\/main\.html/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);

    await page.goto(protocolItemsPath());
    await expect(page.locator('#protocol-items-title')).toBeVisible();
    await expect(page.locator(protocolItemSelector())).toBeVisible();
    await expect(page.locator('#protocol-items-title')).toContainText('协议项管理：');
    await expect(page.locator(`${protocolItemSelector()} .protocol-name`)).toHaveText(/\S+/);
    await expect(page.locator(`${protocolItemSelector()} .protocol-tag`)).toHaveText(/\S+/);
    await expect(page.locator('#add-protocol-item')).toBeEnabled();
});

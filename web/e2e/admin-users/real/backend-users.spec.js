import { test, expect } from '@playwright/test';
import { REAL_E2E } from '../../protocol-interaction/real/test_config.js';

/**
 * 测试思路：
 *
 * 测什么：真实后端管理员页面、用户列表、状态筛选和新增入口的最小读取冒烟。
 * 为什么这么测：Mock 已覆盖用户增删改和状态流程，Real 只确认管理员权限、字段契约和列表接口可以渲染。
 * 怎么测：使用隔离后端管理员登录用户页，检查至少一行用户、状态筛选、刷新和新增按钮。
 * 示例：真实登录 -> admin_users.html -> 用户行 -> 状态筛选/新增用户可见。
 */
test('真实后端管理员页面和列表读取冒烟', async ({ page, context }) => {
    await context.clearCookies();
    await page.goto('/html/login.html');
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill(REAL_E2E.adminNote);
    await page.getByLabel('密码').fill(REAL_E2E.adminPassword);
    await Promise.all([
        page.waitForURL(/\/html\/main\.html/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);

    await page.goto('/html/admin_users.html');
    await expect(page.locator('#admin-users-body tr:not(.admin-users-empty-row)').first()).toBeVisible();
    await expect(page.locator('#user-status-filter')).toBeVisible();
    await expect(page.locator('#refresh-users-btn')).toBeVisible();
    await expect(page.locator('#add-user-btn')).toBeVisible();
});

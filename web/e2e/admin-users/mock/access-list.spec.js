import { test, expect } from '@playwright/test';
import { loginAsAdmin, loginAsNormal } from '../../helpers/auth.js';

/**
 * 测试思路：
 *
 * 测什么：普通用户不能通过菜单或直接 URL 访问用户管理，管理员可以看到列表、筛选和分页。
 * 为什么这么测：管理员页面必须同时有导航层和初始化守卫，单独隐藏菜单不能防止直接访问 URL 绕过权限。
 * 怎么测：普通用户先检查菜单和直接访问结果，再清理会话以管理员登录并检查用户行、状态筛选和分页。
 * 示例：普通用户 -> admin_users.html -> 无权限；管理员 -> admin_users.html -> admin/testuser 列表。
 */
test('普通用户无权限访问用户管理', async ({ page, context }) => {
    await loginAsNormal(page, context, '/html/main.html?apiMode=mock');
    await expect(page.locator('[data-admin-users-nav]')).toBeHidden();
    await page.goto('/html/admin_users.html?apiMode=mock');
    await expect(page.locator('.admin-users-denied')).toContainText('无权限访问用户管理');
    await expect(page.locator('#admin-users-body tr:not(.admin-users-empty-row)')).toHaveCount(0);
});

/**
 * 测试思路：
 *
 * 测什么：管理员用户列表、正常/已停用筛选、刷新按钮和分页控件。
 * 为什么这么测：用户管理的主要工作流是列表扫描，权限守卫、状态参数和分页请求必须在真实 DOM 中组合正确。
 * 怎么测：管理员登录用户页，检查 admin/testuser，切换状态筛选并刷新，确认页码和表格状态稳定。
 * 示例：状态=正常 -> 活跃用户可见；状态=已停用 -> 空状态或停用用户可见；刷新不重置筛选。
 */
test('管理员列表、状态筛选和分页', async ({ page, context }) => {
    await loginAsAdmin(page, context, '/html/admin_users.html?apiMode=mock');
    await expect(page.locator('#admin-users-body')).toContainText('admin');
    await expect(page.locator('#admin-users-body')).toContainText('testuser');
    await expect(page.locator('#users-page-info')).toHaveText('第 1 页');
    await expect(page.locator('#users-prev-page')).toBeDisabled();

    await page.selectOption('#user-status-filter', 'active');
    await expect(page.locator('#admin-users-body')).toContainText('admin');
    await expect(page.locator('#admin-users-body')).toContainText('testuser');
    await page.click('#refresh-users-btn');
    await expect(page.locator('#user-status-filter')).toHaveValue('active');

    await page.selectOption('#user-status-filter', 'disabled');
    await expect(page.locator('.admin-users-empty-row, #admin-users-body tr.is-disabled')).toHaveCount(1);
});

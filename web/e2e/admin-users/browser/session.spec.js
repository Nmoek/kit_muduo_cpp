import { test, expect } from '@playwright/test';
import { loginAsAdmin, loginAsNormal } from '../../helpers/auth.js';

/**
 * 测试思路：
 *
 * 测什么：管理员退出后用户管理页重新执行登录和权限校验，普通用户重新登录后仍然被拒绝。
 * 为什么这么测：管理员页面不能依赖一次性内存状态，否则退出后可能保留旧权限。
 * 怎么测：管理员进入用户页并退出，再访问同页检查 login returnTo；普通用户登录后直接访问检查无权限。
 * 示例：admin_users.html -> 退出 -> login.html?returnTo=... -> normal -> 无权限页面。
 */
test('退出登录后重新触发权限校验', async ({ page, context }) => {
    await loginAsAdmin(page, context, '/html/admin_users.html?apiMode=mock');
    await page.getByRole('button', { name: '退出登录' }).click();
    await expect(page).toHaveURL(/\/html\/login\.html/);

    await page.goto('/html/admin_users.html?apiMode=mock');
    await expect(page).toHaveURL(/\/html\/login\.html\?/);
    expect(new URL(page.url()).searchParams.get('returnTo')).toContain('/html/admin_users.html');

    await loginAsNormal(page, context, '/html/admin_users.html?apiMode=mock');
    await expect(page.locator('.admin-users-denied')).toContainText('无权限访问用户管理');
});

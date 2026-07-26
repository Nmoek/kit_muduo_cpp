import { test, expect } from '@playwright/test';
import { installMockStatePersistence } from '../../helpers/auth.js';

const MOCK_MAIN_PATH = '/html/main.html?apiMode=mock';

async function loginAsAdmin(page, context, returnTo = MOCK_MAIN_PATH) {
    await installMockStatePersistence(context);
    await context.clearCookies();
    await page.goto(`/html/login.html?apiMode=mock&returnTo=${encodeURIComponent(returnTo)}`);
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill('admin');
    await page.getByLabel('密码').fill('admin123');
    await Promise.all([
        page.waitForURL(new RegExp(returnTo.includes('protocol_items') ? '\\/html\\/protocol_items\\.html' : '\\/html\\/main\\.html')),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
}

/**
 * 测试思路：
 *
 * 测什么：验证管理员登录后的 Cookie 会话可跨页面刷新和业务页面跳转继续使用，并且 /auth/me 结果能渲染
 * 当前用户条和管理员导航。
 * 为什么这么测：页面刷新会重新创建 JavaScript 状态，只有浏览器 Cookie 和当前用户初始化都正确时才会
 * 继续保持登录；跨到协议项页还可以确认保护逻辑没有只在 main.html 生效。
 * 怎么测：
 * 1. 管理员登录 main.html，确认 kit_mock_session Cookie 和用户条。
 * 2. 刷新 main.html，重新检查用户条和管理员菜单。
 * 3. 跳转 protocol_items.html?projectId=1，检查页面仍未回登录页且协议项加载。
 *
 * 示例：admin 登录 -> Cookie 写入 -> 刷新仍是 admin -> protocol_items.html 仍是 admin。
 */
test('Cookie 会话在刷新和业务页面跳转后保持登录', async ({ page, context }) => {
    await loginAsAdmin(page, context);
    await expect(page.locator('.user-session-bar .user-note')).toHaveText('admin');

    expect((await context.cookies()).some(cookie => cookie.name === 'kit_mock_session')).toBe(true);

    await page.reload();
    await expect(page).toHaveURL(/\/html\/main\.html\?apiMode=mock/);
    await expect(page.locator('.user-session-bar .user-note')).toHaveText('admin');
    await expect(page.locator('.user-session-bar .user-role')).toHaveText('管理员');
    await expect(page.locator('[data-admin-users-nav]')).toBeVisible();

    await page.goto('/html/protocol_items.html?apiMode=mock&projectId=1');
    await expect(page).toHaveURL(/\/html\/protocol_items\.html\?apiMode=mock&projectId=1/);
    await expect(page.locator('#protocol-items-title')).toContainText('HTTP测试服务示例');
    await expect(page.locator('.user-session-bar .user-note')).toHaveText('admin');
    await expect(page.locator('.user-session-bar .user-role')).toHaveText('管理员');
});

/**
 * 测试思路：
 *
 * 测什么：验证主动退出会删除 Mock Cookie、回到登录页，并在退出后重新访问受保护页面时生成安全 returnTo；
 * 随后使用普通用户重新登录，确认同一个浏览器可以建立新的会话。
 * 为什么这么测：退出是会话生命周期的安全边界。只检查页面跳转而不检查 Cookie，会漏掉退出后旧会话仍能
 * 访问业务页面的问题；再次登录则确认清理没有破坏正常认证入口。
 * 怎么测：
 * 1. 管理员登录 main.html，点击“退出登录”。
 * 2. 断言回到 login.html、没有 returnTo、Mock Cookie 已清除。
 * 3. 直接访问 main.html，断言被送回 login.html，并且 returnTo 仅指向同源 main.html。
 * 4. 使用 testuser 普通登录，断言回到 main.html 且身份变为普通用户。
 *
 * 示例：admin -> 退出 -> login.html -> 访问 main.html -> login.html?returnTo=/html/main.html -> testuser 登录。
 */
test('退出清理 Cookie 后受保护页面要求重新登录', async ({ page, context }) => {
    await loginAsAdmin(page, context);
    await page.getByRole('button', { name: '退出登录' }).click();
    await expect(page).toHaveURL(/\/html\/login\.html\?apiMode=mock$/);
    expect(new URL(page.url()).searchParams.get('returnTo')).toBeNull();
    expect((await context.cookies()).some(cookie => cookie.name === 'kit_mock_session')).toBe(false);

    await page.goto(MOCK_MAIN_PATH);
    await expect(page).toHaveURL(/\/html\/login\.html\?apiMode=mock&returnTo=/);
    const loginUrl = new URL(page.url());
    expect(loginUrl.origin).toBe('http://127.0.0.1:4173');
    expect(loginUrl.searchParams.get('returnTo')).toBe('/html/main.html?apiMode=mock');

    await page.getByLabel('note').fill('testuser');
    await Promise.all([
        page.waitForURL(/\/html\/main\.html\?apiMode=mock/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
    await expect(page.locator('.user-session-bar .user-note')).toHaveText('testuser');
    await expect(page.locator('.user-session-bar .user-role')).toHaveText('普通用户');
});

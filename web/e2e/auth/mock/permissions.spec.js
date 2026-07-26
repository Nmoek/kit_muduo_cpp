import { test, expect } from '@playwright/test';
import { installMockStatePersistence } from '../../helpers/auth.js';

const MOCK_MAIN_PATH = '/html/main.html?apiMode=mock';

async function loginAsMockUser(page, context, note, returnTo = MOCK_MAIN_PATH, password = '') {
    await installMockStatePersistence(context);
    await context.clearCookies();
    await page.goto(`/html/login.html?apiMode=mock&returnTo=${encodeURIComponent(returnTo)}`);
    if (password) {
        await page.getByRole('button', { name: '切换为管理员登录' }).click();
        await page.getByLabel('密码').fill(password);
    }
    await page.getByLabel('note').fill(note);
    await Promise.all([
        page.waitForURL(new RegExp(returnTo.includes('admin_users') ? '\\/html\\/admin_users\\.html' : '\\/html\\/main\\.html')),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
}

function adminNav(page) {
    return page.locator('[data-admin-users-nav]');
}

async function assertUserPanel(page, note, role) {
    await expect(page.locator('.user-session-bar')).toBeVisible();
    await expect(page.locator('.user-session-bar .user-note')).toHaveText(note);
    await expect(page.locator('.user-session-bar .user-role')).toHaveText(role);
    await expect(page.getByRole('button', { name: '退出登录' })).toBeVisible();
}

/**
 * 测试思路：
 *
 * 测什么：验证普通用户登录后主页面不显示用户管理和控制面板入口，同时头部显示普通用户身份。
 * 为什么这么测：菜单是权限边界的第一层，普通用户即使能加载业务页面，也不应看到管理员入口；头部
 * 身份条则用于确认导航权限与当前用户对象来自同一次认证，而不是静态 HTML 偶然呈现。
 * 怎么测：
 * 1. 使用 Mock 内置 testuser 登录 main.html。
 * 2. 检查管理员导航节点不存在，并检查“控制面板”文本不存在。
 * 3. 检查用户 note、角色和退出按钮。
 *
 * 示例：testuser -> main.html -> 无“用户管理”且无“控制面板”、显示“普通用户”。
 */
test('普通用户只看到业务入口并隐藏管理员菜单', async ({ page, context }) => {
    await loginAsMockUser(page, context, 'testuser');

    await expect(adminNav(page)).toHaveCount(0);
    await expect(page.getByRole('link', { name: '用户管理' })).toHaveCount(0);
    await expect(page.getByText('控制面板', { exact: true })).toHaveCount(0);
    await assertUserPanel(page, 'testuser', '普通用户');
});

/**
 * 测试思路：
 *
 * 测什么：验证管理员登录后显示用户管理、隐藏当前未开放的控制面板，并在主页面、协议项页、协议项表单
 * 和用户管理页保持一致的用户信息条与管理员导航。
 * 为什么这么测：管理员菜单由 auth.js 动态同步，多个业务 HTML 又各自带有静态导航壳；跨页面检查可以
 * 暴露静态节点未删除、动态节点未插入或调试参数丢失等问题。
 * 怎么测：
 * 1. 登录管理员 admin，检查 main.html 的用户管理可见、控制面板不可见。
 * 2. 依次访问 protocol_items.html?projectId=1、protocol_item_form.html?projectId=1 和 admin_users.html。
 * 3. 每页检查用户管理可见、控制面板不可见，并检查管理员 note、角色和退出按钮。
 *
 * 示例：admin -> main.html / protocol_items.html / protocol_item_form.html / admin_users.html，管理员入口始终可见。
 */
test('管理员菜单和用户身份在各业务页面保持一致', async ({ page, context }) => {
    await loginAsMockUser(page, context, 'admin', MOCK_MAIN_PATH, 'admin123');

    const assertAdminNavigation = async () => {
        await expect(adminNav(page)).toBeVisible();
        await expect(page.getByRole('link', { name: '用户管理' })).toBeVisible();
        await expect(page.getByText('控制面板', { exact: true })).toHaveCount(0);
        await assertUserPanel(page, 'admin', '管理员');
    };

    await assertAdminNavigation();

    await page.goto('/html/protocol_items.html?apiMode=mock&projectId=1');
    await expect(page.locator('#protocol-items-title')).toContainText('HTTP测试服务示例');
    await assertAdminNavigation();

    await page.goto('/html/protocol_item_form.html?apiMode=mock&projectId=1');
    await expect(page.locator('#protocol-form-title')).toContainText('添加协议项');
    await assertAdminNavigation();

    await page.goto('/html/admin_users.html?apiMode=mock');
    await expect(page.locator('.admin-users-page')).toBeVisible();
    await expect(page.locator('#admin-users-body tr')).toHaveCount(2);
    await assertAdminNavigation();
});

/**
 * 测试思路：
 *
 * 测什么：验证普通用户直接访问 admin_users.html 时不能读取用户列表，并得到明确的无权限页面。
 * 为什么这么测：隐藏菜单只是展示层保护，不能替代管理员页面入口守卫；直接输入 URL 是普通用户绕过菜单
 * 时最现实的访问方式，必须确认 requireAdmin 在页面层仍然拒绝请求。
 * 怎么测：
 * 1. 以 testuser 登录并进入主页面，确认用户管理菜单不存在。
 * 2. 直接打开 /html/admin_users.html?apiMode=mock。
 * 3. 断言仍停留用户管理页，但显示“无权限访问用户管理”、新增按钮禁用且没有用户行。
 *
 * 示例：testuser -> 直接访问 admin_users.html -> 无权限提示 -> 用户列表不加载。
 */
test('普通用户直接访问用户管理页被拒绝', async ({ page, context }) => {
    await loginAsMockUser(page, context, 'testuser');
    await expect(adminNav(page)).toHaveCount(0);

    await page.goto('/html/admin_users.html?apiMode=mock');
    await expect(page.locator('.admin-users-denied h3')).toHaveText('无权限访问用户管理');
    await expect(page.locator('#add-user-btn')).toBeDisabled();
    await expect(page.locator('#admin-users-body tr')).toHaveCount(0);
});

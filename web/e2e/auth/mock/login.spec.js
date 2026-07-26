import { test, expect } from '@playwright/test';
import { installMockStatePersistence } from '../../helpers/auth.js';

const MOCK_LOGIN_PATH = '/html/login.html?apiMode=mock';

async function openMockLogin(page, context, returnTo = '') {
    await installMockStatePersistence(context);
    await context.clearCookies();
    const query = returnTo
        ? `&returnTo=${encodeURIComponent(returnTo)}`
        : '';
    await page.goto(`${MOCK_LOGIN_PATH}${query}`);
}

/**
 * 测试思路：
 *
 * 测什么：验证 Mock 登录页的默认普通用户模式、页面基础元素，以及普通登录和管理员模式切换。
 * 为什么这么测：登录页是所有业务页面的共同入口；模式切换如果没有同步更新密码控件、按钮状态和
 * aria 状态，后续普通用户与管理员流程会向错误的接口参数提交。
 * 怎么测：
 * 1. 清空 Cookie 后打开 Mock 登录页，检查标题、Beta 版本标识、note 输入框和登录按钮。
 * 2. 检查默认普通模式下密码框禁用且模式按钮为未按下。
 * 3. 点击“切换管理员登录”，检查密码框可见、可填写、必填且 aria-pressed=true。
 * 4. 再次切换普通模式，检查密码被清空、控件禁用且 aria-pressed=false。
 *
 * 示例：普通登录 -> 管理员登录（密码可用） -> 普通登录（密码清空并禁用）。
 */
test('登录页默认普通模式并可完整切换管理员模式', async ({ page, context }) => {
    await openMockLogin(page, context);

    await expect(page).toHaveTitle('通用安检机协议半自动测试平台');
    await expect(page.locator('.login-title')).toContainText('通用安检机协议半自动测试平台');
    await expect(page.locator('#loginVersionBadge')).toHaveText('Beta');
    await expect(page.getByLabel('note')).toBeVisible();
    await expect(page.getByRole('button', { name: '登录', exact: true })).toBeVisible();

    const modeSwitch = page.locator('#loginModeSwitch');
    const passwordGroup = page.locator('#adminPasswordGroup');
    const password = page.getByLabel('密码');

    await expect(modeSwitch).toHaveAttribute('aria-pressed', 'false');
    await expect(modeSwitch).toHaveAttribute('aria-label', '切换为管理员登录');
    await expect(passwordGroup).toHaveAttribute('aria-hidden', 'true');
    await expect(password).toBeDisabled();

    await modeSwitch.click();
    await expect(modeSwitch).toHaveAttribute('aria-pressed', 'true');
    await expect(modeSwitch).toHaveAttribute('aria-label', '切换为普通用户登录');
    await expect(passwordGroup).toBeVisible();
    await expect(passwordGroup).toHaveAttribute('aria-hidden', 'false');
    await expect(password).toBeEnabled();
    await expect(password).toHaveAttribute('required', '');
    await password.fill('temporary-password');

    await modeSwitch.click();
    await expect(modeSwitch).toHaveAttribute('aria-pressed', 'false');
    await expect(modeSwitch).toHaveAttribute('aria-label', '切换为管理员登录');
    await expect(passwordGroup).toHaveAttribute('aria-hidden', 'true');
    await expect(password).toBeDisabled();
    await expect(password).toHaveValue('');
});

/**
 * 测试思路：
 *
 * 测什么：验证 Mock 普通用户登录能够建立浏览器会话并跳转主页面，页面随后显示当前用户信息。
 * 为什么这么测：普通登录不需要密码，但必须经过真实表单提交、Mock Cookie 写入和业务页的 /auth/me
 * 初始化；只断言登录页按钮变化无法证明会话真的可跨页面使用。
 * 怎么测：
 * 1. 打开 Mock 登录页并填写内置普通用户 testuser。
 * 2. 等待跳转到 main.html，同时检查 kit_mock_session Cookie 已写入。
 * 3. 断言用户条显示 testuser 和“普通用户”。
 *
 * 示例：testuser -> main.html?apiMode=mock -> 用户条显示 testuser / 普通用户。
 */
test('普通用户登录建立 Mock Cookie 会话并进入主页面', async ({ page, context }) => {
    await openMockLogin(page, context);
    await page.getByLabel('note').fill('testuser');

    await Promise.all([
        page.waitForURL(/\/html\/main\.html\?apiMode=mock/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);

    await expect(page.locator('.user-session-bar')).toBeVisible();
    await expect(page.locator('.user-session-bar .user-note')).toHaveText('testuser');
    await expect(page.locator('.user-session-bar .user-role')).toHaveText('普通用户');

    const cookies = await context.cookies();
    expect(cookies.find(cookie => cookie.name === 'kit_mock_session')).toMatchObject({
        value: '2',
        path: '/',
    });
});

/**
 * 测试思路：
 *
 * 测什么：验证空 note、非法 note、管理员空密码和错误管理员密码都会停留在登录页并显示错误提示。
 * 为什么这么测：这些是客户端校验与 Mock 登录失败语义的最短路径；失败时不能写入会话，也不能让用户
 * 被误认为已经登录，否则后续受保护页面会出现身份污染。
 * 怎么测：
 * 1. 提交空 note，断言“请输入 note”。
 * 2. 提交包含非法字符且长度不足的 note，断言格式错误。
 * 3. 切换管理员模式并提交空密码，断言管理员密码提示。
 * 4. 提交 admin 和错误密码，断言错误凭据提示、仍在 login.html 且没有 Mock 会话 Cookie。
 *
 * 示例：空 note -> 请输入 note；a -> note 格式错误；admin + wrong-password -> 登录失败。
 */
test('非法输入和错误管理员凭据不会建立会话', async ({ page, context }) => {
    await openMockLogin(page, context);

    await page.getByRole('button', { name: '登录', exact: true }).click();
    await expect(page.locator('#loginError')).toHaveText('请输入 note');
    await expect(page).toHaveURL(/\/html\/login\.html\?apiMode=mock$/);

    await page.getByLabel('note').fill('a!');
    await page.getByRole('button', { name: '登录', exact: true }).click();
    await expect(page.locator('#loginError')).toHaveText('note 必须是 3-32 位英文字母和数字');
    await expect(page).toHaveURL(/\/html\/login\.html\?apiMode=mock$/);

    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill('admin');
    await page.getByRole('button', { name: '登录', exact: true }).click();
    await expect(page.locator('#loginError')).toHaveText('管理员登录需要填写密码');
    await expect(page).toHaveURL(/\/html\/login\.html\?apiMode=mock$/);

    await page.getByLabel('密码').fill('wrong-password');
    await page.getByRole('button', { name: '登录', exact: true }).click();
    await expect(page.locator('#loginError')).toHaveText('管理员 note 或密码错误');
    await expect(page).toHaveURL(/\/html\/login\.html\?apiMode=mock$/);
    expect((await context.cookies()).some(cookie => cookie.name === 'kit_mock_session')).toBe(false);
});

/**
 * 测试思路：
 *
 * 测什么：验证外部 returnTo 被拒绝并回退到同源主页面，同时验证合法同源 returnTo 能保留目标页面。
 * 为什么这么测：returnTo 同时影响登录后的导航和开放重定向安全边界。需要用真实浏览器提交成功登录，
 * 才能确认 URL 解析、Cookie 和最终 location.href 是组合正确的，而不是只测一个工具函数。
 * 怎么测：
 * 1. 使用 returnTo=https://evil.example/ 登录管理员，断言最终 origin 仍是测试站点且路径为 main.html。
 * 2. 清空会话后使用 /html/protocol_items.html?apiMode=mock&projectId=1 登录管理员。
 * 3. 断言最终 URL 是该同源协议项页，并且协议项标题和列表已加载。
 *
 * 示例：外站 returnTo -> http://127.0.0.1:4173/html/main.html；同源 returnTo -> /html/protocol_items.html。
 */
test('登录后只接受安全 returnTo 并拒绝开放重定向', async ({ page, context }) => {
    await openMockLogin(page, context, 'https://evil.example/');
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill('admin');
    await page.getByLabel('密码').fill('admin123');
    await Promise.all([
        page.waitForURL(/\/html\/main\.html\?apiMode=mock/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);

    const externalReturnResult = new URL(page.url());
    expect(externalReturnResult.origin).toBe('http://127.0.0.1:4173');
    expect(externalReturnResult.pathname).toBe('/html/main.html');
    expect(externalReturnResult.searchParams.get('returnTo')).toBeNull();

    await openMockLogin(page, context, '/html/protocol_items.html?apiMode=mock&projectId=1');
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill('admin');
    await page.getByLabel('密码').fill('admin123');
    await Promise.all([
        page.waitForURL(/\/html\/protocol_items\.html\?apiMode=mock&projectId=1/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);

    await expect(page.locator('#protocol-items-title')).toContainText('HTTP测试服务示例');
    await expect(page.locator('.protocol-item')).toHaveCount(2);
});

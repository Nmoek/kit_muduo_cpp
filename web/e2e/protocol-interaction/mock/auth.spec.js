import { test, expect } from '@playwright/test';

const protocolItemsUrl = '/html/protocol_items.html?apiMode=mock&projectId=1';

/**
 * 测试思路：
 *
 * 测什么：
 * 验证未登录访问协议项管理页时会跳转登录页，并且登录 URL 保留安全的
 * returnTo，登录成功后能回到原协议项页面并加载协议项列表。
 *
 * 为什么这么测：
 * 实时交互抽屉入口位于协议项管理页，认证跳转错误会让后续抽屉测试在
 * 登录页或错误页面上执行。该行为还涉及真实浏览器的页面跳转和 Cookie，
 * 仅靠现有 JSDOM/Vitest 逻辑测试不能确认页面最终位置和列表渲染结果。
 *
 * 怎么测：
 * 1. 清空当前浏览器 context 的 Cookie 和存储状态。
 * 2. 访问未登录的协议项页面。
 * 3. 断言跳转到 login.html，并检查 returnTo 指向同源协议项页面。
 * 4. 切换管理员登录，填写内置示例管理员账号并提交。
 * 5. 等待返回协议项页面，断言项目元信息和协议项卡片可见。
 *
 * 示例：
 * protocol_items.html -> login.html?returnTo=... -> 登录 -> 协议项列表可见。
 */
test('未登录访问协议项页后登录并返回协议项列表', async ({ page, context }) => {
    await context.clearCookies();
    await page.goto(protocolItemsUrl);

    await expect(page).toHaveURL(/\/html\/login\.html\?/);
    const loginUrl = new URL(page.url());
    expect(loginUrl.searchParams.get('returnTo')).toContain('/html/protocol_items.html');

    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill('admin');
    await page.getByLabel('密码').fill('admin123');
    await Promise.all([
        page.waitForURL(/\/html\/protocol_items\.html\?/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);

    await expect(page.locator('#protocol-items-title')).toContainText('HTTP测试服务示例');
    await expect(page.locator('.protocol-item')).toHaveCount(2);
});

/**
 * 测试思路：
 *
 * 测什么：
 * 验证登录表单对非法 note、错误用户和外部 returnTo 的处理，确保失败登录
 * 不会建立错误会话，也不会形成开放重定向。
 *
 * 为什么这么测：
 * 登录失败状态是协议项 E2E 的公共前置。如果失败后页面错误地保留成功态，
 * 或者 returnTo 可以跳到外站，后续测试会产生不可靠结果并引入安全风险。
 * 该用例使用真实 Chromium 的表单校验、错误提示和 URL 变化进行验证。
 *
 * 怎么测：
 * 1. 直接打开带外部 returnTo 的 login.html。
 * 2. 提交非法 note，检查表单错误。
 * 3. 提交合法但密码错误的普通用户场景，检查登录失败提示。
 * 4. 检查当前 URL 仍在本站登录页，未跳到外部地址。
 *
 * 示例：
 * 非法 note -> 表单错误；错误用户 -> 登录失败；外部 returnTo -> 不跳转。
 */
test('登录失败和外部 returnTo 不建立错误会话', async ({ page, context }) => {
    await context.clearCookies();
    await page.goto('/html/login.html?apiMode=mock&returnTo=https%3A%2F%2Fevil.example%2F');

    await page.getByLabel('note').fill('a');
    await page.getByRole('button', { name: '登录', exact: true }).click();
    await expect(page.locator('#loginError')).toContainText('note 必须是 3-32 位英文字母和数字');

    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill('admin');
    await page.getByLabel('密码').fill('wrong-password');
    await page.getByRole('button', { name: '登录', exact: true }).click();
    await expect(page.locator('#loginError')).toContainText('管理员 note 或密码错误');
    await expect(page).toHaveURL(/\/html\/login\.html/);
    expect(new URL(page.url()).origin).toBe('http://127.0.0.1:4173');
});

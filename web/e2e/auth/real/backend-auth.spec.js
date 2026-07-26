import { test, expect } from '@playwright/test';
import { REAL_E2E } from '../../protocol-interaction/real/test_config.js';

function captureJsonRoute(page, path) {
    return new Promise((resolve, reject) => {
        page.route(`**${path}`, async route => {
            try {
                const response = await route.fetch();
                const payload = await response.json();
                await route.fulfill({ response });
                resolve({ response, payload });
            } catch (error) {
                reject(error);
            }
        }).catch(reject);
    });
}

/**
 * 测试思路：
 *
 * 测什么：验证 Real 配置下真实 C++ 后端能提供登录页，管理员登录能建立真实 Cookie，/auth/me 能返回
 * 当前用户并驱动主页面渲染，最后 /auth/logout 能清除会话并回到登录页。
 * 为什么这么测：Mock 只能证明前端状态机，无法发现真实后端的 Cookie 名称、JSON 字段、业务 code 或认证路由
 * 不一致。这个最小冒烟覆盖登录、当前用户和退出三条真实认证链路，不重复 Mock 场景。
 * 怎么测：
 * 1. 使用现有 Real webServer 和 REAL_E2E.adminNote/adminPassword 打开真实 login.html。
 * 2. 监听 /auth/login 与 /auth/me 响应，提交管理员登录并断言 HTTP/业务 code 及返回用户字段。
 * 3. 断言主页面用户条显示管理员身份且 kit_session Cookie 存在。
 * 4. 点击退出，监听 /auth/logout，断言响应成功、返回 login.html 且 Cookie 已清除。
 *
 * 示例：真实 login.html -> POST /auth/login -> GET /auth/me -> main.html -> POST /auth/logout -> login.html。
 */
test('真实后端管理员登录、当前用户和退出冒烟', async ({ page, context }) => {
    const loginPageResponse = await page.goto('/html/login.html');
    expect(loginPageResponse).not.toBeNull();
    expect(loginPageResponse.status()).toBe(200);
    await expect(page.locator('#loginForm')).toBeVisible();

    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill(REAL_E2E.adminNote);
    await page.getByLabel('密码').fill(REAL_E2E.adminPassword);

    const loginRoutePromise = captureJsonRoute(page, '/auth/login');
    const currentUserRoutePromise = captureJsonRoute(page, '/auth/me');
    await page.getByRole('button', { name: '登录', exact: true }).click();
    const { response: loginResponse, payload: loginPayload } = await loginRoutePromise;
    const { payload: currentUserPayload } = await currentUserRoutePromise;
    expect(loginResponse.status()).toBe(200);
    expect(loginPayload).toMatchObject({
        code: 0,
        data: {
            note_name: REAL_E2E.adminNote,
            role: 'admin',
        },
    });
    expect(currentUserPayload).toMatchObject({
        code: 0,
        data: {
            note_name: REAL_E2E.adminNote,
            role: 'admin',
        },
    });
    await page.waitForURL(/\/html\/main\.html/);

    await expect(page.locator('.user-session-bar .user-note')).toHaveText(REAL_E2E.adminNote);
    await expect(page.locator('.user-session-bar .user-role')).toHaveText('管理员');
    expect((await context.cookies()).some(cookie => cookie.name === 'kit_session')).toBe(true);

    const logoutRoutePromise = captureJsonRoute(page, '/auth/logout');
    await page.getByRole('button', { name: '退出登录' }).click();
    const { response: logoutResponse, payload: logoutPayload } = await logoutRoutePromise;
    expect(logoutResponse.status()).toBe(200);
    expect(logoutPayload).toMatchObject({ code: 0 });
    await page.waitForURL(/\/html\/login\.html/);
    expect((await context.cookies()).some(cookie => cookie.name === 'kit_session')).toBe(false);
});

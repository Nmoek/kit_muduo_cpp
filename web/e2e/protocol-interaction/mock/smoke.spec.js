import { test, expect } from '@playwright/test';

/**
 * 测试思路：
 *
 * 测什么：
 * ��证桌面 Chromium、Playwright 静态服务器和协议项交互详情模块入口
 * 能够共同工作，并且前端入口文档可以返回给真实浏览器。
 *
 * 为什么这么测：
 * 这是所有浏览器测试的运行时前置条件。先验证 HTTP 入口和 body 可见，
 * 可以把浏览器环境、静态资源服务问题与业务交互测试失败区分开。
 * 这个 smoke 不验证真实后端，也不把登录或 WebSocket 行为混入基础检查。
 *
 * 怎么测：
 * 1. Playwright 自动启动 webServer。
 * 2. 使用桌面 Chromium 访问 Mock 页面入口。
 * 3. 断言主文档返回 200。
 * 4. 断言页面主体可见。
 *
 * 示例：
 * Chromium 启动 -> GET /html/main.html -> 200 -> 页面 body 可见。
 */
test('桌面 Chromium 能加载协议项交互详情模块入口', async ({ page }) => {
    const response = await page.goto('/html/main.html?apiMode=mock');

    expect(response).not.toBeNull();
    expect(response.status()).toBe(200);
    await expect(page.locator('body')).toBeVisible();
});

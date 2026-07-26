import { test, expect } from '@playwright/test';
import { installMockStatePersistence } from '../../helpers/auth.js';

async function loginAsAdmin(page, context, returnTo) {
    await installMockStatePersistence(context);
    await context.clearCookies();
    await page.goto(`/html/login.html?apiMode=mock&returnTo=${encodeURIComponent(returnTo)}`);
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill('admin');
    await page.getByLabel('密码').fill('admin123');
    await Promise.all([
        page.waitForURL(url => new URL(url).pathname === '/html/main.html'),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
    await expect(page.locator('.service-cards')).toBeVisible();
}

/**
 * 测试思路：
 *
 * 测什么：验证服务卡片只有“查看协议项”按钮会跳转到 protocol_items.html，目标 projectId
 * 正确，并保留 apiMode、apiBaseUrl、enableDebugLog 等调试参数；标题编辑和删除按钮不误导航。
 * 为什么这么测：服务页和协议项页是独立入口，参数丢失会从 Mock 切回 Real，卡片事件冒泡
 * 还可能让编辑/删除操作被错误导航中断，必须在真实浏览器 URL 中验收。
 * 怎么测：以包含三个调试参数的 main.html 登录，点击标题进入编辑并取消，点击删除并取消，
 * 最后点击服务 1 的查看协议项，断言目标页、projectId 和全部调试参数。
 * 示例：main.html?apiMode=mock&apiBaseUrl=/proxy&enableDebugLog=1 -> protocol_items.html?...
 */
test('查看协议项保留调试参数且卡片其他操作不误导航', async ({ page, context }) => {
    const mainPath = '/html/main.html?apiMode=mock&apiBaseUrl=%2Fproxy&enableDebugLog=1';
    await loginAsAdmin(page, context, mainPath);

    const card = page.locator('#service-card-1');
    await expect(card).toBeVisible();
    const mainUrlBeforeActions = page.url();

    await card.locator('.service-title').click();
    await expect(card.locator('.inline-title-input')).toBeVisible();
    await card.locator('.inline-title-cancel').click();
    expect(page.url()).toBe(mainUrlBeforeActions);

    page.once('dialog', dialog => dialog.dismiss());
    await card.locator('.delete-service-btn').click();
    expect(page.url()).toBe(mainUrlBeforeActions);

    await Promise.all([
        page.waitForURL(/\/html\/protocol_items\.html\?/),
        card.locator('.view-protocols-btn').click(),
    ]);

    const targetUrl = new URL(page.url());
    expect(targetUrl.pathname).toBe('/html/protocol_items.html');
    expect(targetUrl.searchParams.get('projectId')).toBe('1');
    expect(targetUrl.searchParams.get('apiMode')).toBe('mock');
    expect(targetUrl.searchParams.get('apiBaseUrl')).toBe('/proxy');
    expect(targetUrl.searchParams.get('enableDebugLog')).toBe('1');
});

/**
 * 测试思路：
 * 测什么：验证主入口引用的脚本和样式资源均可成功读取，并确认旧 protocol_test.html 入口没有残留引用。
 * 为什么这么测：资源路径错误会让主页面部分渲染但功能不可用，旧入口残留会把用户导向已删除页面；这些问题需要在真实页面和静态服务器上检查。
 * 怎么测：登录 Mock 管理员，收集 main.html 的 script/link 资源，用 Playwright request 逐个检查 HTTP 200，同时检查页面 HTML 和链接中没有旧入口，并确认新 protocol_items.html 可读取。
 * 示例：main.html -> js/main.js/css/main.css 全部 200；不存在 protocol_test.html 链接。
 */
test('主入口资源完整且不再引用旧协议测试入口', async ({ page, context, request }) => {
    await loginAsAdmin(page, context, '/html/main.html?apiMode=mock');

    const resourcePaths = await page.locator('script[src], link[rel="stylesheet"]').evaluateAll(nodes => (
        nodes.map(node => node.getAttribute('src') || node.getAttribute('href')).filter(Boolean)
    ));
    expect(resourcePaths.length).toBeGreaterThan(5);
    expect(await page.content()).not.toContain('protocol_test.html');
    await expect(page.locator('a[href*="protocol_test.html"]')).toHaveCount(0);

    for (const resourcePath of resourcePaths) {
        const resourceUrl = new URL(resourcePath, page.url());
        const response = await request.get(resourceUrl.toString());
        expect(response.status(), resourcePath).toBe(200);

        if (!resourceUrl.pathname.endsWith('.css')) continue;
        const css = await response.text();
        const assetPaths = [...css.matchAll(/url\(["']?([^"')]+)["']?\)/gi)]
            .map(match => match[1].trim())
            .filter(assetPath => !assetPath.startsWith('data:'));
        for (const assetPath of assetPaths) {
            const assetUrl = new URL(assetPath, resourceUrl);
            const assetResponse = await request.get(assetUrl.toString());
            expect(assetResponse.status(), `${assetUrl.pathname} referenced by ${resourcePath}`).toBe(200);
        }
    }

    const protocolItemsResponse = await request.get(new URL(
        '/html/protocol_items.html?apiMode=mock&projectId=1',
        page.url(),
    ).toString());
    expect(protocolItemsResponse.status()).toBe(200);

    const oldEntryResponse = await request.get(new URL(
        '/html/protocol_test.html?apiMode=mock',
        page.url(),
    ).toString());
    expect(oldEntryResponse.status()).toBe(404);
});

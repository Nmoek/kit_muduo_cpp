import { test, expect } from '@playwright/test';
import { installMockStatePersistence } from '../../helpers/auth.js';

const RECONFIG_URL = '/html/protocol_item_form.html?apiMode=mock&projectId=2&protocolId=5&mode=reconfig&enableDebugLog=1';

async function loginAsAdmin(page, context) {
    await installMockStatePersistence(context);
    await context.clearCookies();
    await page.goto(`/html/login.html?apiMode=mock&returnTo=${encodeURIComponent(RECONFIG_URL)}`);
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill('admin');
    await page.getByLabel('密码').fill('admin123');
    await Promise.all([
        page.waitForURL(/protocol_item_form\.html\?/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
}

/**
 * 测试思路：
 * 测什么：验证 mode=reconfig 会进入重配置语义，回填待重配置协议项，且取消不会提交并返回当前项目
 * 的协议项列表。
 * 为什么这么测：待重配置状态不能被误当成普通编辑或直接上线；取消是用户放弃修改时的安全出口，
 * 需要确认它不会调用重配置接口，也不会丢失 projectId。
 * 怎么测：打开 projectId=2、protocolId=5、mode=reconfig，检查标题/名称和保存按钮状态，点击取消，
 * 等待带 projectId=2 的列表页并确认原协议项仍在列表。
 * 示例：mode=reconfig -> 重配置协议项 -> 取消 -> protocol_items.html?apiMode=mock&projectId=2。
 */
test('F7 重配置模式取消返回列表且不改变原项目', async ({ page, context }) => {
    await loginAsAdmin(page, context);

    await expect(page.locator('#protocol-form-title')).toHaveText('重配置协议项');
    await expect(page.locator('#protocol-form-subtitle')).toContainText('重新提交当前协议项');
    await expect(page.getByLabel('协议项名称')).toHaveValue('TCP待重配置示例');
    await expect(page.locator('#save-protocol-menu-toggle')).toBeDisabled();
    await expect(page.locator('#save-and-online-protocol')).toBeDisabled();

    await Promise.all([
        page.waitForURL(/protocol_items\.html\?apiMode=mock&projectId=2&enableDebugLog=1/),
        page.locator('#cancel-protocol-form').click(),
    ]);
    await expect(page.locator('.protocol-item').filter({ hasText: 'TCP待重配置示例' })).toHaveCount(1);
});

/**
 * 测试思路：
 * 测什么：验证表单页“返回协议项列表”链接在带 apiMode、projectId 和调试参数时能够保留这些参数，
 * 同时移除 protocolId 和 mode 这两个表单专属参数。
 * 为什么这么测：Mock/Browser 调试依赖 apiMode，项目上下文依赖 projectId；参数丢失会导致返回后切换到
 * 真实 API 或回到无项目的空页面。
 * 怎么测：进入重配置 URL，读取 back link 的 href，断言目标路径和参数精确符合 buildProtocolListUrl，
 * 再点击链接确认页面位置。
 * 示例：protocolId=5&mode=reconfig -> protocol_items.html?apiMode=mock&projectId=2&enableDebugLog=1。
 */
test('F7 返回链接保留调试参数并清理表单参数', async ({ page, context }) => {
    await loginAsAdmin(page, context);

    const backLink = page.locator('#back-protocol-list');
    const href = await backLink.getAttribute('href');
    expect(href).toBe('protocol_items.html?apiMode=mock&projectId=2&enableDebugLog=1');

    await Promise.all([
        page.waitForURL(/protocol_items\.html\?apiMode=mock&projectId=2&enableDebugLog=1/),
        backLink.click(),
    ]);
});

import { test, expect } from '@playwright/test';
import { createHttpsServiceAndProtocol, loginHttpsAdmin, preserveHttpsMockState } from '../https_flow.js';

/**
 * 测试思路：使用 Chromium 验证 HTTPS 创建、筛选、启停、协议新增及详情，Mock 不替代真实 TLS 验收。
 * 示例：选择 HTTPS -> 启动 -> GET /https/health 协议项 -> type=https -> 停止服务。
 */
test('HTTPS 页面创建筛选和 HTTP 协议字段复用', async ({ page }) => {
    await preserveHttpsMockState(page);
    await loginHttpsAdmin(page, true);
    const { projectId } = await createHttpsServiceAndProtocol(page, { mock: true });
    await page.getByRole('button', { name: '展开协议项详情', exact: true }).click();
    await expect(page.locator('.protocol-item .path .value')).toBeVisible();
    await expect(page.locator('.protocol-item .path .value')).toHaveText('/https/health');
    await page.goto('/html/main.html?apiMode=mock');
    const card = page.locator(`#service-card-${projectId}`);
    await card.getByRole('button', { name: '停止测试服务', exact: true }).click();
    await expect(card.getByRole('button', { name: '启动测试服务', exact: true })).toBeVisible();
});

/**
 * 测试思路：HTTPS 证书加载错误需要保留后端原因，启动按钮可重试，服务仍是停止态。
 * 示例：接口返回 certificate chain load failed -> 页面错误提示 -> 启动按钮仍可用。
 */
test('HTTPS 启动失败显示证书原因并保留停止态', async ({ page }) => {
    await loginHttpsAdmin(page, true);
    await expect(page.locator('.service-card').first()).toBeVisible();
    await page.evaluate(async () => {
        const project = window.KitProxy.mocks.state.projects.find(item => item.id === 1);
        project.protocol_type = 3;
        project.runtime_state = 0;
        await window.loadAllProjects();
        window.KitProxy.api.setProjectRuntimeState = async () => {
            throw new Error('HTTPS certificate chain load failed');
        };
    });
    const card = page.locator('#service-card-1');
    await card.getByRole('button', { name: '启动测试服务', exact: true }).click();
    await expect(page.getByText('HTTPS certificate chain load failed', { exact: false })).toBeVisible();
    await expect(card.getByRole('button', { name: '启动测试服务', exact: true })).toBeEnabled();
    await expect(card.locator('.project-status')).toContainText('未开启');
});

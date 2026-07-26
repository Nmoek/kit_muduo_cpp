import { test, expect } from '@playwright/test';
import { REAL_E2E } from '../../protocol-interaction/real/test_config.js';

function formPath() {
    return `/html/protocol_item_form.html?projectId=${encodeURIComponent(REAL_E2E.projectId)}&protocolId=${encodeURIComponent(REAL_E2E.protocolId)}`;
}

async function loginRealAdmin(page, context) {
    await context.clearCookies();
    await page.goto('/html/login.html');
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill(REAL_E2E.adminNote);
    await page.getByLabel('密码').fill(REAL_E2E.adminPassword);
    await Promise.all([
        page.waitForURL(/\/html\/main\.html/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
}

/**
 * 测试思路：
 * 测什么：验证真实 C++ 后端登录后，配置的 projectId/protocolId 能打开协议项编辑页并读取完整详情，
 * 包括标题、协议项名称、测试服务上下文、类型专属字段和 Body 编辑器。
 * 为什么这么测：Real 阶段只需要确认真实协议项编辑页的读取链路，不应批量修改真实配置；该用例覆盖
 * getProject、getProtocolEditDetail、Body 类型/数据读取和表单回填的组合。
 * 怎么测：使用现有 REAL_E2E 管理员和 ID 配置登录，直接访问 protocol_item_form.html，断言 HTTP/TCP
 * 类型专属区域至少渲染出一个可配置字段，并确认 URL 仍包含配置的 projectId/protocolId。
 * 示例：真实登录 -> projectId=1&protocolId=1 -> 修改协议项 -> 名称和 Body 控件可见，不提交保存。
 */
test('F8 真实后端协议项编辑页读取冒烟', async ({ page, context }) => {
    await loginRealAdmin(page, context);
    await page.goto(formPath());

    await expect(page).toHaveURL(new RegExp(`protocol_item_form\\.html\\?projectId=${REAL_E2E.projectId}&protocolId=${REAL_E2E.protocolId}`));
    await expect(page.locator('#protocol-form-title')).toHaveText('修改协议项');
    await expect(page.locator('#protocol-form-project-context')).toBeVisible();
    await expect(page.locator('.service-context-title')).not.toHaveText('');
    await expect(page.getByLabel('协议项名称')).not.toHaveValue('');
    await expect(page.locator('#protocol-type-fields')).not.toBeEmpty();
    await expect(page.locator('[data-body-editor="protocol-form-body"]')).toBeVisible();
    await expect(page.locator('.body-switch-btn')).toHaveCount(2);

    const protocolType = await page.locator('#protocol-type-fields').innerText();
    expect(protocolType).toMatch(/请求|响应|功能码|路径|请求方法|响应码/);
});


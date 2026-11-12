import { expect } from '@playwright/test';

/** Mock 数据保存在内存；本用例跨页面创建数据时由测试夹具保存，生产 Mock 不引入持久化。 */
export async function preserveHttpsMockState(page) {
    await page.route('**/js/mock_data.js', async route => {
        const response = await route.fetch();
        const fixture = `
            (() => {
                const state = window.KitProxy.mocks.state;
                const saved = sessionStorage.getItem('https-e2e-state');
                if (saved) Object.assign(state, JSON.parse(saved));
                window.addEventListener('pagehide', () => {
                    sessionStorage.setItem('https-e2e-state', JSON.stringify(state));
                });
            })();`;
        await route.fulfill({ response, body: (await response.text()) + fixture });
    });
}

/** 从页面创建 HTTPS 服务与协议项；各页面监听端口只显示数字，例如 8443。Mock 和 Real 共用操作。 */
export async function createHttpsServiceAndProtocol(page, { mock = false } = {}) {
    const suffix = mock ? '?apiMode=mock' : '';
    await page.goto(`/html/main.html${suffix}`);
    // 页面初始化会等待真实登录态接口；DOM 按钮可见时事件监听可能尚未安装。
    await expect(page.locator('.service-card').first()).toBeVisible();
    await page.locator('#add-service').click();
    const modal = page.locator('.add-service-modal');
    const name = `HTTPS验收-${Date.now()}`;
    await modal.getByLabel('服务名称').fill(name);
    await modal.getByLabel('协议类型').selectOption('3');
    await modal.getByLabel('测试模式').selectOption('1');
    await modal.getByRole('button', { name: '确认添加' }).click();
    await expect(modal).toBeHidden();
    await page.locator('#filter-protocol-type').selectOption('3');
    await page.locator('#apply-service-filter').click();
    const card = page.locator('.service-card').filter({ has: page.getByText(name, { exact: true }) });
    await expect(card).toBeVisible();
    const projectId = Number((await card.getAttribute('id')).split('-').at(-1));
    await card.getByRole('button', { name: '启动测试服务', exact: true }).click();
    await expect(card.getByRole('button', { name: '停止测试服务', exact: true })).toBeVisible();
    const project = await page.evaluate(async id => (await window.KitProxy.api.getProject(id))[0], projectId);
    await expect(card.locator('.project-listen-port .field-value')).toHaveText(String(project.listen_port));
    await card.getByRole('button', { name: '查看协议项' }).click();
    await expect(page.locator('#protocol-service-meta .project-listen-port .field-value'))
        .toHaveText(String(project.listen_port));
    await page.locator('#add-protocol-item').click();
    await expect(page.locator('#request-path')).toBeVisible();
    await page.getByLabel('协议项名称').fill('HTTPS健康检查');
    await page.locator('#request-path').fill('/https/health');
    const addedResponse = mock ? null : page.waitForResponse(response => (
        response.request().method() === 'POST' && new URL(response.url()).pathname === '/protocols/add'
    ));
    await page.getByRole('button', { name: '保存', exact: true }).click();
    await expect(page.locator('.protocol-item')).toHaveCount(1);
    const item = page.locator('.protocol-item').first();
    const protocolId = Number(await item.getAttribute('data-protocol-id'));
    if (addedResponse) expect((await (await addedResponse).json()).data.protocol_id).toBe(protocolId);
    const protocol = await page.evaluate(async id => (await window.KitProxy.api.getProtocol(id))[0], protocolId);
    expect(String(protocol.type).toLowerCase()).toBe('https');
    await page.evaluate(async id => window.KitProxy.api.setProtocolRuntime(id, true), protocolId);
    await page.reload();
    await expect(page.getByTestId('protocol-interaction-open')).toBeEnabled();
    return { projectId, protocolId, port: project.listen_port };
}

/** 页面登录后跳转主页面；使用现有测试管理员，不写生产数据。 */
export async function loginHttpsAdmin(page, mock = false) {
    await page.goto(`/html/login.html${mock ? '?apiMode=mock' : ''}`);
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill(process.env.PLAYWRIGHT_REAL_ADMIN_NOTE || 'admin');
    await page.getByLabel('密码').fill(process.env.PLAYWRIGHT_REAL_ADMIN_PASSWORD || 'admin123');
    await Promise.all([
        page.waitForURL(/\/html\/main\.html/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
}

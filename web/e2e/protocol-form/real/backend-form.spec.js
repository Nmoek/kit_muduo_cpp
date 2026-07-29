import { test, expect } from '@playwright/test';
import { REAL_E2E } from '../../protocol-interaction/real/test_config.js';

function formPath() {
    return `/html/protocol_item_form.html?projectId=${encodeURIComponent(REAL_E2E.projectId)}&protocolId=${encodeURIComponent(REAL_E2E.protocolId)}`;
}

function addFormPath(projectId) {
    return `/html/protocol_item_form.html?projectId=${encodeURIComponent(projectId)}`;
}

function reconfigFormPath(projectId, protocolId) {
    return `/html/protocol_item_form.html?projectId=${encodeURIComponent(projectId)}&protocolId=${encodeURIComponent(protocolId)}&mode=reconfig`;
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

async function fieldRowBySelectValue(modal, selector, value) {
    const rows = modal.locator('.pattern-field-container');
    const count = await rows.count();
    for (let index = 0; index < count; index += 1) {
        if (await rows.nth(index).locator(selector).inputValue() === value) return rows.nth(index);
    }
    throw new Error(`未找到 ${selector}=${value} 的 TCP 字段行`);
}

async function fieldRowByBytePosition(modal, position) {
    const rows = modal.locator('.pattern-field-container');
    const count = await rows.count();
    for (let index = 0; index < count; index += 1) {
        if (await rows.nth(index).locator('.pattern-field-byte-pos').inputValue() === String(position)) {
            return rows.nth(index);
        }
    }
    throw new Error(`未找到 byte_pos=${position} 的 TCP 字段行`);
}

async function openHeaderModal(page, buttonId) {
    await page.locator(buttonId).click();
    const modal = page.locator('.modal-overlay .config-pattern-modal.is-item-pattern');
    await expect(modal).toBeVisible();
    return modal;
}

async function setFunctionCode(modal, value) {
    const row = await fieldRowBySelectValue(modal, '.pattern-field-role', 'function_code');
    const hexText = value.replace(/^H/i, '').toUpperCase();
    await row.locator('.pattern-value-editor-input').fill(hexText.replace(/(..)(?=.)/g, '$1 '));
    await expect(row.locator('.pattern-field-value')).toHaveValue(`H${hexText}`);
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

/**
 * 测试思路：
 * 测什么：验证 HTTP 新增生成的 protocol_cfg_header 能被真实 C++ AddProtocolReqHeader 解码，
 * 并且请求配置能够完整持久化和回读。
 * 为什么这么测：前端曾发送 type=HTTP/TCP，而后端契约只接受 http/custom_tcp/https；仅验证 FormData
 * 字符串无法发现前后端枚举再次漂移，必须让真实 multipart 经过 bindMultipart。
 * 怎么测：在隔离数据库中打开 HTTP 项目新增页，提交后捕获 POST /protocols/add，并通过详情聚合接口
 * 回读名称、项目 ID、协议类型、请求路径和 Body 类型。
 * 示例：type=http、project_id=2 -> Add(state=0) -> protocol_id > 0 -> GET detail 字段一致。
 */
test('F9 HTTP 新增协议头通过真实后端并完整回读', async ({ page, context }) => {
    test.setTimeout(60_000);
    await loginRealAdmin(page, context);
    await page.goto(addFormPath(REAL_E2E.attachmentProjectId));

    await expect(page.locator('#protocol-form-title')).toHaveText('添加协议项');
    await expect(page.locator('#request-path')).toBeVisible();
    await page.getByLabel('协议项名称').fill('Real E2E contract protocol');
    await page.locator('#request-path').fill('/api/real-contract-protocol');

    const addResponsePromise = page.waitForResponse(response => (
        response.url().includes('/protocols/add')
        && response.request().method() === 'POST'
    ));
    await page.locator('#save-protocol-form').click();

    const addResponse = await addResponsePromise;
    expect(addResponse.ok()).toBe(true);
    const responseBody = await addResponse.json();
    expect(responseBody.code).toBe(0);
    const protocolId = Number(responseBody.data?.protocol_id);
    expect(protocolId).toBeGreaterThan(0);
    expect(Number(responseBody.data?.config_state)).toBe(0);

    await page.waitForURL(/protocol_items\.html\?/);
    const detail = await page.evaluate(async id => window.KitProxy.api.getProtocolEditDetail(id), protocolId);
    expect(detail.name).toBe('Real E2E contract protocol');
    expect(Number(detail.project_id)).toBe(REAL_E2E.attachmentProjectId);
    expect(String(detail.type).toLowerCase()).toBe('http');
    expect(detail.req_cfg).toMatchObject({
        method: 'GET',
        path: '/api/real-contract-protocol',
    });
    expect(detail.req_body_type).toBe('json');
    expect(detail.resp_body_type).toBe('json');
});

/**
 * 测试思路：
 * 测什么：验证 CUSTOM_TCP 新增页从真实项目读取 Pattern，用户配置的功能码和普通字段能够连同
 * type=custom_tcp 的 protocol_cfg_header 一起被后端解码；Pattern 变更后还能完成完整重配置。
 * 为什么这么测：HTTP 和 CUSTOM_TCP 使用不同的协议类型枚举与 req/resp cfg 结构；只覆盖 HTTP
 * 无法阻止前端再次把 custom_tcp 错写成旧值 TCP，也无法验证 Pattern 编辑结果是否进入 multipart。
 * 怎么测：先新增并回读 TCP 协议；再停止项目并更新原 Pattern，使协议进入 state=2，随后打开重配置页
 * 把请求功能码改为 H1001，提交 /reconfig 并确认状态回到 0、配置按新值落库。
 * 示例：Add(H1000) -> Pattern update(state=2) -> Reconfig(H1001) -> state=0。
 */
test('F10 CUSTOM_TCP Pattern 配置通过真实新增和回读链路', async ({ page, context }) => {
    test.setTimeout(60_000);
    await loginRealAdmin(page, context);
    await page.goto(addFormPath(REAL_E2E.projectId));

    await expect(page.locator('#protocol-form-title')).toHaveText('添加协议项');
    await expect(page.locator('#req-pattern-infos')).toBeVisible();
    await expect(page.locator('#resp-pattern-infos')).toBeVisible();

    const requestModal = await openHeaderModal(page, '#req-pattern-infos');
    await setFunctionCode(requestModal, 'H1000');
    const totalLengthRow = await fieldRowByBytePosition(requestModal, 4);
    await totalLengthRow.locator('.pattern-value-editor-input').fill('00 00 02 09');
    await expect(totalLengthRow.locator('.pattern-field-value')).toHaveValue('H00000209');
    await requestModal.locator('.confirm-btn').click();
    await expect(requestModal).toBeHidden();

    const responseModal = await openHeaderModal(page, '#resp-pattern-infos');
    await setFunctionCode(responseModal, 'H1080');
    await responseModal.locator('.confirm-btn').click();
    await expect(responseModal).toBeHidden();

    await page.getByLabel('协议项名称').fill('Real E2E custom TCP protocol');
    const addResponsePromise = page.waitForResponse(response => (
        response.url().includes('/protocols/add')
        && response.request().method() === 'POST'
    ));
    await page.locator('#save-protocol-form').click();

    const addResponse = await addResponsePromise;
    expect(addResponse.ok()).toBe(true);
    const responseBody = await addResponse.json();
    expect(responseBody.code).toBe(0);
    const protocolId = Number(responseBody.data?.protocol_id);
    expect(protocolId).toBeGreaterThan(0);

    await page.waitForURL(/protocol_items\.html\?/);
    const detail = await page.evaluate(async id => window.KitProxy.api.getProtocolEditDetail(id), protocolId);
    expect(detail.name).toBe('Real E2E custom TCP protocol');
    expect(Number(detail.project_id)).toBe(REAL_E2E.projectId);
    // AddProtocolReqHeader 接收 custom_tcp，ProtocolVO 对外展示值为 TCP。
    expect(String(detail.type).toLowerCase()).toBe('tcp');
    expect(detail.req_cfg).toMatchObject({
        function_code: 'H1000',
        fields: { 4: 'H00000209' },
    });
    expect(detail.resp_cfg).toMatchObject({
        function_code: 'H1080',
    });

    const pendingReconfig = await page.evaluate(async ({ id, projectId }) => {
        await window.KitProxy.api.setProjectRuntimeState(projectId, false);
        const patternInfo = await window.KitProxy.api.getProjectPatternInfo(projectId);
        await window.KitProxy.api.updateProjectPatternInfo(projectId, patternInfo);
        const protocols = await window.KitProxy.api.getProtocol(id);
        return protocols[0];
    }, {
        id: protocolId,
        projectId: REAL_E2E.projectId,
    });
    expect(Number(pendingReconfig.config_state)).toBe(2);

    await page.goto(reconfigFormPath(REAL_E2E.projectId, protocolId));
    await expect(page.locator('#protocol-form-title')).toHaveText('重配置协议项');
    await expect(page.getByLabel('协议项名称')).toHaveValue('Real E2E custom TCP protocol');
    await page.getByLabel('协议项名称').fill('Real E2E reconfigured TCP protocol');
    const reconfigRequestModal = await openHeaderModal(page, '#req-pattern-infos');
    await setFunctionCode(reconfigRequestModal, 'H1001');
    await reconfigRequestModal.locator('.confirm-btn').click();
    await expect(reconfigRequestModal).toBeHidden();
    await expect(page.getByLabel('协议项名称')).toHaveValue('Real E2E reconfigured TCP protocol');
    const reconfigPayload = await page.evaluate(() => {
        const data = window.KitProxy.protocolItemForm.collectFormData();
        return window.KitProxy.protocolItemForm.buildAddPayload(data);
    });
    expect(reconfigPayload.cfg_header).toMatchObject({
        name: 'Real E2E reconfigured TCP protocol',
        type: 'custom_tcp',
        project_id: REAL_E2E.projectId,
    });
    expect(reconfigPayload.req_cfg.function_code).toBe('H1001');

    const reconfigResponsePromise = page.waitForResponse(response => (
        response.url().includes(`/protocols/${protocolId}/reconfig`)
        && response.request().method() === 'POST'
    ));
    await page.locator('#save-protocol-form').click();

    const reconfigResponse = await reconfigResponsePromise;
    expect(reconfigResponse.ok()).toBe(true);
    const reconfigBody = await reconfigResponse.json();
    expect(reconfigBody.code).toBe(0);
    expect(Number(reconfigBody.data?.protocol_id)).toBe(protocolId);
    expect(Number(reconfigBody.data?.config_state)).toBe(0);

    await page.waitForURL(/protocol_items\.html\?/);
    const reconfigured = await page.evaluate(async id => window.KitProxy.api.getProtocolEditDetail(id), protocolId);
    expect(reconfigured.name).toBe('Real E2E reconfigured TCP protocol');
    expect(Number(reconfigured.config_state)).toBe(0);
    expect(reconfigured.req_cfg.function_code).toBe('H1001');
    expect(reconfigured.req_cfg.fields).toMatchObject({ 4: 'H00000209' });
});

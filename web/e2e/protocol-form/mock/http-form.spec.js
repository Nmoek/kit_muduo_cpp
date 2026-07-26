import { test, expect } from '@playwright/test';
import { installMockStatePersistence } from '../../helpers/auth.js';

const FORM_PATH = '/html/protocol_item_form.html';

function formUrl(projectId = 1, extra = '') {
    return `${FORM_PATH}?apiMode=mock&projectId=${projectId}${extra}`;
}

async function loginAsAdmin(page, context, returnTo) {
    await installMockStatePersistence(context);
    await context.clearCookies();
    await page.goto(`/html/login.html?apiMode=mock&returnTo=${encodeURIComponent(returnTo)}`);
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill('admin');
    await page.getByLabel('密码').fill('admin123');
    await Promise.all([
        page.waitForURL(/protocol_item_form\.html\?/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
}

async function expectFormError(page, message) {
    await expect(page.locator('.global-error-popup.is-visible[role="alert"]').last()).toContainText(message);
}

async function fillRequiredHttpFields(page, name, path = '/api/e2e-form') {
    await page.getByLabel('协议项名称').fill(name);
    await page.locator('#request-path').fill(path);
    await page.locator('#response-status-code').fill('200');
}

async function saveAndWaitForList(page) {
    await Promise.all([
        page.waitForURL(/protocol_items\.html\?/),
        page.locator('#save-protocol-form').click(),
    ]);
}

/**
 * 测试思路：
 * 测什么：验证新增 HTTP 协议项表单能够根据 projectId 加载，并显示 HTTP 默认字段。
 * 为什么这么测：表单初始化依赖认证、项目上下文、协议类型注册表和 BodyEditor；任一环节
 * 失败都会让用户无法开始配置协议项，必须用真实 Chromium 走完整入口。
 * 怎么测：登录 Mock 管理员后打开 projectId=1 的新增页，检查标题、服务上下文、默认名称、
 * GET、/api/、200，以及请求 Body 隐藏而响应 Body 可切换的状态。
 * 示例：projectId=1 -> 添加协议项 -> GET + /api/ + 200。
 */
test('F1 新增 HTTP 表单加载默认字段', async ({ page, context }) => {
    const target = formUrl(1);
    await loginAsAdmin(page, context, target);

    await expect(page).toHaveURL(/protocol_item_form\.html\?apiMode=mock&projectId=1/);
    await expect(page.locator('#protocol-form-title')).toHaveText('添加协议项');
    await expect(page.locator('#back-protocol-list')).toBeVisible();
    await expect(page.locator('.service-context-title')).toHaveText('HTTP测试服务示例');
    await expect(page.locator('.service-context-status')).toHaveText('开启');
    await expect(page.getByLabel('协议项名称')).toHaveValue('');
    await expect(page.locator('input[name="request-method"]:checked')).toHaveValue('GET');
    await expect(page.locator('#request-path')).toHaveValue('/api/');
    await expect(page.locator('#response-status-code')).toHaveValue('200');

    const bodySection = page.locator('.protocol-body-section');
    const requestEditor = page.locator('[data-body-editor="protocol-form-body"]');
    await expect(page.getByRole('button', { name: '校验请求Body', exact: true })).toHaveClass(/is-active/);
    await expect(bodySection).toHaveClass(/is-request-body-content-hidden/);
    await expect(requestEditor.locator('.body-editor-input-wrap')).toBeHidden();

    await page.getByRole('button', { name: '目标响应Body', exact: true }).click();
    await expect(bodySection).not.toHaveClass(/is-request-body-content-hidden/);
    await expect(requestEditor.locator('.body-editor-input-wrap')).toBeVisible();
    await expect(requestEditor.locator('.body-editor-type')).toHaveValue('json');
});

/**
 * 测试思路：
 * 测什么：验证 HTTP 协议项名称、请求路径和响应码的提交校验，以及错误时不离开表单页。
 * 为什么这么测：这些字段直接决定匹配路由和响应行为，非法值若被提交会产生不可用协议项；
 * 表单使用自定义校验，浏览器级断言可以确认中文错误真的展示给用户。
 * 怎么测：先空提交检查名称错误，再依次填入不以 / 开头的路径、99 和 600，分别检查
 * role=alert 文案，并确认 URL 始终是 protocol_item_form.html。
 * 示例：path=api/test -> 请求路径必须以 / 开头；status=600 -> 响应码必须是 100 到 599 的整数。
 */
test('F2 HTTP 名称路径和响应码校验阻止提交', async ({ page, context }) => {
    const target = formUrl(1);
    await loginAsAdmin(page, context, target);

    await page.locator('#save-protocol-form').click();
    await expectFormError(page, '协议项名称不能为空');
    await expect(page).toHaveURL(/protocol_item_form\.html\?/);

    await page.getByLabel('协议项名称').fill('HTTP校验失败路径');
    await page.locator('#request-path').fill('api/test');
    await page.locator('#response-status-code').fill('200');
    await page.locator('#save-protocol-form').click();
    await expectFormError(page, '请求路径必须以 / 开头');
    await expect(page).toHaveURL(/protocol_item_form\.html\?/);

    await page.locator('#request-path').fill('/api/invalid-status');
    await page.locator('#response-status-code').fill('99');
    await page.locator('#save-protocol-form').click();
    await expectFormError(page, '响应码必须是 100 到 599 的整数');
    await expect(page).toHaveURL(/protocol_item_form\.html\?/);

    await page.locator('#response-status-code').fill('600');
    await page.locator('#save-protocol-form').click();
    await expectFormError(page, '响应码必须是 100 到 599 的整数');
    await expect(page).toHaveURL(/protocol_item_form\.html\?/);
});

/**
 * 测试思路：
 * 测什么：验证响应 Body Tab 的 JSON 编辑、格式化、保存和重新编辑回填，并覆盖请求侧
 * Body 内容区按产品规则隐藏的当前行为。
 * 为什么这么测：BodyEditor 不只是 textarea，还负责类型选择、格式化、校验和提交内容；
 * 保存后重新读取才能证明 payload 中的 Body 类型和内容确实进入了 Mock 状态。
 * 怎么测：新增 HTTP 协议项，切换响应 Body 输入紧凑 JSON，点击格式化并保存；从列表取得新
 * 协议 ID 后重新打开编辑页，断言响应 JSON 已回填且请求 Tab 内容区仍隐藏。
 * 示例：{"ok":true} -> 格式化为多行 JSON -> 保存 -> 编辑页回填 { "ok": true }。
 */
test('F3 HTTP 响应 Body 格式化保存并回填', async ({ page, context }) => {
    const target = formUrl(1);
    const name = 'HTTP响应Body回填示例';
    await loginAsAdmin(page, context, target);

    await fillRequiredHttpFields(page, name);
    await page.getByRole('button', { name: '目标响应Body', exact: true }).click();

    const editor = page.locator('[data-body-editor="protocol-form-body"]');
    const textarea = editor.locator('.body-editor-textarea');
    await editor.locator('.body-editor-type').selectOption('json');
    await textarea.fill('{"ok":true}');
    await editor.locator('.body-editor-format').click();
    await expect(textarea).toHaveValue(/\{\s*"ok": true\s*\}/);
    await expect(editor.locator('.body-editor-status')).toHaveText('格式正确');

    await saveAndWaitForList(page);
    const item = page.locator('.protocol-item').filter({ hasText: name });
    await expect(item).toHaveCount(1);
    const protocolId = await item.getAttribute('data-protocol-id');
    expect(protocolId).toMatch(/^\d+$/);

    await page.goto(formUrl(1, `&protocolId=${protocolId}`));
    await expect(page.locator('#protocol-form-title')).toHaveText('修改协议项');
    await expect(page.getByLabel('协议项名称')).toHaveValue(name);
    await page.getByRole('button', { name: '目标响应Body', exact: true }).click();
    const responseEditor = page.locator('[data-body-editor="protocol-form-body"]');
    await expect(responseEditor.locator('.body-editor-type')).toHaveValue('json');
    await expect(responseEditor.locator('.body-editor-textarea')).toHaveValue(/\{\s*"ok": true\s*\}/);

    await page.getByRole('button', { name: '校验请求Body', exact: true }).click();
    await expect(page.locator('.protocol-body-section')).toHaveClass(/is-request-body-content-hidden/);
});

/**
 * 测试思路：
 * 测什么：验证新增 HTTP 协议项的普通保存会得到未上线状态，且提交期间按钮进入 busy/disabled
 * 状态，随后验证保存并上线菜单在运行项目上可用并生成已上线协议项。
 * 为什么这么测：配置保存和运行态应用是两个不同语义；重复点击还可能创建重复协议项，必须
 * 检查提交锁和 config_state 的最终 UI 映射。
 * 怎么测：在运行中的 projectId=1 新增一个协议，立即检查保存按钮忙碌，再等待列表中的“未上线”；
 * 第二个协议通过分裂按钮选择“保存并上线”，检查列表按钮为“已上线”。
 * 示例：保存 -> config_state=0 -> 未上线；保存并上线 -> config_state=1 -> 已上线。
 */
test('F4 HTTP 保存和保存并上线状态流转', async ({ page, context }) => {
    const target = formUrl(1);
    await loginAsAdmin(page, context, target);

    await fillRequiredHttpFields(page, 'HTTP普通保存示例');
    const saveButton = page.locator('#save-protocol-form');
    await saveButton.click();
    await expect(saveButton).toBeDisabled();
    await expect(saveButton).toHaveAttribute('aria-busy', 'true');
    await page.waitForURL(/protocol_items\.html\?/);
    const offlineItem = page.locator('.protocol-item').filter({ hasText: 'HTTP普通保存示例' });
    await expect(offlineItem.locator('.protocol-runtime-btn')).toHaveText('未上线');

    await page.goto(formUrl(1));
    await fillRequiredHttpFields(page, 'HTTP保存并上线示例', '/api/e2e-online');
    await page.locator('#save-protocol-menu-toggle').click();
    await expect(page.locator('#save-protocol-menu')).toBeVisible();
    await expect(page.locator('#save-and-online-protocol')).toBeEnabled();
    await Promise.all([
        page.waitForURL(/protocol_items\.html\?/),
        page.locator('#save-and-online-protocol').click(),
    ]);

    const onlineItem = page.locator('.protocol-item').filter({ hasText: 'HTTP保存并上线示例' });
    await expect(onlineItem.locator('.protocol-runtime-btn')).toHaveText('已上线');
    await expect(onlineItem).toHaveAttribute('data-config-state', '1');
});

/**
 * 测试思路：
 * 测什么：验证编辑已有 HTTP 协议项时能回填 method/path/status/Body，保存后回到列表并显示修改值。
 * 为什么这么测：编辑走的是名称、请求配置、响应配置和 Body 的差异 patch，与新增 payload 路径不同，
 * 是最容易出现“页面看似保存但实际没有更新”的场景。
 * 怎么测：打开 Mock 的 protocolId=1 编辑页，断言原始 GET、/api/test1、200 和 Body，修改名称、路径、
 * 响应码及响应 JSON 后保存，展开列表卡片检查关键字段和 Body 已设置。
 * 示例：/api/test1 -> /api/e2e-edit -> 保存 -> 列表显示 /api/e2e-edit 和 207。
 */
test('F5 编辑 HTTP 协议项并返回列表', async ({ page, context }) => {
    const target = formUrl(1, '&protocolId=1');
    await loginAsAdmin(page, context, target);

    await expect(page.locator('#protocol-form-title')).toHaveText('修改协议项');
    await expect(page.getByLabel('协议项名称')).toHaveValue('HTTP健康检查示例');
    await expect(page.locator('input[name="request-method"]:checked')).toHaveValue('GET');
    await expect(page.locator('#request-path')).toHaveValue('/api/test1');
    await expect(page.locator('#response-status-code')).toHaveValue('200');

    await page.getByRole('button', { name: '目标响应Body', exact: true }).click();
    const responseEditor = page.locator('[data-body-editor="protocol-form-body"]');
    await expect(responseEditor.locator('.body-editor-textarea')).toHaveValue(/\{\s*"ok": true\s*\}/);

    await page.getByLabel('协议项名称').fill('HTTP编辑后示例');
    await page.locator('#request-path').fill('/api/e2e-edit');
    await page.locator('#response-status-code').fill('207');
    await responseEditor.locator('.body-editor-textarea').fill('{"edited":true}');
    await responseEditor.locator('.body-editor-format').click();
    await saveAndWaitForList(page);

    const editedItem = page.locator('.protocol-item').filter({ hasText: 'HTTP编辑后示例' });
    await expect(editedItem).toHaveCount(1);
    await editedItem.locator('.protocol-toggle-btn').click();
    await expect(editedItem.locator('.protocol-field.path .value')).toHaveText('/api/e2e-edit');
    await expect(editedItem.locator('.protocol-field.status .value')).toHaveText('207');
    await expect(editedItem.locator('.protocol-field.response-body .value')).toHaveText('已设置');
});

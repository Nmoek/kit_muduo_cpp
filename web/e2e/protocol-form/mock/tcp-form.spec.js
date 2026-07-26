import { test, expect } from '@playwright/test';
import { installMockStatePersistence } from '../../helpers/auth.js';

const FORM_PATH = '/html/protocol_item_form.html';

function formUrl(projectId = 2) {
    return `${FORM_PATH}?apiMode=mock&projectId=${projectId}`;
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
    await row.locator('.pattern-value-editor-input').fill(value.replace(/^H/i, '').replace(/(..)(?=.)/g, '$1 '));
    await expect(row.locator('.pattern-field-value')).toHaveValue(`H${value.replace(/^H/i, '').toUpperCase()}`);
}

/**
 * 测试思路：
 * 测什么：验证新增 TCP 表单能读取项目 TCP Pattern，打开请求/响应头部字段配置，并约束起始标识、
 * 长度字段等不可编辑元数据，同时确认功能码和普通字段值控件存在。
 * 为什么这么测：TCP 协议项不是固定的几个 input，而是由项目格式动态生成；若 Pattern 没有加载，
 * 用户无法配置合法报文，且只读字段被误改会破坏项目级格式。
 * 怎么测：打开 projectId=2，分别打开请求和响应配置 modal，按 role 检查功能码、start_magic、
 * body_length 的字段行及 disabled 状态。
 * 示例：projectId=2 -> TCP格式 -> 配置请求头部字段值 -> 起始标识/长度只读。
 */
test('F6 TCP 表单加载 Pattern 和字段权限', async ({ page, context }) => {
    const target = formUrl(2);
    await loginAsAdmin(page, context, target);

    await expect(page.locator('#protocol-form-title')).toHaveText('添加协议项');
    await expect(page.locator('.service-context-title')).toHaveText('TCP测试服务示例');
    await expect(page.locator('.service-context-status')).toHaveText('未开启');
    await expect(page.locator('#req-pattern-infos')).toBeVisible();
    await expect(page.locator('#resp-pattern-infos')).toBeVisible();

    const modal = await openHeaderModal(page, '#req-pattern-infos');
    await expect(await fieldRowBySelectValue(modal, '.pattern-field-role', 'function_code')).toBeVisible();
    const startMagicRow = await fieldRowBySelectValue(modal, '.pattern-field-role', 'start_magic');
    const bodyLengthRow = await fieldRowBySelectValue(modal, '.pattern-field-role', 'body_length');
    await expect(startMagicRow.locator('.pattern-value-editor-input')).toBeDisabled();
    await expect(bodyLengthRow.locator('.pattern-value-editor-input')).toBeDisabled();
    await expect(startMagicRow.locator('.pattern-field-role')).toBeDisabled();
    await expect(bodyLengthRow.locator('.pattern-field-role')).toBeDisabled();
    await modal.locator('.cancel-btn').click();

    const responseModal = await openHeaderModal(page, '#resp-pattern-infos');
    await expect(responseModal.locator('.pattern-field-container')).toHaveCount(6);
    await responseModal.locator('.cancel-btn').click();
});

/**
 * 测试思路：
 * 测什么：验证未运行项目中的“保存并上线”选项被禁用，并且控件 title 明确说明限制原因。
 * 为什么这么测：保存并上线需要运行中的项目承载运行态配置；禁用入口比提交后才失败更能避免
 * 用户误以为协议已经上线。当前 Mock 没有未运行的 HTTP 示例，所以使用计划允许的未运行 TCP 项目。
 * 怎么测：打开 projectId=2 的新增表单，展开保存选项，检查保存并上线按钮不能用且提示为“项目未运行”。
 * 示例：TCP projectId=2 runtime_state=0 -> 保存并上线 disabled=true。
 */
test('F4 未运行项目限制保存并上线', async ({ page, context }) => {
    const target = formUrl(2);
    await loginAsAdmin(page, context, target);

    await page.locator('#save-protocol-menu-toggle').click();
    const saveAndOnlineButton = page.locator('#save-and-online-protocol');
    await expect(saveAndOnlineButton).toBeVisible();
    await expect(saveAndOnlineButton).toBeDisabled();
    await expect(saveAndOnlineButton).toHaveAttribute('title', '项目未运行，不能保存并上线');
});

/**
 * 测试思路：
 * 测什么：验证 TCP 功能码不能为空、必须按 Pattern 的 Byte 长度填写，并验证普通字段值的 H 字节串
 * 长度校验；修正为合法值后，新增 TCP 协议项能够保存回列表。
 * 为什么这么测：TCP 字段值直接组成二进制报文，少一个字节或缺少功能码都不能形成合法协议项；
 * 这是计划中最关键的用户输入错误路径。
 * 怎么测：在请求 modal 中先提交空功能码，再输入一字节而非四字节的普通字段，断言错误；然后填入
 * H1000/H00000209 和响应 H1080，保存后检查列表中出现新的 TCP 卡片。
 * 示例：function_code=H1000、byte_pos=4 的值=H00000209 -> 保存成功。
 */
test('F6 TCP 功能码和普通字段字节校验后保存', async ({ page, context }) => {
    const target = formUrl(2);
    await loginAsAdmin(page, context, target);

    const requestModal = await openHeaderModal(page, '#req-pattern-infos');
    await requestModal.locator('.confirm-btn').click();
    await expect(requestModal.locator('.pattern-validation-errors')).toContainText('功能码必须配置');

    await setFunctionCode(requestModal, 'H1000');
    const totalLengthRow = await fieldRowByBytePosition(requestModal, 4);
    await totalLengthRow.locator('.pattern-value-editor-input').fill('00');
    await expect(requestModal.locator('.pattern-validation-errors')).toContainText('消息总长度：字段值字节数必须等于 4');

    await totalLengthRow.locator('.pattern-value-editor-input').fill('00 00 02 09');
    await expect(totalLengthRow.locator('.pattern-field-value')).toHaveValue('H00000209');
    await requestModal.locator('.confirm-btn').click();
    await expect(requestModal).toBeHidden();
    await expect(page.locator('#req-pattern-infos').locator('..').locator('.import-status')).toContainText('已设置 2 个字段');

    const responseModal = await openHeaderModal(page, '#resp-pattern-infos');
    await setFunctionCode(responseModal, 'H1080');
    await responseModal.locator('.confirm-btn').click();
    await expect(responseModal).toBeHidden();

    await page.getByLabel('协议项名称').fill('TCP字段校验保存示例');
    await page.locator('#save-protocol-form').click();
    await page.waitForURL(/protocol_items\.html\?/);
    const item = page.locator('.protocol-item').filter({ hasText: 'TCP字段校验保存示例' });
    await expect(item).toHaveCount(1);
    await expect(item).toHaveClass(/tcp/);
});

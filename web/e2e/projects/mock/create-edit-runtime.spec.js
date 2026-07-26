import { test, expect } from '@playwright/test';
import { installMockStatePersistence } from '../../helpers/auth.js';

const MAIN_PATH = '/html/main.html?apiMode=mock';

async function login(page, context, role = 'admin') {
    await installMockStatePersistence(context);
    await context.clearCookies();
    await page.goto(`/html/login.html?apiMode=mock&returnTo=${encodeURIComponent(MAIN_PATH)}`);
    if (role === 'admin') {
        await page.getByRole('button', { name: '切换为管理员登录' }).click();
        await page.getByLabel('密码').fill('admin123');
    }
    await page.getByLabel('note').fill(role === 'admin' ? 'admin' : 'testuser');
    await Promise.all([
        page.waitForURL(/\/html\/main\.html\?apiMode=mock/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
    await expect(page.locator('.service-cards')).toBeVisible();
    await expect(page.locator('.service-card').first()).toBeVisible({ timeout: 10_000 });
}

/**
 * 测试思路：
 *
 * 测什么：验证添加测试服务弹窗的必填校验、HTTP/服务器模式的动态端点提示和提交后
 * 列表更新，确认新服务初始状态为未开启。
 * 为什么这么测：新增弹窗通过 innerHTML 动态创建，字段校验和 change 事件容易在真实
 * 浏览器中失效；HTTP 是最常用的新增路径，应验证完整的弹窗到列表生命周期。
 * 怎么测：登录管理员打开弹窗，空表单提交后确认弹窗仍在；填写名称、HTTP、服务器模式，
 * 检查监听端口为 0 且显示自动分配说明，提交后按标题找到新卡片并检查状态。
 * 示例：空表单 -> 保留弹窗；HTTP服务器模式 -> 监听端口 0 -> 新服务未开启。
 */
test('新增 HTTP 测试服务并显示未开启状态', async ({ page, context }) => {
    await login(page, context);
    await page.locator('#add-service').click();

    const modal = page.locator('.modal-overlay').last();
    await expect(modal.locator('#add-service-form')).toBeVisible();
    await modal.locator('.confirm-btn').click();
    await expect(modal).toBeVisible();

    await modal.locator('#service-name').fill('E2E新增HTTP服务');
    await modal.locator('#protocol-type').selectOption('1');
    await modal.locator('#service-mode').selectOption('1');
    await expect(modal.locator('#service-port')).toHaveValue('0');
    await expect(modal.locator('.path-hint')).toContainText('启动时由后端自动分配监听端口');

    await modal.locator('.confirm-btn').click();
    await expect(modal).toBeHidden();

    const card = page.locator('.service-card').filter({ hasText: 'E2E新增HTTP服务' });
    await expect(card).toHaveCount(1);
    await expect(card.locator('.project-protocol-type .field-value')).toHaveText('HTTP');
    await expect(card.locator('.project-mode .field-value')).toHaveText('服务器模式');
    await expect(card.locator('.project-listen-port .field-value')).toHaveText('未开启');
    await expect(card.locator('.project-status .status')).toHaveText('未开启');
});

/**
 * 测试思路：
 *
 * 测什么：验证新增 TCP 服务时会出现 TCP 格式配置入口，格式弹窗展示长度策略、字节序、
 * 字段列表和字节布局预览，保存默认合法格式后可以完成服务新增。
 * 为什么这么测：TCP 新增比 HTTP 多一层项目格式配置，字段编辑器和服务新增表单之间的
 * dataset 传递是高风险链路，单测无法证明真实浏览器中的弹窗事件和提交顺序。
 * 怎么测：选择 TCP 与服务器模式，打开配置按钮，检查默认 6 个字段和 Body 长度策略，
 * 保存格式后提交新增，最后断言卡片存在且显示格式信息。
 * 示例：新增 TCP -> 配置TCP格式 -> 格式已设置 -> TCP服务卡片显示 Body长度依赖。
 */
test('新增 TCP 测试服务并保存 TCP 格式入口', async ({ page, context }) => {
    await login(page, context);
    await page.locator('#add-service').click();

    const modal = page.locator('.modal-overlay').last();
    await modal.locator('#service-name').fill('E2E新增TCP服务');
    await modal.locator('#protocol-type').selectOption('2');
    await modal.locator('#service-mode').selectOption('1');

    const patternButton = modal.locator('#pattern-infos');
    await expect(patternButton).toBeVisible();
    await patternButton.click();

    const patternModal = page.locator('.config-pattern-modal.is-project-pattern');
    await expect(patternModal).toBeVisible();
    await expect(patternModal.locator('#pattern-length-policy')).toHaveValue('body_length');
    await expect(patternModal.locator('#pattern-byte-order')).toHaveValue('big');
    await expect(patternModal.locator('.pattern-field-container')).toHaveCount(6);
    await expect(patternModal.locator('.pattern-byte-layout')).toBeVisible();
    await expect(patternModal.locator('.pattern-byte-block')).toHaveCount(6);

    await patternModal.locator('.confirm-btn').click();
    await expect(patternModal).toBeHidden();
    await expect(modal.locator('#first-pattern-import-status')).toHaveText('格式已设置');

    await modal.locator('.confirm-btn').click();
    await expect(modal).toBeHidden();

    const card = page.locator('.service-card').filter({ hasText: 'E2E新增TCP服务' });
    await expect(card).toHaveCount(1);
    await expect(card.locator('.project-protocol-type .field-value')).toHaveText('自定义TCP');
    await expect(card.locator('.project-pattern .field-value')).toHaveText('Body长度依赖');
    await expect(card.locator('.project-status .status')).toHaveText('未开启');
});

/**
 * 测试思路：
 *
 * 测什么：验证服务标题行内编辑的空值错误和成功保存，以及服务启动、停止、软删除、
 * 已删除筛选和恢复操作的状态流转。
 * 为什么这么测：这些按钮都会阻止卡片事件并触发异步 Mock mutation，必须检查 DOM 在
 * 每个状态转换后的标题、状态、端点和列表归属，才能发现乐观更新或刷新遗漏。
 * 怎么测：编辑 TCP 示例标题，先保存空值再保存新名称；点击状态按钮完成未开启->开启->未开启，
 * 删除后确认默认列表移除，再通过管理员“已删除”筛选恢复该服务。
 * 示例：TCP服务示例 -> 重命名 -> 开启(30002) -> 停止(未开启) -> 删除 -> 恢复。
 */
test('服务名称编辑、启停、删除和恢复完成生命周期流转', async ({ page, context }) => {
    await login(page, context);
    const card = page.locator('#service-card-2');
    const title = card.locator('.service-title');

    await title.click();
    const titleInput = title.locator('.inline-title-input');
    await titleInput.fill('');
    await card.locator('.inline-title-save').click();
    await expect(card.locator('.inline-title-error')).toHaveText('服务名称不能为空');
    await expect(titleInput).toBeVisible();

    await titleInput.fill('E2E重命名TCP服务');
    await card.locator('.inline-title-save').click();
    await expect(title).toHaveText('E2E重命名TCP服务');

    const statusToggle = card.locator('.service-active-toggle');
    await expect(statusToggle).toHaveAttribute('aria-label', '启动测试服务');
    await statusToggle.click();
    await expect(card.locator('.project-status .status')).toHaveText('开启');
    await expect(card.locator('.project-listen-port .field-value')).toHaveText('30002');
    await expect(statusToggle).toHaveAttribute('aria-label', '停止测试服务');

    await statusToggle.click();
    await expect(card.locator('.project-status .status')).toHaveText('未开启');
    await expect(card.locator('.project-listen-port .field-value')).toHaveText('未开启');

    page.once('dialog', dialog => dialog.accept());
    await card.locator('.delete-service-btn').click();
    await expect.poll(() => page.evaluate(() => (
        window.KitProxy.mocks.state.projects.find(project => Number(project.id) === 2)?.status
    ))).toBe(0);
    await page.locator('#filter-status').selectOption('active');
    await page.locator('#apply-service-filter').click();
    await expect(page.locator('#service-card-2')).toHaveCount(0);

    await page.locator('#filter-status').selectOption('deleted');
    await page.locator('#apply-service-filter').click();
    const deletedCard = page.locator('#service-card-2');
    await expect(deletedCard).toBeVisible();
    await expect(deletedCard.locator('.service-title')).toHaveText('E2E重命名TCP服务');
    await expect(deletedCard.locator('.project-status .status-deleted')).toHaveText('已删除');

    page.once('dialog', dialog => dialog.accept());
    await deletedCard.locator('.restore-service-btn').click();
    await expect(page.locator('#service-card-2')).toHaveCount(0);
});

/**
 * 测试思路：
 * 测什么：验证普通用户删除自己的服务后，默认列表不再显示该软删服务。
 * 为什么这么测：管理员页面默认包含已删除数据以支持恢复，普通用户的服务列表则只返回有效数据；必须在普通用户视角验证删除后的可见性。
 * 怎么测：以 testuser 登录，找到 Mock 自动创建的 HTTP 示例，确认删除对话框后删除，等待列表刷新并断言原卡片消失。
 * 示例：testuser 的 HTTP测试服务示例 -> 删除 -> 默认服务列表不再显示该服务。
 */
test('普通用户删除服务后默认列表不再显示', async ({ page, context }) => {
    await login(page, context, 'normal');
    const card = page.locator('.service-card').filter({ hasText: 'HTTP测试服务示例' }).first();
    await expect(card).toBeVisible();
    const cardId = await card.getAttribute('id');

    page.once('dialog', dialog => dialog.accept());
    await card.locator('.delete-service-btn').click();
    await expect(page.locator(`#${cardId}`)).toHaveCount(0, { timeout: 10_000 });
});

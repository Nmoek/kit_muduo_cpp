import { test, expect } from '@playwright/test';
import { loginAsAdmin } from '../../helpers/auth.js';

/**
 * 测试思路：
 *
 * 测什么：缺少或非法 projectId 时协议项页展示错误和返回服务列表入口。
 * 为什么这么测：协议项页依赖 URL 上的服务上下文，错误地继续请求会产生空白页或错误服务数据。
 * 怎么测：登录后分别访问无 projectId 和不存在的 projectId，检查全局错误弹窗、禁用新增按钮和返回链接。
 * 示例：protocol_items.html -> “缺少或非法的测试服务 ID” -> 返回 main.html。
 */
test('缺少或非法 projectId 的错误处理', async ({ page, context }) => {
    await loginAsAdmin(page, context, '/html/main.html?apiMode=mock');

    let protocolListRequestCount = 0;
    page.on('request', request => {
        const url = new URL(request.url());
        if (url.pathname === '/protocols/list') protocolListRequestCount += 1;
    });

    await page.goto('/html/protocol_items.html?apiMode=mock');
    await expect(page.locator('.global-error-popup.is-visible')).toContainText('缺少或非法的测试服务 ID');
    await expect(page.locator('#add-protocol-item')).toBeDisabled();
    await expect(page.locator('.global-error-action')).toHaveAttribute('href', /main\.html/);

    await page.goto('/html/protocol_items.html?apiMode=mock&projectId=abc');
    await expect(page.locator('.global-error-popup.is-visible')).toContainText('缺少或非法的测试服务 ID');
    await expect(page.locator('#add-protocol-item')).toBeDisabled();
    expect(protocolListRequestCount).toBe(0);

    await page.goto('/html/protocol_items.html?apiMode=mock&projectId=99999');
    await expect(page.locator('.global-error-popup.is-visible')).toContainText('测试服务信息加载失败');
});

/**
 * 测试思路：
 *
 * 测什么：服务上下文、HTTP/TCP 协议项列表、空状态、分页以及新增入口。
 * 为什么这么测：列表页既要显示服务运行信息，又要把协议类型和状态交给后续编辑/运行操作，页面组合错误会导致业务入口失效。
 * 怎么测：打开 HTTP 服务 1 和 TCP 服务 2，检查上下文、卡片字段、分页和新增按钮；用不存在协议的服务上下文检查空状态。
 * 示例：projectId=1 -> HTTP 卡片；projectId=2 -> TCP 卡片；无协议 -> 暂无协议项。
 */
test('服务上下文、HTTP/TCP 列表、空状态和分页', async ({ page, context }) => {
    await loginAsAdmin(page, context, '/html/protocol_items.html?apiMode=mock&projectId=1');
    await expect(page.locator('#protocol-items-title')).toContainText('HTTP测试服务示例');
    await expect(page.locator('#protocol-service-meta')).toContainText('HTTP');
    await expect(page.locator('.protocol-item')).toHaveCount(2);
    await expect(page.locator('.protocol-item.http')).toHaveCount(2);
    const httpItem = page.locator('#protocol-item-1');
    await expect(httpItem.locator('.protocol-tag')).toHaveText('HTTP');
    await expect(httpItem.locator('.protocol-name')).toHaveText('HTTP健康检查示例');
    await httpItem.getByRole('button', { name: '展开协议项详情' }).click();
    await expect(httpItem.locator('[data-field-name="method"] .value')).toHaveText('GET');
    await expect(httpItem.locator('[data-field-name="path"] .value')).toHaveText('/api/test1');
    await expect(httpItem.locator('[data-field-name="status_code"] .value')).toHaveText('200');
    await expect(page.locator('#add-protocol-item')).toBeEnabled();
    await expect(page.locator('#protocol-pagination')).toContainText('第 1 页');

    await page.goto('/html/protocol_items.html?apiMode=mock&projectId=2');
    await expect(page.locator('#protocol-items-title')).toContainText('TCP测试服务示例');
    await expect(page.locator('.protocol-item.tcp')).toHaveCount(3);
    await expect(page.locator('#protocol-service-meta .project-pattern')).toBeVisible();
    await expect(page.locator('#protocol-item-2 .details-grid.tcp')).toBeAttached();
    await expect(page.locator('#protocol-item-2 [data-field-name="fields"]')).toHaveCount(2);
    await expect(page.locator('.protocol-item').filter({ hasText: '待重配置' })).toBeVisible();

    await page.evaluate(() => {
        window.KitProxy.mocks.state.protocols = window.KitProxy.mocks.state.protocols
            .filter(protocol => Number(protocol.project_id) !== 2);
        return window.KitProxy.protocolItemsPage.loadProtocolItems(1);
    });
    await expect(page.locator('.protocol-list .empty-state')).toContainText('暂无协议项，点击按钮添加');
    await expect(page.locator('.protocol-item')).toHaveCount(0);
});

/**
 * 测试思路：
 *
 * 测什么：验证协议项列表按默认每页 5 条分页，下一页显示剩余数据，切换每页 10 条
 * 后回到第一页并展示全部数据。
 * 为什么这么测：分页条同时控制后端 offset/limit 和当前 DOM，只有真实浏览器操作才能
 * 验证页码、列表数量与按钮禁用状态不会互相脱节。
 * 怎么测：在 Mock project 2 中追加 6 条 TCP 协议项，使管理员可见总数达到 9；依次点击
 * 下一页和每页数量 select，断言每页数量、页码及下一页状态。
 * 示例：9 条 -> 第 1 页 5 条 -> 第 2 页 4 条 -> 每页 10 条后第 1 页 9 条。
 */
test('协议项分页和每页数量切换', async ({ page, context }) => {
    await loginAsAdmin(page, context, '/html/protocol_items.html?apiMode=mock&projectId=2');

    await page.evaluate(() => {
        const state = window.KitProxy.mocks.state;
        for (let index = 0; index < 6; index += 1) {
            state.protocols.push({
                id: 600 + index,
                name: `分页 TCP 协议项 ${index + 1}`,
                project_id: 2,
                type: 'TCP',
                req_cfg: { function_code: 'H1000', fields: {} },
                resp_cfg: { function_code: 'H1080', fields: {} },
                req_body_status: 0,
                resp_body_status: 0,
                req_body_type: 'json',
                resp_body_type: 'json',
                status: 1,
                config_state: 0,
                ctime: '2026-01-01 00:00:00',
                utime: '2026-01-01 00:00:00',
            });
        }
        return window.KitProxy.protocolItemsPage.loadProtocolItems(1);
    });

    await expect(page.locator('.protocol-item')).toHaveCount(5);
    await expect(page.locator('.pagination-current')).toHaveText('第 1 页');
    await expect(page.locator('.pagination-next')).toBeEnabled();

    await page.locator('.pagination-next').click();
    await expect(page.locator('.pagination-current')).toHaveText('第 2 页');
    await expect(page.locator('.protocol-item')).toHaveCount(4);
    await expect(page.locator('.pagination-prev')).toBeEnabled();

    await page.locator('.pagination-page-size').selectOption('10');
    await expect(page.locator('.pagination-current')).toHaveText('第 1 页');
    await expect(page.locator('.protocol-item')).toHaveCount(9);
    await expect(page.locator('.pagination-next')).toBeDisabled();
});

/**
 * 测试思路：
 *
 * 测什么：验证 TCP 卡片展示请求/响应字段摘要和已配置字段数量，不展示具体功能码。
 * 为什么这么测：协议项列表页负责快速浏览，具体 function_code 和普通字段值由独立协议项表单
 * 的字段配置用例验证；把完整字段值塞进列表卡片会混淆列表摘要和配置详情的职责。
 * 怎么测：打开 Mock project 2，检查 TCP 请求/响应字段摘要、数量以及卡片中没有具体功能码。
 * 示例：TCP开包检测示例 -> 请求字段“已设置 3 个”、响应字段“已设置 4 个”；具体 H1000
 * 在 F6 TCP 表单字段配置用例中验证。
 */
test('TCP 协议项字段摘要展示，不在卡片展示功能码', async ({ page, context }) => {
    await loginAsAdmin(page, context, '/html/protocol_items.html?apiMode=mock&projectId=2');
    const tcpItem = page.locator('#protocol-item-2');
    await expect(tcpItem.locator('.details-grid.tcp')).toBeAttached();
    const fields = tcpItem.locator('[data-field-name="fields"]');
    await expect(fields).toHaveCount(2);
    await expect(fields.nth(0)).toContainText('请求头部字段值');
    await expect(fields.nth(0)).toContainText('已设置 3 个');
    await expect(fields.nth(1)).toContainText('响应头部字段值');
    await expect(fields.nth(1)).toContainText('已设置 4 个');
    await expect(tcpItem).not.toContainText('H1000');
});

/**
 * 测试思路：
 *
 * 测什么：验证停止状态的 TCP 测试服务仍允许进入新增协议项表单，并保留 projectId、
 * apiMode、apiBaseUrl 和 enableDebugLog 调试参数。
 * 为什么这么测：新增协议项是配置动作，不应被项目当前是否运行错误禁用；参数丢失会让
 * 表单无法确定服务归属或切换到错误 API 模式。
 * 怎么测：带完整调试参数打开 project 2，点击真实新增按钮，等待表单页导航并解析 URL。
 * 示例：projectId=2 -> protocol_item_form.html?projectId=2&apiMode=mock&apiBaseUrl=mock-api&enableDebugLog=true。
 */
test('新增协议项入口保留项目和调试参数', async ({ page, context }) => {
    await loginAsAdmin(page, context, '/html/protocol_items.html?apiMode=mock&projectId=2&apiBaseUrl=mock-api&enableDebugLog=true');
    await expect(page.locator('#add-protocol-item')).toBeEnabled();

    await Promise.all([
        page.waitForURL(/protocol_item_form\.html\?/),
        page.locator('#add-protocol-item').click(),
    ]);

    const url = new URL(page.url());
    expect(url.pathname).toBe('/html/protocol_item_form.html');
    expect(url.searchParams.get('projectId')).toBe('2');
    expect(url.searchParams.get('apiMode')).toBe('mock');
    expect(url.searchParams.get('apiBaseUrl')).toBe('mock-api');
    expect(url.searchParams.get('enableDebugLog')).toBe('true');
});

import { test, expect } from '@playwright/test';
import { installMockStatePersistence } from '../../helpers/auth.js';

const MAIN_RETURN_TO = encodeURIComponent('/html/main.html?apiMode=mock');

async function loginAsAdmin(page, context) {
    await installMockStatePersistence(context);
    await context.clearCookies();
    await page.goto(`/html/login.html?apiMode=mock&returnTo=${MAIN_RETURN_TO}`);
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill('admin');
    await page.getByLabel('密码').fill('admin123');
    await Promise.all([
        page.waitForURL(/\/html\/main\.html\?apiMode=mock/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
    await expect(page.locator('.service-cards')).toBeVisible();
    await expect(page.locator('.service-card').first()).toBeVisible({ timeout: 10_000 });
}

function cards(page) {
    return page.locator('.service-cards .service-card');
}

/**
 * 测试思路：
 * 测什么：验证管理员进入主页面后能看到 HTTP/TCP 测试服务卡片，以及名称、协议、模式、端点和运行状态等关键字段。
 * 为什么这么测：服务列表是服务管理页的主入口，字段缺失会直接影响后续筛选、启停和协议项跳转；这些组合行为需要真实 Chromium DOM 才能确认。
 * 怎么测：登录 Mock 管理员，等待列表加载完成，按固定 Mock 项目 ID 检查两张服务卡片，并检查分页条的默认每页数量和前后页按钮。
 * 示例：admin -> main.html -> HTTP测试服务示例/TCP测试服务示例 -> 每页数量 10。
 */
test('管理员能看到服务列表字段和分页控件', async ({ page, context }) => {
    await loginAsAdmin(page, context);

    const httpCard = page.locator('#service-card-1');
    const tcpCard = page.locator('#service-card-2');
    await expect(httpCard).toBeVisible();
    await expect(tcpCard).toBeVisible();
    await expect(httpCard.locator('.service-title')).toHaveText('HTTP测试服务示例');
    await expect(httpCard.locator('.project-protocol-type .field-value')).toHaveText('HTTP');
    await expect(httpCard.locator('.project-mode .field-value')).toHaveText('服务器模式');
    await expect(httpCard.locator('.project-listen-port .field-value')).toHaveText('18080');
    await expect(httpCard.locator('.project-status .field-value')).toHaveText('开启');
    await expect(tcpCard.locator('.service-title')).toHaveText('TCP测试服务示例');
    await expect(tcpCard.locator('.project-protocol-type .field-value')).toHaveText('自定义TCP');
    await expect(tcpCard.locator('.project-status .field-value')).toHaveText('未开启');
    await expect(tcpCard.locator('.project-pattern')).toContainText('Body长度依赖');

    await expect(page.locator('.pagination-page-size')).toHaveValue('10');
    await expect(page.locator('.pagination-prev')).toBeDisabled();
    await expect(page.locator('.pagination-next')).toBeDisabled();
    await expect(page.locator('.pagination-current')).toHaveText('第 1 页');

    await page.evaluate(async () => {
        for (let index = 0; index < 12; index += 1) {
            window.KitProxy.mocks.addProject({
                name: `E2E分页服务-${index + 1}`,
                protocol_type: index % 2 === 0 ? 1 : 2,
                mode: 1,
                listen_port: 0,
                target_ip: '',
            });
        }
        await window.loadAllProjects(1);
    });

    await expect(cards(page)).toHaveCount(10);
    await expect(page.locator('.pagination-next')).toBeEnabled();
    await page.locator('.pagination-next').click();
    await expect(page.locator('.pagination-current')).toHaveText('第 2 页');
    await expect(cards(page)).toHaveCount(5);
    await expect(page.locator('.pagination-prev')).toBeEnabled();
    await page.locator('.pagination-prev').click();
    await expect(page.locator('.pagination-current')).toHaveText('第 1 页');
    await expect(cards(page)).toHaveCount(10);
});

/**
 * 测试思路：
 * 测什么：验证筛选无匹配项时显示明确空状态，而不是保留旧卡片或显示空白区域。
 * 为什么这么测：筛选发生在当前页数据渲染之后，空状态可以发现列表清理、筛选条件应用和提示文案之间的集成问题。
 * 怎么测：登录管理员，输入 Mock 数据范围之外的创建日期并应用筛选，断言服务卡片清空且出现“当前页无匹配测试服务”。
 * 示例：创建日期 2099-01-01 至 2099-01-02 -> 当前页无匹配测试服务。
 */
test('筛选没有匹配服务时显示空状态', async ({ page, context }) => {
    await loginAsAdmin(page, context);

    await page.locator('#filter-create-start').fill('2099-01-01');
    await page.locator('#filter-create-end').fill('2099-01-02');
    await page.locator('#apply-service-filter').click();

    await expect(cards(page)).toHaveCount(0);
    await expect(page.locator('.empty-state')).toContainText('当前页无匹配测试服务');
    await expect(page.locator('#service-filter-summary')).toContainText('创建日期 2099-01-01 至 2099-01-02');
});

/**
 * 测试思路：
 * 测什么：验证状态筛选、协议类型筛选和管理员 owner note 筛选分别只保留符合条件的服务。
 * 为什么这么测：这些条件由同一个筛选面板驱动，但匹配逻辑分别依赖 runtime_state、protocol_type 和所有者映射，组合断言可以防止字段串用。
 * 怎么测：依次选择开启、未开启、已删除、HTTP、TCP，并输入 owner note=admin；每次应用后检查卡片 ID 集合和筛选摘要。
 * 示例：协议种类=HTTP -> 仅 service-card-1 和 service-card-3；状态=已删除 -> 仅 service-card-3。
 */
test('管理员可以按状态协议类型和 owner note 筛选', async ({ page, context }) => {
    await loginAsAdmin(page, context);

    const applyFilter = async (selector, value) => {
        await page.locator(selector).selectOption(value);
        await page.locator('#apply-service-filter').click();
    };

    await applyFilter('#filter-status', 'active');
    await expect(cards(page)).toHaveCount(1);
    await expect(cards(page).first()).toHaveAttribute('id', 'service-card-1');

    await applyFilter('#filter-status', 'inactive');
    await expect(cards(page)).toHaveCount(1);
    await expect(cards(page).first()).toHaveAttribute('id', 'service-card-2');

    await applyFilter('#filter-status', 'deleted');
    await expect(cards(page)).toHaveCount(1);
    await expect(cards(page).first()).toHaveAttribute('id', 'service-card-3');

    await applyFilter('#filter-status', 'all');
    await applyFilter('#filter-protocol-type', '1');
    await expect(cards(page)).toHaveCount(2);
    const httpIds = await cards(page).evaluateAll(nodes => nodes.map(node => node.id));
    expect(httpIds).toEqual([
        'service-card-1',
        'service-card-3',
    ]);

    await applyFilter('#filter-protocol-type', '2');
    await expect(cards(page)).toHaveCount(1);
    await expect(cards(page).first()).toHaveAttribute('id', 'service-card-2');

    await applyFilter('#filter-protocol-type', 'all');
    await page.locator('#filter-owner-note').fill('admin');
    await page.locator('#apply-service-filter').click();
    await expect(cards(page)).toHaveCount(3);
    await expect(page.locator('#service-filter-summary')).toContainText('所有者：admin');
});

/**
 * 测试思路：
 * 测什么：验证非法日期范围会显示校验错误且不覆盖当前列表，重置后所有控件和摘要恢复默认状态。
 * 为什么这么测：日期校验发生在渲染前，若错误时仍然刷新列表，用户会丢失当前筛选上下文；重置则是筛选面板的基本恢复路径。
 * 怎么测：输入结束日期早于开始日期并应用，断言错误文案；随后点击重置，断言输入为空、下拉框为 all、列表恢复和摘要为未启用筛选。
 * 示例：2025-08-12 至 2025-08-01 -> 创建日期开始时间不能晚于结束时间 -> 重置 -> 默认列表。
 */
test('非法日期筛选显示错误且重置可恢复默认列表', async ({ page, context }) => {
    await loginAsAdmin(page, context);

    await page.locator('#filter-create-start').fill('2025-08-12');
    await page.locator('#filter-create-end').fill('2025-08-01');
    await page.locator('#apply-service-filter').click();
    await expect(page.locator('#service-filter-error')).toHaveText('创建日期开始时间不能晚于结束时间');
    await expect(cards(page)).toHaveCount(3);

    await page.locator('#reset-service-filter').click();
    await expect(page.locator('#filter-create-start')).toHaveValue('');
    await expect(page.locator('#filter-create-end')).toHaveValue('');
    await expect(page.locator('#filter-status')).toHaveValue('all');
    await expect(page.locator('#filter-protocol-type')).toHaveValue('all');
    await expect(page.locator('#filter-owner-note')).toHaveValue('');
    await expect(page.locator('#service-filter-summary')).toHaveText('当前页筛选：未启用筛选');
    await expect(cards(page)).toHaveCount(3);
});

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

async function fillCreateDateRange(page, startDate, endDate) {
    await page.locator('#filter-create-range').click();
    await page.locator('#filter-create-direct-start').fill(startDate);
    await page.locator('#filter-create-direct-end').fill(endDate);
    await page.locator('#filter-create-range-confirm').click();
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
 * 测什么：验证日期面板打开一次后，可以直接在同一个日历网格中连续点击开始和结束日期。
 * 为什么这么测：日历第一次点击会重绘日期按钮，旧按钮脱离 DOM 后不能被外部点击监听误判，否则面板会提前关闭。
 * 怎么测：打开面板，连续点击当月第 5、9 个日期，确认外部值未变化；点击确定后检查已提交值。
 * 示例：当月 5 日 -> 当月 9 日，中间 6、7、8 日高亮，确定前外部值为空，确定后才写入。
 */
test('日期面板一次打开即可在日历中完成两次点击', async ({ page, context }) => {
    await loginAsAdmin(page, context);

    const trigger = page.locator('#filter-create-range');
    const panel = page.locator('#filter-create-range-panel');
    const calendarDays = page.locator('#filter-create-calendar-days [data-date]');
    await trigger.click();

    const startDate = await calendarDays.nth(4).getAttribute('data-date');
    const endDate = await calendarDays.nth(8).getAttribute('data-date');
    await calendarDays.nth(4).click();
    await expect(panel).toBeVisible();
    await expect(page.locator('#filter-create-start')).toHaveValue('');
    await expect(page.locator('#filter-create-end')).toHaveValue('');
    await expect(page.locator('#filter-create-direct-start')).toHaveValue(startDate);

    await calendarDays.nth(8).click();
    await expect(panel).toBeVisible();
    await expect(page.locator('#filter-create-start')).toHaveValue('');
    await expect(page.locator('#filter-create-end')).toHaveValue('');
    await expect(page.locator('#filter-create-start-clock')).toHaveValue('');
    await expect(page.locator('#filter-create-end-clock')).toHaveValue('');
    await expect(page.locator('#filter-create-calendar-days .is-in-range')).toHaveCount(3);
    await page.locator('#filter-create-range-confirm').click();
    await expect(panel).toBeHidden();
    await expect(page.locator('#filter-create-start')).toHaveValue(startDate);
    await expect(page.locator('#filter-create-end')).toHaveValue(endDate);
    await expect(page.locator('#filter-create-start-clock')).toHaveValue('00:00:00');
    await expect(page.locator('#filter-create-end-clock')).toHaveValue('23:59:59');
});

/**
 * 测试思路：
 * 测什么：验证后端筛选结果为空时显示整体筛选空状态，而不是把它误报为当前页无匹配。
 * 为什么这么测：Project 筛选和 total 已经在分页接口中完成，空状态必须由筛选后的 total 驱动。
 * 怎么测：登录管理员，输入 Mock 数据范围之外的创建日期并应用筛选，断言服务卡片清空且出现整体筛选空状态。
 * 示例：创建日期 2099-01-01 至 2099-01-02 -> 没有符合筛选条件的测试服务。
 */
test('筛选没有匹配服务时显示空状态', async ({ page, context }) => {
    await loginAsAdmin(page, context);

    await fillCreateDateRange(page, '2099-01-01', '2099-01-02');
    await expect(page.locator('#filter-create-start-time')).toHaveValue('00:00:00');
    await expect(page.locator('#filter-create-end-time')).toHaveValue('23:59:59');
    await page.locator('#apply-service-filter').click();

    await expect(cards(page)).toHaveCount(0);
    await expect(page.locator('.empty-state')).toContainText('没有符合筛选条件的测试服务');
    await expect(page.locator('#service-filter-summary')).toContainText('创建日期 2099-01-01 00:00:00 至 2099-01-02 23:59:59');
});

/**
 * 测试思路：
 * 测什么：验证状态筛选、协议类型筛选和管理员 owner note 候选筛选分别只保留符合条件的服务。
 * 为什么这么测：这些条件由后端分页查询执行，组合断言可以防止页面漏传字段或把候选 note 当作模糊字符串提交。
 * 怎么测：依次选择开启、未开启、已删除、HTTP、TCP，输入 admin 后点击候选，再应用；每次检查卡片 ID 集合和筛选摘要。
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
        'service-card-3',
        'service-card-1',
    ]);

    await applyFilter('#filter-protocol-type', '2');
    await expect(cards(page)).toHaveCount(1);
    await expect(cards(page).first()).toHaveAttribute('id', 'service-card-2');

    await applyFilter('#filter-protocol-type', 'all');
    await page.locator('#filter-owner-note').fill('admin');
    await expect(page.locator('.filter-candidate-option')).toHaveCount(1);
    await page.locator('.filter-candidate-option').click();
    await page.locator('#apply-service-filter').click();
    await expect(cards(page)).toHaveCount(3);
    await expect(page.locator('#service-filter-summary')).toContainText('所有者：admin');
});

/**
 * 测试思路：输入 note 前缀但没有点击候选时，应用筛选必须只显示校验错误，不发起 Project 列表请求。
 * 示例：admin -> 未选择候选 -> “请选择候选 note”，列表仍保持默认结果。
 */
test('未确认 owner note 候选时不会查询项目列表', async ({ page, context }) => {
    await loginAsAdmin(page, context);

    await page.evaluate(() => {
        const original = window.KitProxy.api.getProjectList;
        window.__projectListCalls = 0;
        window.KitProxy.api.getProjectList = (...args) => {
            window.__projectListCalls += 1;
            return original(...args);
        };
    });

    await page.locator('#filter-owner-note').fill('adm');
    await page.locator('#apply-service-filter').click();

    await expect(page.locator('#service-filter-error')).toHaveText('请选择候选 note');
    await expect(cards(page)).toHaveCount(3);
    expect(await page.evaluate(() => window.__projectListCalls)).toBe(0);
});

/**
 * 测试思路：连续输入期间不能因为短暂停顿提前发起候选请求，必须等待完整防抖窗口。
 * 示例：以 100ms 间隔输入 admin -> 额外等待 300ms 仍不请求 -> 防抖窗口结束后只请求一次。
 */
test('连续输入未结束时不会提前查询 owner note', async ({ page, context }) => {
    await loginAsAdmin(page, context);

    await page.evaluate(() => {
        window.__noteCandidateCalls = 0;
        window.KitProxy.api.getProjectNoteCandidates = () => {
            window.__noteCandidateCalls += 1;
            return Promise.resolve([]);
        };
    });

    const input = page.locator('#filter-owner-note');
    await input.pressSequentially('admin', { delay: 100 });
    await page.waitForTimeout(300);

    expect(await page.evaluate(() => window.__noteCandidateCalls)).toBe(0);
    await expect.poll(
        () => page.evaluate(() => window.__noteCandidateCalls),
        { timeout: 2000 },
    ).toBe(1);
    await expect(page.locator('#filter-owner-candidates')).toContainText('未搜索到匹配项');
});

/**
 * 测试思路：候选请求期间锁定输入框并展示进行中状态，请求成功但无结果时展示明确空结果提示。
 * 示例：输入 unknown，延迟释放 Mock 请求 -> 输入框禁用且显示“正在搜索” -> 恢复后显示“未搜索到匹配项”。
 */
test('note 候选查询显示搜索中和无匹配状态', async ({ page, context }) => {
    await loginAsAdmin(page, context);

    await page.evaluate(() => {
        window.KitProxy.api.getProjectNoteCandidates = () => new Promise(resolve => {
            window.__resolveNoteCandidates = resolve;
        });
    });

    const input = page.locator('#filter-owner-note');
    const candidates = page.locator('#filter-owner-candidates');
    await input.fill('unknown');

    await expect(input).toBeDisabled();
    await expect(candidates).toBeVisible();
    await expect(candidates).toContainText('正在搜索');

    await page.evaluate(() => window.__resolveNoteCandidates([]));
    await expect(input).toBeEnabled();
    await expect(input).toBeFocused();
    await expect(candidates).toContainText('未搜索到匹配项');
});

/**
 * 测试思路：候选请求未返回时，任何输入事件都不能解除输入框锁定或清除“正在搜索”状态。
 * 示例：输入 unknown -> 请求中尝试清空输入 -> 仍显示“正在搜索” -> 响应返回后才恢复可编辑。
 */
test('候选请求返回前不能编辑或清空 note', async ({ page, context }) => {
    await loginAsAdmin(page, context);

    await page.evaluate(() => {
        window.KitProxy.api.getProjectNoteCandidates = () => new Promise(resolve => {
            window.__resolveNoteCandidatesAfterClear = resolve;
        });
    });

    const input = page.locator('#filter-owner-note');
    const candidates = page.locator('#filter-owner-candidates');
    await input.fill('unknown');
    await expect(input).toBeDisabled();
    await expect(candidates).toContainText('正在搜索');

    await page.evaluate(() => {
        const ownerInput = document.querySelector('#filter-owner-note');
        ownerInput.value = '';
        ownerInput.dispatchEvent(new Event('input', { bubbles: true }));
    });
    await expect(input).toBeDisabled();
    await expect(candidates).toContainText('正在搜索');

    await page.evaluate(() => window.__resolveNoteCandidatesAfterClear([]));
    await expect(input).toBeEnabled();
    await expect(candidates).toContainText('未搜索到匹配项');
});

/**
 * 测试思路：候选接口迟迟不返回时，候选栏必须给出超时提示，并结束本次锁定状态。
 * 示例：输入 unknown -> 接口持续 pending -> 显示“搜索超时，请重试”并允许重新输入。
 */
test('note 候选查询超时后提示并允许重试', async ({ page, context }) => {
    await loginAsAdmin(page, context);

    await page.evaluate(() => {
        window.KitProxy.api.getProjectNoteCandidates = () => new Promise(() => {});
    });

    const input = page.locator('#filter-owner-note');
    const candidates = page.locator('#filter-owner-candidates');
    await input.fill('unknown');
    await expect(input).toBeDisabled();
    await expect(candidates).toContainText('正在搜索');

    await expect(candidates).toContainText('搜索超时，请重试', { timeout: 7000 });
    await expect(input).toBeEnabled();
});

/**
 * 测试思路：验证筛选发生在分页之前，原始第一页没有命中的项目也能出现在筛选结果第一页。
 * 为什么这么测：这是 Project 筛选补全的核心回归场景，当前页本地过滤会把后页命中项错误地漏掉。
 * 怎么测：构造 10 条较新的 TCP 项目和 1 条较旧的 HTTP 项目，先加载未筛选第一页，再应用 HTTP 筛选。
 * 示例：原始第一页只有 TCP，协议=HTTP 后应直接得到 service-card-200，且分页回到第 1 页。
 */
test('筛选会在分页前执行并找到原始后续页项目', async ({ page, context }) => {
    await loginAsAdmin(page, context);

    await page.evaluate(async () => {
        const state = window.KitProxy.mocks.state;
        const template = state.projects[0];
        state.projects = Array.from({ length: 10 }, (_, index) => ({
            ...template,
            id: 100 + index,
            name: `较新TCP服务-${index}`,
            protocol_type: 2,
            status: 1,
            runtime_state: 0,
            user_id: 1,
            ctime: new Date(Date.now() + index * 1000).toISOString(),
        })).concat({
            ...template,
            id: 200,
            name: '原始后页HTTP服务',
            protocol_type: 1,
            status: 1,
            runtime_state: 0,
            user_id: 1,
            ctime: '2020-01-01T00:00:00.000Z',
        });
        await window.loadAllProjects(1);
    });

    await expect(cards(page)).toHaveCount(10);
    await page.locator('#filter-protocol-type').selectOption('1');
    await page.locator('#apply-service-filter').click();

    await expect(cards(page)).toHaveCount(1);
    await expect(cards(page).first()).toHaveAttribute('id', 'service-card-200');
    await expect(page.locator('.pagination-current')).toHaveText('第 1 页');
    await expect(page.locator('#service-filter-summary')).toContainText('共 1 条');
});

/**
 * 测试思路：
 * 测什么：验证非法日期范围在配置框底部报错，确定按钮不提交，整体重置后恢复默认状态。
 * 为什么这么测：时间配置采用草稿提交，非法草稿不能污染外部展示和查询条件。
 * 怎么测：输入结束日期早于开始日期，断言面板保持打开、底部显示错误、确定按钮禁用且外部值为空；随后整体重置。
 * 示例：2025-08-12 至 2025-08-01 -> 底部报错且不提交 -> 重置 -> 默认列表。
 */
test('非法日期筛选显示错误且重置可恢复默认列表', async ({ page, context }) => {
    await loginAsAdmin(page, context);

    await page.locator('#filter-create-range').click();
    await page.locator('#filter-create-direct-start').fill('2025-08-12');
    await page.locator('#filter-create-direct-end').fill('2025-08-01');
    await expect(page.locator('#filter-create-range-panel')).toBeVisible();
    await expect(page.locator('#filter-create-range-error')).toHaveText('结束日期不能早于开始日期');
    await expect(page.locator('#filter-create-range-confirm')).toBeDisabled();
    await expect(page.locator('#filter-create-start')).toHaveValue('');
    await expect(page.locator('#filter-create-end')).toHaveValue('');
    await expect(cards(page)).toHaveCount(3);

    await page.locator('#reset-service-filter').click();
    await expect(page.locator('#filter-create-range')).toHaveAttribute('aria-label', '配置起止日期');
    await expect(page.locator('#filter-create-start')).toHaveValue('');
    await expect(page.locator('#filter-create-end')).toHaveValue('');
    await expect(page.locator('#filter-create-direct-start')).toHaveValue('');
    await expect(page.locator('#filter-create-direct-end')).toHaveValue('');
    await expect(page.locator('#filter-create-start-time')).toHaveValue('');
    await expect(page.locator('#filter-create-end-time')).toHaveValue('');
    await expect(page.locator('#filter-status')).toHaveValue('all');
    await expect(page.locator('#filter-protocol-type')).toHaveValue('all');
    await expect(page.locator('#filter-owner-note')).toHaveValue('');
    await expect(page.locator('#service-filter-summary')).toHaveText('未启用筛选，共 3 条');
    await expect(cards(page)).toHaveCount(3);
});

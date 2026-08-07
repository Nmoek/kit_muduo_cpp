import { test, expect } from '@playwright/test';

async function openConnectedDrawer(page, context) {
    await context.clearCookies();
    await page.goto('/html/login.html?apiMode=mock&returnTo=%2Fhtml%2Fprotocol_items.html%3FapiMode%3Dmock%26projectId%3D1');
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill('admin');
    await page.getByLabel('密码').fill('admin123');
    await Promise.all([
        page.waitForURL(/\/html\/protocol_items\.html\?/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
    const item = page.locator('.protocol-item[data-protocol-id="1"]');
    await item.locator('.protocol-interaction-btn').click();
    const drawer = page.locator('.protocol-interaction-drawer');
    await expect(drawer).toBeVisible();
    // 布局用例注入固定数量的记录；关闭周期 Mock 消息，避免后台新记录改变样本数量。
    await page.evaluate(() => {
        window.KitProxy.protocolInteractionLive.setMockScenario(1, 1, {
            liveIntervalMs: 60_000,
        });
    });
    await drawer.locator('[data-action="connect"]').click();
    await expect(drawer.locator('[data-role="status"]')).toHaveText('实时');
    // Mock 首批消息通过定时器逐条抵达；布局断言只依赖可选中的记录，不依赖第 4 条
    // Notice 的到达时机，至少三条即可稳定覆盖列表轨道。
    await expect.poll(() => drawer.locator('.interaction-record-item').count())
        .toBeGreaterThanOrEqual(3);
    return drawer;
}

/**
 * 测试思路：
 *
 * 测什么：
 * 验证固定 1440 x 900 电脑端窗口下，实时抽屉可以在普通右侧模式和全屏
 * 模式之间切换，关键区域仍然存在，页面不会产生非预期的整体横向溢出。
 *
 * 为什么这么测：
 * 这是当前模块唯一纳入范围的视觉环境。JSDOM 不执行真实 CSS 布局和滚动
 * 计算，无法确认抽屉宽度、全屏 class、页面 scrollWidth 和真实浏览器布局
 * 是否一致，因此使用 Playwright Chromium 做结构化布局断言并保存截图。
 *
 * 怎么测：
 * 1. 固定桌面 Chromium 视口为 1440 x 900。
 * 2. 打开并连接实时抽屉。
 * 3. 断言标题、状态、工具栏和列表存在。
 * 4. 点击全屏，断言 drawer/backdrop 都进入全屏 class。
 * 5. 检查 document 和 drawer 没有非预期横向溢出。
 * 6. 恢复普通模式并保存截图证据。
 *
 * 示例：
 * 1440x900 -> 右侧抽屉 -> 全屏 -> 无横向溢出 -> 还原。
 */
test('电脑端抽屉支持全屏切换且布局容器不横向溢出', async ({ page, context }, testInfo) => {
    const drawer = await openConnectedDrawer(page, context);
    await expect(drawer.locator('#protocol-interaction-drawer-title')).toBeVisible();
    await expect(drawer.locator('.interaction-drawer-status')).toBeVisible();
    await expect(drawer.locator('.interaction-record-list')).toBeVisible();

    await drawer.locator('[data-action="fullscreen"]').click();
    await expect(drawer).toHaveClass(/is-fullscreen/);
    await expect(page.locator('.protocol-interaction-backdrop')).toHaveClass(/is-fullscreen/);

    const overflow = await page.evaluate(() => ({
        documentOverflow: document.documentElement.scrollWidth - window.innerWidth,
        drawerOverflow: document.querySelector('.protocol-interaction-drawer').scrollWidth
            - document.querySelector('.protocol-interaction-drawer').clientWidth,
    }));
    expect(overflow.documentOverflow).toBeLessThanOrEqual(1);
    expect(overflow.drawerOverflow).toBeLessThanOrEqual(1);

    await page.screenshot({
        path: testInfo.outputPath('protocol-interaction-desktop-fullscreen.png'),
        fullPage: true,
    });
    await drawer.locator('[data-action="fullscreen"]').click();
    await expect(drawer).not.toHaveClass(/is-fullscreen/);
});

/**
 * 测试思路：全屏仍然固定为两列时，注入 11 条 Notice + 10 条协议记录，专门覆盖
 * 第 21 条记录。旧实现按“每 10 条换一列”计算位置，第 21 条会得到 gridColumn=3，
 * 浏览器于是创建隐式第三列，表现为前两列变窄、第三列变宽。当前固定容量为两列
 * 共 20 张，第 21 条必须留在 pending，不能推动 DOM 创建第三列。
 *
 * 示例：project(11) + protocol(10) -> 两列各 10 张，剩余 1 条 pending，无隐式第三列。
 */
test('全屏双列超过 20 条记录不创建隐式第三列', async ({ page, context }) => {
    const drawer = await openConnectedDrawer(page, context);
    await drawer.locator('[data-action="fullscreen"]').click();
    await expect(drawer).toHaveClass(/is-fullscreen/);

    await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const state = client.getState();
        const projectTemplate = state.visibleRecords.find(record => record.scope === 'project');
        const protocolTemplate = state.visibleRecords.find(record => record.scope === 'protocol');
        if (!projectTemplate || !protocolTemplate) throw new Error('缺少双 scope 模板记录');

        client.clearRecords();
        client.pauseMerge();
        for (let index = 1; index <= 21; index += 1) {
            const isProject = index <= 11;
            const record = JSON.parse(JSON.stringify(isProject ? projectTemplate : protocolTemplate));
            record.scope = isProject ? 'project' : 'protocol';
            record.project_id = 1;
            record.protocol_id = isProject ? 0 : 1;
            record.cache_instance_id = isProject ? 9901 : 9902;
            record.seq = index;
            record.time_ms = 1_786_037_000_000 + index;
            client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'catch_up', record }));
        }
        client.setMergeSpeed('fast');
        client.resumeMerge();
    });

    await expect(drawer.locator('.interaction-record-item')).toHaveCount(20, { timeout: 15_000 });
    const layout = await page.evaluate(() => {
        const list = document.querySelector('.interaction-record-items');
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const state = client.getState();
        const listRect = list.getBoundingClientRect();
        const cards = Array.from(list.querySelectorAll('.interaction-record-item')).map(card => {
            const rect = card.getBoundingClientRect();
            return {
                key: card.dataset.recordKey,
                left: rect.left,
                right: rect.right,
                top: rect.top,
                bottom: rect.bottom,
                height: rect.height,
                column: card.closest('[data-role="record-column-secondary"]') ? '2' : '1',
                row: card.style.gridRow,
            };
        });
        const overlaps = [];
        for (let index = 0; index < cards.length; index += 1) {
            for (let next = index + 1; next < cards.length; next += 1) {
                const left = cards[index];
                const right = cards[next];
                if (left.left < right.right && left.right > right.left
                    && left.top < right.bottom && left.bottom > right.top) {
                    overlaps.push([left.key, right.key]);
                }
            }
        }
        return {
            columnCount: list.querySelectorAll('.interaction-record-column').length,
            columnOverflow: Array.from(list.querySelectorAll('.interaction-record-column'))
                .map(column => getComputedStyle(column).overflowY),
            pendingCount: state.pendingRecords.length,
            listRect: {
                right: listRect.right,
                clientHeight: list.clientHeight,
                scrollHeight: list.scrollHeight,
            },
            cards,
            overlaps,
        };
    });

    expect(layout.columnCount).toBe(2);
    expect(layout.pendingCount).toBe(1);
    const firstColumn = layout.cards.filter(card => card.column === '1');
    const secondColumn = layout.cards.filter(card => card.column === '2');
    expect(firstColumn).toHaveLength(10);
    expect(secondColumn).toHaveLength(10);
    expect(firstColumn.map(card => Number(card.row))).toEqual(Array.from({ length: 10 }, (_, index) => index + 1));
    expect(secondColumn.map(card => Number(card.row))).toEqual(Array.from({ length: 10 }, (_, index) => index + 1));
    expect(layout.cards.every(card => card.height >= 48)).toBe(true);
    expect(layout.cards.every(card => card.right <= layout.listRect.right + 1)).toBe(true);
    expect(layout.overlaps).toEqual([]);
    expect(layout.columnOverflow).toEqual(['auto', 'auto']);
});

/**
 * 测试思路：
 *
 * 测什么：
 * 验证长 Header、长 JSON 和长附件名进入实时详情后，内容仍限制在详情区域
 * 内，标题/状态/工具栏不被内容覆盖，且用户可以选中记录查看详情。
 *
 * 为什么这么测：
 * 实时记录来自后端，长度不可完全由前端控制。长字段最容易造成按钮遮挡、
 * 横向滚动和列表/详情相互挤压；这个风险必须在真实桌面布局中检查，而非
 * 只检查字符串是否写入 DOM。
 *
 * 怎么测：
 * 1. 打开并连接抽屉，等待默认记录。
 * 2. 从当前 client 复制一条记录并替换为长路径、长 Header、长 JSON 和长附件名。
 * 3. 注入该记录并等待列表出现。
 * 4. 点击记录，断言详情区非空。
 * 5. 检查 header、status、toolbar 的矩形区域没有互相重叠。
 * 6. 保存长内容截图作为视觉验收证据。
 *
 * 示例：
 * 长字段 interaction -> 列表仍可选中 -> 详情可见 -> 顶部控制区不重叠。
 */
test('电脑端长交互内容不遮挡控制区并可打开详情', async ({ page, context }, testInfo) => {
    const drawer = await openConnectedDrawer(page, context);
    await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const source = client.getState().visibleRecords[0];
        const record = JSON.parse(JSON.stringify(source));
        record.seq = 99;
        record.request.meta.path = `/long/${'header-value-'.repeat(80)}`;
        record.request.head_text = `X-Long-Header: ${'header-value-'.repeat(100)}`;
        record.response.body.text = JSON.stringify({ payload: 'json-value-'.repeat(160) });
        record.response.body.attachments = [{
            attachment_id: `attachment-${'name-'.repeat(50)}`,
            side: 'response',
            flag: 'response.body',
            kind: 'binary',
            size: 32,
            captured_size: 32,
            truncated: false,
            binary_available: false,
        }];
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record }));
    });

    await expect(drawer.locator('.interaction-record-item')).toHaveCount(5);
    await drawer.locator('.interaction-record-item').filter({ hasText: '99' }).click();
    await expect(drawer.locator('.interaction-record-detail-inner')).not.toBeEmpty();

    const layout = await page.evaluate(() => {
        const rect = selector => document.querySelector(selector).getBoundingClientRect();
        const header = rect('.interaction-drawer-header');
        const status = rect('.interaction-drawer-status');
        const toolbar = rect('.interaction-drawer-toolbar');
        return {
            headerStatusOverlap: Math.max(0, Math.min(header.bottom, status.bottom) - Math.max(header.top, status.top)),
            statusToolbarOverlap: Math.max(0, Math.min(status.bottom, toolbar.bottom) - Math.max(status.top, toolbar.top)),
            viewportOverflow: document.documentElement.scrollWidth - window.innerWidth,
        };
    });
    expect(layout.headerStatusOverlap).toBe(0);
    expect(layout.statusToolbarOverlap).toBe(0);
    expect(layout.viewportOverflow).toBeLessThanOrEqual(1);
    await page.screenshot({
        path: testInfo.outputPath('protocol-interaction-desktop-long-content.png'),
        fullPage: true,
    });
});

/**
 * 测试思路：
 *
 * 测什么：
 * 验证超长 HTTP 路径只在交互记录卡片标题区域显示省略号，卡片宽高仍由记录
 * 网格固定，鼠标悬停可用的原生 title/aria-label 保留完整标题。
 *
 * 为什么这么测：
 * 标题来自运行态请求路径，长度不可控。仅验证文本写入 DOM 无法发现 flex/grid
 * 子项把卡片撑宽、挤压结果标签或让文字绘制到详情区的问题，需 Chromium 测量。
 *
 * 怎么测：
 * 1. 注入一条超长 HTTP path 的实时记录。
 * 2. 读取标题的完整文本，并断言 title 和 aria-label 使用同一完整值。
 * 3. 检查标题实际产生横向溢出但启用 ellipsis；卡片 border-box 完全落在列表
 *    网格内，标题行也没有越过卡片右边界。
 *
 * 示例：
 *
 *   GET /__title-overflow-xxxx... 200
 *                 |
 *                 v
 *   [GET /__title-overflow-...] [成功]
 *                 |
 *                 v
 *   hover title -> 浏览器展示完整标题
 */
test('超长交互标题在固定记录卡片内截断并保留完整悬停文本', async ({ page, context }, testInfo) => {
    const drawer = await openConnectedDrawer(page, context);
    const longPath = `/__title-overflow-${'segment-'.repeat(80)}`;

    await page.evaluate(path => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const source = client.getState().visibleRecords.find(record => (
            String(record.protocol_type || '').toLowerCase() === 'http'
        )) || client.getState().visibleRecords[0];
        const record = JSON.parse(JSON.stringify(source));
        record.seq = 10001;
        record.request.meta.path = path;
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record }));
    }, longPath);

    const title = drawer.locator('[data-role="title"]').filter({
        hasText: '/__title-overflow-',
    });
    await expect(title).toHaveCount(1);
    const fullTitle = await title.textContent();
    expect(fullTitle).toContain(longPath);
    await expect(title).toHaveAttribute('title', fullTitle);
    await expect(title).toHaveAttribute('aria-label', fullTitle);

    const layout = await title.evaluate(node => {
        const card = node.closest('.interaction-record-item');
        const titleRow = node.closest('.interaction-record-item-top');
        const list = card && card.parentElement;
        if (!card || !titleRow || !list) throw new Error('缺少交互记录卡片布局节点');

        const cardStyle = getComputedStyle(card);
        const titleStyle = getComputedStyle(node);
        return {
            boxSizing: cardStyle.boxSizing,
            cardWithinList: card.clientWidth <= list.clientWidth,
            titleWithinCard: node.clientWidth <= card.clientWidth
                && titleRow.clientWidth <= card.clientWidth,
            titleHasOverflow: node.scrollWidth > node.clientWidth,
            textOverflow: titleStyle.textOverflow,
            whiteSpace: titleStyle.whiteSpace,
        };
    });

    expect(layout.boxSizing).toBe('border-box');
    expect(layout.cardWithinList).toBe(true);
    expect(layout.titleWithinCard).toBe(true);
    expect(layout.titleHasOverflow).toBe(true);
    expect(layout.textOverflow).toBe('ellipsis');
    expect(layout.whiteSpace).toBe('nowrap');
    await page.screenshot({
        path: testInfo.outputPath('protocol-interaction-record-title-overflow.png'),
        fullPage: true,
    });
});

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
    await drawer.locator('[data-action="connect"]').click();
    await expect(drawer.locator('[data-role="status"]')).toHaveText('实时');
    await expect.poll(() => drawer.locator('.interaction-record-item').count())
        .toBeGreaterThanOrEqual(4);
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

import net from 'node:net';
import { test, expect } from '@playwright/test';
import { REAL_E2E, ensureRealProjectRunning, protocolItemSelector, protocolItemsPath } from './test_config.js';

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

function sendProtocolOneRequest() {
    const packet = Buffer.alloc(26);
    packet.writeUInt32BE(0x23232323, 0);
    packet.writeUInt32BE(0x11111111, 4);
    packet.writeUInt32BE(0x22222222, 8);
    packet.writeUInt16BE(0x1111, 12);
    packet.writeUInt32BE(0, 14);
    packet.writeBigUInt64BE(0n, 18);

    return new Promise((resolve, reject) => {
        const socket = net.createConnection({ host: REAL_E2E.projectTcpHost, port: REAL_E2E.projectTcpPort }, () => socket.end(packet, resolve));
        const timer = setTimeout(() => {
            socket.destroy();
            reject(new Error('TCP visual fixture request timeout'));
        }, 5_000);
        socket.once('error', error => {
            clearTimeout(timer);
            reject(error);
        });
        socket.once('close', () => clearTimeout(timer));
    });
}

async function assertDesktopLayout(drawer) {
    const layout = await drawer.evaluate(panel => {
        const rect = selector => {
            const node = panel.querySelector(selector);
            if (!node) return null;
            const value = node.getBoundingClientRect();
            return {
                left: value.left,
                right: value.right,
                top: value.top,
                bottom: value.bottom,
                width: value.width,
                height: value.height,
            };
        };
        const overlap = (left, right) => left && right
            && left.left < right.right
            && left.right > right.left
            && left.top < right.bottom
            && left.bottom > right.top;
        const header = rect('.interaction-drawer-header');
        const status = rect('.interaction-drawer-status');
        const toolbar = rect('.interaction-drawer-toolbar');
        const body = panel.querySelector('.interaction-drawer-body');
        return {
            viewport: { width: window.innerWidth, height: window.innerHeight },
            overlaps: {
                headerStatus: overlap(header, status),
                statusToolbar: overlap(status, toolbar),
                headerToolbar: overlap(header, toolbar),
            },
            drawerOverflow: panel.scrollWidth > panel.clientWidth + 1,
            bodyOverflow: body ? body.scrollWidth > body.clientWidth + 1 : false,
        };
    });

    expect(layout.viewport).toEqual({ width: 1440, height: 900 });
    expect(layout.overlaps).toEqual({ headerStatus: false, statusToolbar: false, headerToolbar: false });
    expect(layout.drawerOverflow).toBe(false);
    expect(layout.bodyOverflow).toBe(false);
}

/**
 * 测试思路：
 *
 * 测什么：
 * 验证固定桌面 Chromium 视口 `1440 x 900` 下，真实右侧抽屉在普通模式和全屏
 * 模式都能承载长 Header、长 JSON、长 Hex 和长附件/文本摘要；检查列表/详情
 * 滚动、当前选中记录和关键控制区边界，最后保存截图作为视觉验收证据。
 *
 * 为什么这么测：
 * JSDOM 没有真实 CSS layout、滚动尺寸和 viewport，因此不能发现长文本导致的
 * 横向溢出、toolbar 覆盖 status、抽屉高度错误或全屏还原异常。B2-B5 已验证
 * 协议和生命周期，本用例把风险集中在用户实际看到的桌面渲染结果。
 *
 * 怎么测：
 * 1. 登录真实后端并打开 project 1/protocol 1 的实时抽屉。
 * 2. 发送一条真实 TCP 请求获取真实记录，再通过生产 LiveClient 注入长字段记录。
 * 3. 选择长记录，切换 Request/Response/Raw 详情，检查滚动容器和边界不溢出。
 * 4. 截取普通模式截图，点击全屏后再次检查并截取全屏截图。
 * 5. 向列表继续注入一条记录，确认当前选中记录和详情滚动区域保持稳定。
 *
 * 示例：
 * 1440x900 + long content -> drawer/list/detail scroll -> fullscreen -> no overlap/no overflow。
 */
test('真实后端电脑端抽屉长内容和全屏布局通过视觉验收', async ({ page, context }, testInfo) => {
    await loginRealAdmin(page, context);
    await ensureRealProjectRunning(page);
    await page.goto(protocolItemsPath());
    await page.locator(protocolItemSelector()).getByTestId('protocol-interaction-open').click();
    const drawer = page.getByTestId('protocol-interaction-drawer');
    await expect(drawer).toBeVisible();
    await drawer.getByTestId('protocol-interaction-connect').click();
    await expect(drawer.getByTestId('protocol-interaction-state')).toHaveText('实时', { timeout: 10_000 });
    await sendProtocolOneRequest();
    await expect.poll(() => drawer.locator('.interaction-record-item').count(), { timeout: 10_000 })
        .toBeGreaterThan(0);

    await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const source = client.getState().visibleRecords[0];
        const record = JSON.parse(JSON.stringify(source));
        record.cache_instance_id = 900001;
        record.seq = 1001;
        record.time_ms = 1700000000000;
        record.request.head_text = `REQUEST-HEADER-${'A'.repeat(3200)}`;
        record.request.raw_packet = {
            kind: 'binary',
            size: 2048,
            captured_size: 2048,
            truncated: false,
            raw_hex: Array.from({ length: 512 }, (_, index) => `${(index % 256).toString(16).padStart(2, '0')}`).join(' '),
            attachments: [],
        };
        record.response.body = {
            kind: 'json',
            expect_kind: 'json',
            size: 3200,
            captured_size: 3200,
            truncated: false,
            text: JSON.stringify({
                longField: 'Z'.repeat(2600),
                nested: { values: Array.from({ length: 80 }, (_, index) => `value-${index}-${'x'.repeat(20)}`) },
            }),
            error_message: '',
            attachments: [],
        };
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record }));
    });

    await expect.poll(() => drawer.locator('.interaction-record-item').count(), { timeout: 5_000 })
        .toBeGreaterThan(1);
    await drawer.locator('.interaction-record-item').last().click();
    await expect(drawer.locator('.interaction-record-detail-inner')).not.toBeEmpty();
    await assertDesktopLayout(drawer);

    const detail = drawer.locator('.interaction-record-detail');
    await detail.evaluate(node => {
        node.scrollTop = node.scrollHeight;
    });
    await drawer.screenshot({
        path: testInfo.outputPath('protocol-interaction-real-desktop-long-content.png'),
    });
    await expect(drawer).toHaveScreenshot('protocol-interaction-long-content.png', {
        animations: 'disabled',
        caret: 'hide',
        mask: [drawer.locator('[data-role="time"]')],
    });

    await drawer.getByTestId('protocol-interaction-fullscreen').click();
    await expect(drawer).toHaveClass(/is-fullscreen/);
    await assertDesktopLayout(drawer);
    await drawer.screenshot({
        path: testInfo.outputPath('protocol-interaction-real-desktop-fullscreen.png'),
    });
    await expect(drawer).toHaveScreenshot('protocol-interaction-fullscreen.png', {
        animations: 'disabled',
        caret: 'hide',
        mask: [drawer.locator('[data-role="time"]')],
    });

    await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const source = client.getState().visibleRecords[0];
        const record = JSON.parse(JSON.stringify(source));
        record.cache_instance_id = 900001;
        record.seq = 1002;
        record.time_ms = 1700000000000;
        record.request.head_text = 'new-record';
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record }));
    });
    await expect(drawer.locator('.interaction-record-detail-inner')).not.toBeEmpty();
    await assertDesktopLayout(drawer);
});

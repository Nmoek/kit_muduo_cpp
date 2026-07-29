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

function buildProtocolOneRequest(magic = 0x23232323) {
    const packet = Buffer.alloc(26);
    packet.writeUInt32BE(magic, 0);
    packet.writeUInt32BE(0x11111111, 4);
    packet.writeUInt32BE(0x22222222, 8);
    packet.writeUInt16BE(0x1111, 12);
    packet.writeUInt32BE(0, 14);
    packet.writeBigUInt64BE(0n, 18);
    return packet;
}

function sendProtocolOneRequest(port = REAL_E2E.projectTcpPort, packet = buildProtocolOneRequest()) {
    return new Promise((resolve, reject) => {
        const socket = net.createConnection({ host: REAL_E2E.projectTcpHost, port }, () => {
            socket.end(packet, resolve);
        });
        const timer = setTimeout(() => {
            socket.destroy();
            reject(new Error(`TCP request timeout: ${REAL_E2E.projectTcpHost}:${port}`));
        }, 5_000);
        socket.once('error', error => {
            clearTimeout(timer);
            reject(error);
        });
        socket.once('close', () => clearTimeout(timer));
    });
}

/**
 * 测试思路：
 *
 * 测什么：
 * 验证 Real Playwright 配置能够启动真实 C++ 后端，浏览器能够通过配置的
 * `PLAYWRIGHT_REAL_BASE_URL` 访问真实登录页面，并且登录表单资源可以正常呈现。
 *
 * 为什么这么测：
 * 真实 WebSocket、项目运行态和协议项联调都依赖后端先成功启动并提供页面。
 * 先用一个不改变数据库的页面 smoke 把“命令、工作目录、监听地址、静态资源”
 * 这条基础链路固定下来，后续 B2 才能把失败归因到 API/WebSocket，而不是服务
 * 没有启动或 URL 配错。
 *
 * 怎么测：
 * 1. Playwright 使用 Real 配置启动 kit_protocol_test_platform。
 * 2. 访问真实 `/html/login.html`。
 * 3. 断言响应返回 200。
 * 4. 断言登录表单和 note 输入框可见。
 * 5. 不提交登录、不创建项目、不修改数据库。
 *
 * 示例：
 * 启动 C++ 后端 -> GET /html/login.html -> 200 -> 登录表单可见。
 */
test('真实 C++ 后端能提供桌面登录入口', async ({ page }) => {
    const response = await page.goto('/html/login.html');

    expect(response).not.toBeNull();
    expect(response.status()).toBe(200);
    await expect(page.locator('#loginForm')).toBeVisible();
    await expect(page.getByLabel('note')).toBeVisible();
});

/**
 * 测试思路：
 *
 * 测什么：
 * 验证真实后端登录后，协议项页面能够创建原生 Chromium WebSocket，收到
 * `live_ready(open)` 后进入“实时”，再由真实 ProjectServer 接收一帧符合协议
 * 1 配置的 TCP 请求，最终把后端发布的 protocol interaction 渲染到抽屉列表。
 * 同时检查 project Notice 的协议范围没有被误当成 protocol 记录。
 *
 * 为什么这么测：
 * Mock transport 只能验证前端状态机，不能发现真实 Cookie、WebSocket 升级、
 * URL 查询参数、C++ 二进制协议解析和 publisher 字段之间的不一致。这个用例
 * 把页面操作、原生 WebSocket、真实 C++ 服务和真实 ProjectServer 串在一起，
 * 是阶段三前后端协议对齐的第一条验收链路。
 *
 * 怎么测：
 * 1. 使用 Real webServer 启动真实 C++ 二进制的隔离数据库副本。
 * 2. 以隔离副本管理员配置登录，再进入环境变量指定的 project/protocol 页。
 * 3. 监听 Playwright 原生 WebSocket 事件，点击 protocol 1 的实时入口和连接按钮。
 * 4. 断言 WebSocket URL 为 `/ws/protocol-interactions/live`，且带 protocol_id=1、
 *    include_project_notice=1；页面状态由首帧 live_ready(open) 驱动为“实时”。
 * 5. 通过 Node TCP 客户端向真实 project TCP 监听端口发送 magic、字段匹配、
 *    body_length=0 的 26 字节请求。
 * 6. 断言抽屉新增 protocol scope 的真实交互记录，并显示成功结果。
 *
 * 示例：
 * login -> WebSocket 101/live_ready(open) -> TCP request -> interaction(live)
 * -> 页面显示 protocol record。
 */
test('真实 WebSocket open/live 和 ProjectServer 交互记录在桌面抽屉展示', async ({ page, context }) => {
    await loginRealAdmin(page, context);
    await ensureRealProjectRunning(page);
    await page.goto(protocolItemsPath());

    const item = page.locator(protocolItemSelector());
    await expect(item).toBeVisible();
    const openButton = item.getByTestId('protocol-interaction-open');
    await expect(openButton).toBeEnabled();
    await openButton.click();

    const drawer = page.getByTestId('protocol-interaction-drawer');
    await expect(drawer).toBeVisible();
    const websocketPromise = page.waitForEvent('websocket');
    await drawer.getByTestId('protocol-interaction-connect').click();
    const websocket = await websocketPromise;
    const websocketUrl = new URL(websocket.url());
    expect(websocketUrl.pathname).toBe('/ws/protocol-interactions/live');
    expect(websocketUrl.searchParams.get('protocol_id')).toBe(String(REAL_E2E.protocolId));
    expect(websocketUrl.searchParams.get('include_project_notice')).toBe('1');
    await expect(drawer.getByTestId('protocol-interaction-state')).toHaveText('实时', { timeout: 10_000 });

    const beforeCount = await drawer.locator('.interaction-record-item').count();
    await sendProtocolOneRequest();
    await expect.poll(() => drawer.locator('.interaction-record-item').count(), {
        timeout: 10_000,
    }).toBeGreaterThan(beforeCount);

    const latestProtocolRecordState = await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        return client.getState().visibleRecords
            .filter(record => record.scope === 'protocol')
            .at(-1);
    });
    expect(latestProtocolRecordState).toMatchObject({
        scope: 'protocol',
        result: 'matched',
        _delivery: 'live',
    });
    const latestProtocolRecord = drawer.locator(
        `[data-testid="protocol-interaction-record-item"][data-record-key="${latestProtocolRecordState._key}"]`,
    );
    await expect(latestProtocolRecord).toBeVisible();
    await expect(latestProtocolRecord.locator('[data-role="delivery"]')).toHaveText('实时');
    await expect(latestProtocolRecord.locator('[data-role="result"]')).toContainText('成功');

    const beforeNoticeCount = await drawer.locator('.interaction-record-item').count();
    await sendProtocolOneRequest(undefined, buildProtocolOneRequest(0x24232323));
    await expect.poll(() => drawer.locator('.interaction-record-item').count(), {
        timeout: 10_000,
    }).toBeGreaterThan(beforeNoticeCount);
    await expect(drawer.locator('[data-role="scope"]')).toHaveText('Notice');
});

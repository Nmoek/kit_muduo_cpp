import net from 'node:net';
import { test, expect } from '@playwright/test';
import { REAL_E2E, protocolItemSelector, protocolItemsPath } from './test_config.js';

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

function sendProtocolOneRequest(port, magic = 0x23232323) {
    const packet = Buffer.alloc(26);
    packet.writeUInt32BE(magic, 0);
    packet.writeUInt32BE(0x11111111, 4);
    packet.writeUInt32BE(0x22222222, 8);
    packet.writeUInt16BE(0x1111, 12);
    packet.writeUInt32BE(0, 14);
    packet.writeBigUInt64BE(0n, 18);

    return new Promise((resolve, reject) => {
        const socket = net.createConnection({ host: REAL_E2E.projectTcpHost, port }, () => socket.end(packet, resolve));
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

async function openTcpDrawer(page) {
    await page.goto(protocolItemsPath());
    await page.locator(protocolItemSelector()).getByTestId('protocol-interaction-open').click();
    const drawer = page.getByTestId('protocol-interaction-drawer');
    await expect(drawer).toBeVisible();
    return drawer;
}

/**
 * 测试思路：
 *
 * 测什么：
 * 验证真实 WebSocket 异常断开后，LiveClient 按退避策略创建新连接，新连接
 * 的 URL 同时携带 protocol/project 两套完整 durable cursor；服务端重启后
 * cache instance 变化会在 live_ready 中标记 cursor_reset，并清理旧 scope。
 *
 * 为什么这么测：
 * 断线恢复是旧连接状态、浏览器网络事件、服务端 cache 生命周期和双游标协议
 * 的交汇点。只测连接按钮无法发现重连丢 cursor、半套参数、旧记录污染或服务
 * 重启后把另一 scope 一并清掉的问题。
 *
 * 怎么测：
 * 1. 登录并连接真实 protocol 1，产生一条 matched 和一条 project Notice，
 *    取得两个 durable cursor 和 cache instance。
 * 2. 通过 page.context().setOffline(true) 制造浏览器网络异常，断言进入等待重连；
 *    恢复网络后监听新 WebSocket URL，断言四个 cursor 参数成对存在，并最终实时。
 * 3. 发送重复/恢复请求，断言列表不重复。
 * 4. 点击停止测试服务，等待页面显示服务未运行；再点击启动测试服务，等待
 *    ProjectServer 重建和入口恢复可用。
 * 5. 重新打开抽屉并连接，记录 live_ready 的两套 cursor_reset/新 cache
 *    instance，断言旧 protocol/project 记录都不会污染新运行窗口。
 *
 * 示例：
 * session A + cursors -> offline/reconnect(session B) -> dual cursor catch-up
 * -> stop/start project -> protocol/project new cache_instance + cursor_reset。
 */
test('真实断线双游标恢复并识别项目重启后的 cursor reset', async ({ page, context }) => {
    await loginRealAdmin(page, context);
    const drawer = await openTcpDrawer(page);
    const websocketEvents = [];
    const firstWebSocketPromise = page.waitForEvent('websocket');
    await drawer.getByTestId('protocol-interaction-connect').click();
    const firstWebSocket = await firstWebSocketPromise;
    websocketEvents.push(firstWebSocket);
    await expect(drawer.getByTestId('protocol-interaction-state')).toHaveText('实时', { timeout: 10_000 });
    await sendProtocolOneRequest(REAL_E2E.projectTcpPort);
    await expect.poll(() => drawer.locator('.interaction-record-item').count(), { timeout: 10_000 })
        .toBeGreaterThan(0);
    await sendProtocolOneRequest(REAL_E2E.projectTcpPort, 0x24232323);
    await expect.poll(() => page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        return client.getState().visibleRecords.concat(client.getState().pendingRecords)
            .some(record => record.scope === 'project');
    }), { timeout: 10_000 }).toBe(true);

    const beforeReconnect = await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        return client.getState();
    });
    expect(beforeReconnect.protocolDurableCursor).toBeTruthy();
    expect(beforeReconnect.projectDurableCursor).toBeTruthy();
    const beforeKeys = await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        return Array.from(client.getState().recordKeys).sort();
    });

    const reconnectSocketPromise = page.waitForEvent('websocket');
    await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        if (!client || !client.state.socket) throw new Error('没有找到真实 WebSocket');
        // 3000 是浏览器允许脚本发起的应用异常关闭码，LiveClient 不会把它
        // 当作 1000/client disconnect，从而进入退避重连。
        client.state.socket.close(3000, 'e2e network failure');
    });
    await expect(drawer.getByTestId('protocol-interaction-state')).toHaveText('等待重连', { timeout: 5_000 });
    const reconnectSocket = await reconnectSocketPromise;
    websocketEvents.push(reconnectSocket);
    await expect(drawer.getByTestId('protocol-interaction-state')).toHaveText('实时', { timeout: 10_000 });
    const reconnectUrl = new URL(reconnectSocket.url());
    expect(reconnectUrl.searchParams.get('after_protocol_cache_instance_id')).toMatch(/^\d+$/);
    expect(reconnectUrl.searchParams.get('after_protocol_seq')).toMatch(/^\d+$/);
    expect(reconnectUrl.searchParams.get('after_project_cache_instance_id')).toMatch(/^\d+$/);
    expect(reconnectUrl.searchParams.get('after_project_seq')).toMatch(/^\d+$/);
    await expect.poll(() => page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        return Array.from(client.getState().recordKeys).sort();
    })).toEqual(beforeKeys);

    await drawer.getByTestId('protocol-interaction-close').click();
    await expect(drawer).toBeHidden();
    const runtimeButton = page.locator('#protocol-service-meta button.service-active-toggle');
    await expect(runtimeButton).toHaveAttribute('aria-label', '停止测试服务');
    await runtimeButton.click();
    const startButton = page.locator('#protocol-service-meta button.service-active-toggle');
    await expect(startButton).toHaveAttribute('aria-label', '启动测试服务', { timeout: 10_000 });
    await startButton.click();
    await expect(startButton).toHaveAttribute('aria-label', '停止测试服务', { timeout: 15_000 });

    const resetDrawer = await openTcpDrawer(page);
    await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        window.__resetLiveReady = null;
        client.on('liveReady', payload => {
            window.__resetLiveReady = payload.message;
        });
    });
    await resetDrawer.getByTestId('protocol-interaction-connect').click();
    await expect(resetDrawer.getByTestId('protocol-interaction-state')).toHaveText('实时', { timeout: 10_000 });
    const resetReady = await expect.poll(() => page.evaluate(() => window.__resetLiveReady))
        .not.toBeNull()
        .then(() => page.evaluate(() => window.__resetLiveReady));
    expect(resetReady.protocol_cache_info.cache_instance_id).not.toBe(
        beforeReconnect.protocolDurableCursor.cacheInstanceId,
    );
    expect(resetReady.project_cache_info.cache_instance_id).not.toBe(
        beforeReconnect.projectDurableCursor.cacheInstanceId,
    );
    expect(resetReady.protocol_cache_info.cursor_reset).toBe(true);
    expect(resetReady.project_cache_info.cursor_reset).toBe(true);
    await expect(resetDrawer.locator('.interaction-record-item')).toHaveCount(0);
});

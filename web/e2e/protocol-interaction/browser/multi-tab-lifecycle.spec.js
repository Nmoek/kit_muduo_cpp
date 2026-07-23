import { test, expect } from '@playwright/test';

const PROTOCOL_ITEMS_URL = '/html/protocol_items.html?apiMode=mock&projectId=1';

async function loginAdmin(page, context) {
    await context.clearCookies();
    await page.goto('/html/login.html?apiMode=mock&returnTo=%2Fhtml%2Fprotocol_items.html%3FapiMode%3Dmock%26projectId%3D1');
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill('admin');
    await page.getByLabel('密码').fill('admin123');
    await Promise.all([
        page.waitForURL(/\/html\/protocol_items\.html\?/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
}

async function openConnectedDrawer(page) {
    if (!page.url().includes('/html/protocol_items.html')) {
        await page.goto(PROTOCOL_ITEMS_URL);
    }
    const openButton = page.locator('.protocol-item[data-protocol-id="1"]')
        .getByTestId('protocol-interaction-open');
    await expect(openButton).toBeEnabled();
    await openButton.click();
    const drawer = page.getByTestId('protocol-interaction-drawer');
    await expect(drawer).toBeVisible();
    await drawer.getByTestId('protocol-interaction-connect').click();
    await expect(drawer.getByTestId('protocol-interaction-state')).toHaveText('实时');
    await expect.poll(() => drawer.getByTestId('protocol-interaction-record-item').count())
        .toBeGreaterThanOrEqual(4);
    await page.evaluate(async () => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        await client.flushPersistence();
    });
    return drawer;
}

async function readWorkspaceIdentity(page) {
    return page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const marker = JSON.parse(window.sessionStorage.getItem('kit_protocol_interaction_restore_marker'));
        return {
            clientCount: window.KitProxy.protocolInteractionLive.clients.size,
            connectionState: client.getState().connectionState,
            snapshotKey: client.getPersistenceState().snapshotKey,
            markerSnapshotKey: marker && marker.snapshotKey,
            markerTabSessionId: marker && marker.tabSessionId,
            tabSessionId: window.sessionStorage.getItem('kit_protocol_interaction_tab_session_id'),
        };
    });
}

/**
 * 测试思路：
 *
 * 测什么：
 * 验证同一 BrowserContext 的两个桌面标签页使用同一登录 Cookie
 * 打开同一协议项时，LiveClient、sessionStorage marker、tabSessionId、
 * IndexedDB snapshotKey、记录列表和选中状态仍按标签页隔离。
 *
 * 为什么这么测：
 * Cookie 和 IndexedDB 在同一 Context 内共享，而 sessionStorage 应按页面隔离。
 * 只测单页无法发现 snapshot key 碰撞、一页注入记录污染另一页或
 * 选中详情被跨页覆盖。
 *
 * 怎么测：
 * 1. 在 Page A 登录，再创建共享 Cookie 的 Page B。
 * 2. 两页都打开 project 1/protocol 1 并连接到 active。
 * 3. 断言两页各只有一个 client，tabSessionId/snapshotKey 不同且 marker 自洽。
 * 4. 在 B 选中一条记录，只向 A 注入 seq=9001，断言 B 没有该 key且选中不变。
 *
 * 示例：
 * Page A -> Client A -> tab A -> snapshot A
 * Page B -> Client B -> tab B -> snapshot B。
 */
test('同一用户的两个标签页保持 client 和 workspace 隔离', async ({ page, context }) => {
    await loginAdmin(page, context);
    const pageB = await context.newPage();
    const drawerA = await openConnectedDrawer(page);
    const drawerB = await openConnectedDrawer(pageB);

    const identityA = await readWorkspaceIdentity(page);
    const identityB = await readWorkspaceIdentity(pageB);
    expect(identityA).toMatchObject({
        clientCount: 1,
        connectionState: 'active',
        markerSnapshotKey: identityA.snapshotKey,
        markerTabSessionId: identityA.tabSessionId,
    });
    expect(identityB).toMatchObject({
        clientCount: 1,
        connectionState: 'active',
        markerSnapshotKey: identityB.snapshotKey,
        markerTabSessionId: identityB.tabSessionId,
    });
    expect(identityA.tabSessionId).not.toBe(identityB.tabSessionId);
    expect(identityA.snapshotKey).not.toBe(identityB.snapshotKey);

    const selectedB = await pageB.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const record = client.getState().visibleRecords[0];
        if (!record || !client.selectRecord(record._key)) throw new Error('标签页 B 无法选中基准记录');
        return record._key;
    });
    const injectedKey = await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const source = client.getState().visibleRecords.find(record => record.scope === 'protocol');
        const record = JSON.parse(JSON.stringify(source));
        record.seq = 9001;
        record.time_ms = Date.now();
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record }));
        return `protocol:${record.cache_instance_id}:${record.seq}`;
    });

    await expect.poll(() => page.evaluate(key => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        return client.getState().recordKeys.has(key);
    }, injectedKey)).toBe(true);
    expect(await pageB.evaluate(key => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        const state = client.getState();
        return { hasInjected: state.recordKeys.has(key), selected: state.selectedRecordKey };
    }, injectedKey)).toEqual({ hasInjected: false, selected: selectedB });
    await expect(drawerA).toBeVisible();
    await expect(drawerB).toBeVisible();
});

/**
 * 测试思路：
 *
 * 测什么：
 * 验证关闭标签页 A 只销毁 A 的 client，不会让标签页 B 断线或进入
 * 重连；B 刷新后仍使用自己的 marker/snapshot 恢复唯一抽屉和 client。
 *
 * 为什么这么测：
 * 页面卸载、Mock transport 定时器、全局 client 集合和 IndexedDB 都参与
 * 关闭/恢复链路。Vitest 的单 client destroy 无法证明真实标签页关闭不会
 * 误清理另一页的状态。
 *
 * 怎么测：
 * 1. 两页连接同一协议项并等待持久化完成。
 * 2. 记录 B 的 tabSessionId/snapshotKey 后关闭 A。
 * 3. 等待超过首个重连退避窗口，断言 B 仍为 active。
 * 4. 刷新 B，断言恢复后只有一个抽屉、一个 client，且身份键不变。
 *
 * 示例：
 * close Page A -> destroy Client A -> Page B stays active -> reload B -> restore snapshot B。
 */
test('关闭一个标签页不影响另一页且剩余页可刷新恢复', async ({ page, context }) => {
    await loginAdmin(page, context);
    const pageB = await context.newPage();
    await openConnectedDrawer(page);
    await openConnectedDrawer(pageB);
    const before = await readWorkspaceIdentity(pageB);

    await page.close();
    await pageB.waitForTimeout(1_500);
    await expect(pageB.getByTestId('protocol-interaction-state')).toHaveText('实时');
    expect(await pageB.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        return { count: window.KitProxy.protocolInteractionLive.clients.size, state: client.getState().connectionState };
    })).toEqual({ count: 1, state: 'active' });

    await pageB.reload();
    const restoredDrawer = pageB.getByTestId('protocol-interaction-drawer');
    await expect(restoredDrawer).toBeVisible();
    await expect(restoredDrawer.getByTestId('protocol-interaction-state')).toHaveText('实时');
    await expect(pageB.getByTestId('protocol-interaction-drawer')).toHaveCount(1);
    const after = await readWorkspaceIdentity(pageB);
    expect(after).toMatchObject({
        clientCount: 1,
        connectionState: 'active',
        tabSessionId: before.tabSessionId,
        snapshotKey: before.snapshotKey,
        markerSnapshotKey: before.snapshotKey,
    });
});

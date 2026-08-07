import { test, expect } from '@playwright/test';

async function loginAsAdmin(page, context) {
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

async function openConnectedDrawer(page, context) {
    await loginAsAdmin(page, context);
    await page.locator('.protocol-item[data-protocol-id="1"] .protocol-interaction-btn').click();
    const drawer = page.locator('.protocol-interaction-drawer');
    await expect(drawer).toBeVisible();
    await drawer.locator('[data-action="connect"]').click();
    await expect(drawer.locator('[data-role="status"]')).toHaveText('实时');
    // 该组用例验证持久化降级后的内存状态。记录卡片带有入场动画，末尾记录可能
    // 正在等待合并，必须同时计入 visibleRecords 和 pendingRecords。
    await expect.poll(async () => page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        if (!client) return 0;
        const state = client.getState();
        return state.visibleRecords.length + state.pendingRecords.length;
    }))
        .toBeGreaterThanOrEqual(4);
    await expect(drawer.locator('.interaction-record-item')).not.toHaveCount(0);
    return drawer;
}

/**
 * 测试思路：
 *
 * 测什么：
 * 验证浏览器没有 IndexedDB 时，实时抽屉仍可以建立 Mock transport、接收
 * live_ready 和 interaction，并把记录展示到页面；同时页面把持久化状态标记
 * 为 disabled，而不是把实时功能判定为连接失败。
 *
 * 为什么这么测：
 * IndexedDB 是恢复能力，不是实时查看的必要依赖。隐私模式、浏览器策略或
 * 环境权限都可能让 IndexedDB 不可用。JSDOM 中的内存对象不能证明真实页面
 * 初始化脚本在 IndexedDB 缺失时仍能继续执行，所以这里在 Chromium 导航前
 * 禁用原生 IndexedDB，再执行完整的抽屉流程。
 *
 * 怎么测：
 * 1. 在页面初始化前把 window.indexedDB 设为 undefined。
 * 2. 进入 Mock 协议项页并打开 protocol 1 抽屉。
 * 3. 点击连接，等待 Mock transport 发送 live_ready 和实时记录。
 * 4. 断言状态为“实时”、记录列表有内容。
 * 5. 读取 LiveClient 状态，断言持久化状态为 disabled，并存在降级告警。
 *
 * 示例：
 * indexedDB unavailable -> connect -> active + records visible + persistence disabled。
 */
test('IndexedDB 不可用时实时内存流程仍可用', async ({ page, context }) => {
    await page.addInitScript(() => {
        Object.defineProperty(window, 'indexedDB', {
            configurable: true,
            value: undefined,
        });
    });

    const drawer = await openConnectedDrawer(page, context);
    const clientState = await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        return client.getState();
    });

    expect(clientState.persistenceStatus).toBe('disabled');
    expect(clientState.visibleRecords.length + clientState.pendingRecords.length)
        .toBeGreaterThanOrEqual(4);
    await expect(drawer.locator('[data-role="warning"]')).toContainText('实时查看仍可继续');
});

/**
 * 测试思路：
 *
 * 测什么：
 * 验证 IndexedDB 中存在 schema version 不兼容的损坏快照时，页面不会把它
 * 当成有效工作区恢复，也不会因 restore marker 反复进入错误恢复循环；坏快照
 * 会被删除，协议项列表和实时页面仍能正常使用��
 *
 * 为什么这么测：
 * 数据库升级、开发阶段结构变化或手工损坏都可能留下旧快照。若恢复流程只
 * 判断 snapshot 存在而不验证 schema 和 snapshotKey，页面会恢复出不完整状态，
 * 甚至每次刷新都重复失败。真实 Chromium 才能验证 IndexedDB 写入、reload 后
 * 新脚本读取和异步清理的组合时序。
 *
 * 怎么测：
 * 1. 登录协议项页，按生产逻辑创建 protocol 1 的 workspace identity。
 * 2. 使用真实 IndexedDB adapter 写入 schemaVersion=999 的快照，并写入合法 marker。
 * 3. 关闭 adapter 后执行 page.reload()，等待协议列表完成。
 * 4. 通过真实 IndexedDB adapter 查询同一个 snapshotKey，断言最终快照已经是
 *    当前 schema，而不是继续保留 schema=999 的损坏数据；同时检查 client 处于
 *    degraded 状态。
 * 5. 断言页面仍可打开抽屉并建立实时连接，证明降级没有阻塞业务页面。
 *
 * 示例：
 * invalid snapshot -> reload -> reject/replace with current schema -> page usable -> live records visible。
 */
test('schema 不兼容快照被清理且页面仍可继续实时查看', async ({ page, context }) => {
    await loginAsAdmin(page, context);
    await page.evaluate(async () => {
        const persistence = window.KitProxy.protocolInteractionPersistence;
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: window.KitProxy.config.apiBaseUrl,
            user: window.KitProxy.auth.getCurrentUser(),
            projectId: 1,
            protocolId: 1,
        });
        const adapter = persistence.createIndexedDbPersistenceAdapter();
        const result = await adapter.saveSnapshot({
            ...identity,
            snapshotKey: identity.snapshotKey,
            schemaVersion: 999,
            expiresAt: Date.now() + 60 * 60 * 1000,
            visibleKeys: [],
            protocolPendingKeys: [],
            noticePendingKeys: [],
            reflowKeys: [],
            readRecordKeys: [],
            uiState: { drawerOpen: true },
            connectionIntent: { desiredConnected: true },
        });
        if (result.status !== 'stored') throw new Error(`无法写入损坏快照: ${result.status}`);
        if (!persistence.writeRestoreMarker(identity, { drawerOpen: true })) {
            throw new Error('无法写入恢复 marker');
        }
        await adapter.close();
    });

    await page.reload();
    await expect(page.locator('.protocol-item[data-protocol-id="1"]')).toBeVisible();
    const restoredDrawer = page.locator('.protocol-interaction-drawer');
    await expect(restoredDrawer).toBeVisible();

    await expect.poll(async () => page.evaluate(async () => {
        const persistence = window.KitProxy.protocolInteractionPersistence;
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: window.KitProxy.config.apiBaseUrl,
            user: window.KitProxy.auth.getCurrentUser(),
            projectId: 1,
            protocolId: 1,
        });
        const adapter = persistence.createIndexedDbPersistenceAdapter();
        const result = await adapter.loadWorkspace(identity.snapshotKey);
        await adapter.close();
        return {
            status: result.status,
            schemaVersion: result.workspace && result.workspace.snapshot
                ? result.workspace.snapshot.schemaVersion
                : null,
        };
    })).toEqual({ status: 'hit', schemaVersion: 1 });

    const degradedState = await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        return client.getState();
    });
    expect(degradedState.persistenceStatus).toBe('degraded');
    await expect(restoredDrawer.locator('[data-role="warning"]')).toContainText('实时查看仍可继续');

    await restoredDrawer.locator('[data-action="connect"]').click();
    await expect(restoredDrawer.locator('[data-role="status"]')).toHaveText('实时');
    const drawer = restoredDrawer;
    await expect(drawer.locator('.interaction-record-item')).not.toHaveCount(0);
});

/**
 * 测试思路：
 *
 * 测什么：
 * 验证持久化写入返回 QuotaExceededError 时，LiveClient 会进入 degraded 状态
 * 并显示恢复不可用告警，但不会断开实时连接或丢弃已经收到的内存记录。
 *
 * 为什么这么测：
 * 浏览器存储配额与用户数据量、隐私模式和磁盘空间有关，真实触发配额错误
 * 不稳定，不能作为每次 CI 的确定条件。本用例在真实 Chromium 页面中注入
 * 与 IndexedDB 相同错误语义的 adapter，固定复现前端 quota recovery、重试和
 * 降级告警逻辑；真正的 IndexedDB 可用性已由前一个用例覆盖。
 *
 * 怎么测：
 * 1. 登录协议项页后替换 createPersistenceAdapter，所有持久化操作返回
 *    name=QuotaExceededError 的错误。
 * 2. 打开抽屉并连接 Mock transport。
 * 3. 等待实时记录进入列表。
 * 4. 断言连接仍为“实时”、记录仍可见、持久化状态为 degraded。
 * 5. 断言页面出现“实时查看仍可继续”的告警。
 *
 * 示例：
 * putRecordBundle quota error -> purge/retry -> degraded + active + records visible。
 */
test('持久化配额错误时实时连接和内存记录不受阻塞', async ({ page, context }) => {
    await loginAsAdmin(page, context);
    await page.evaluate(() => {
        const persistence = window.KitProxy.protocolInteractionPersistence;
        persistence.createPersistenceAdapter = () => {
            const quotaError = new Error('浏览器存储空间不足');
            quotaError.name = 'QuotaExceededError';
            return persistence.createMemoryPersistenceAdapter({ failure: () => quotaError });
        };
    });

    const item = page.locator('.protocol-item[data-protocol-id="1"] .protocol-interaction-btn');
    await item.click();
    const drawer = page.locator('.protocol-interaction-drawer');
    await expect(drawer).toBeVisible();
    await drawer.locator('[data-action="connect"]').click();
    await expect(drawer.locator('[data-role="status"]')).toHaveText('实时');
    await expect.poll(async () => page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        if (!client) return 0;
        const state = client.getState();
        return state.visibleRecords.length + state.pendingRecords.length;
    }))
        .toBeGreaterThanOrEqual(4);
    await expect(drawer.locator('.interaction-record-item')).not.toHaveCount(0);

    const clientState = await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        return client.getState();
    });
    expect(clientState.persistenceStatus).toBe('degraded');
    expect(clientState.visibleRecords.length + clientState.pendingRecords.length)
        .toBeGreaterThanOrEqual(4);
    await expect(drawer.locator('[data-role="warning"]')).toContainText('实时查看仍可继续');
});

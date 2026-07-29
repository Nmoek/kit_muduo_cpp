import { test, expect } from '@playwright/test';
import { REAL_E2E, protocolItemSelector, protocolItemsPath } from '../../protocol-interaction/real/test_config.js';

async function loginReal(page, context, role = 'admin') {
    await context.clearCookies();
    await page.goto('/html/login.html');
    if (role === 'admin') {
        await page.getByRole('button', { name: '切换为管理员登录' }).click();
        await page.getByLabel('密码').fill(REAL_E2E.adminPassword);
    }
    await page.getByLabel('note').fill(role === 'admin' ? REAL_E2E.adminNote : REAL_E2E.normalNote);
    await Promise.all([
        page.waitForURL(/\/html\/main\.html/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
}

/**
 * 测试思路：
 *
 * 测什么：真实隔离后端指定项目的协议项列表、服务上下文和核心入口。
 * 为什么这么测：Mock 已覆盖列表操作，Real 只需证明真实接口字段能驱动列表页，并保留新增入口。
 * 怎么测：真实管理员登录后访问配置项目，检查服务标题、指定协议项和新增按钮。
 * 示例：真实登录 -> protocol_items.html?projectId=1 -> protocolId=1 卡片可见。
 */
test('真实后端协议项列表读取冒烟', async ({ page, context }) => {
    await context.clearCookies();
    await page.goto('/html/login.html');
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill(REAL_E2E.adminNote);
    await page.getByLabel('密码').fill(REAL_E2E.adminPassword);
    await Promise.all([
        page.waitForURL(/\/html\/main\.html/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);

    await page.goto(protocolItemsPath());
    await expect(page.locator('#protocol-items-title')).toBeVisible();
    await expect(page.locator(protocolItemSelector())).toBeVisible();
    await expect(page.locator('#protocol-items-title')).toContainText('协议项管理：');
    await expect(page.locator(`${protocolItemSelector()} .protocol-name`)).toHaveText(/\S+/);
    await expect(page.locator(`${protocolItemSelector()} .protocol-tag`)).toHaveText(/\S+/);
    await expect(page.locator('#add-protocol-item')).toBeEnabled();
});

/**
 * 测试思路：真实后端临时软删除一个协议项后，默认列表分页仍应保留它，并且跨页 ID 不重复；status=1/0
 * 两种显式查询还要分别只返回有效项和软删除项，最后恢复隔离库状态。
 * 示例：projectId=2 的协议项 4 -> 删除 -> pageSize=1 逐页收集 -> 已删除项出现一次 -> 恢复协议项 4。
 */
test('真实后端软删除协议项统一分页且可按状态筛选', async ({ page, context }) => {
    await context.clearCookies();
    await page.goto('/html/login.html');
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill(REAL_E2E.adminNote);
    await page.getByLabel('密码').fill(REAL_E2E.adminPassword);
    await Promise.all([
        page.waitForURL(/\/html\/main\.html/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);

    const projectId = REAL_E2E.attachmentProjectId;
    await page.goto(protocolItemsPath(projectId));
    await expect(page.locator('#protocol-items-title')).toContainText('协议项管理：');

    const result = await page.evaluate(async ({ projectId, preferredProtocolId }) => {
        const api = window.KitProxy.api;
        const before = await api.getProtocolList(projectId, 0, 100);
        const target = before.items.find(protocol => Number(protocol.id) === Number(preferredProtocolId))
            || before.items.find(protocol => Number(protocol.status) === 1);
        if (!target || Number(target.status) !== 1) {
            throw new Error('Real fixture must contain a valid protocol item to soft-delete');
        }

        const project = (await api.getProject(projectId))[0];
        const wasRunning = Number(project && project.runtime_state) === 1;
        if (wasRunning) {
            await api.setProjectRuntimeState(projectId, false);
        }
        try {
            await api.deleteProtocol(target.id, projectId);
            try {
                const allPage = await api.getProtocolList(projectId, 0, 100);
                const deletedPage = await api.getProtocolList(projectId, 0, 100, { status: 0 });
                const validPage = await api.getProtocolList(projectId, 0, 100, { status: 1 });
                const pageState = window.KitProxy.protocolItemsPage.pageState;
                pageState.pageSize = 1;
                const pagedIds = [];
                let deletedDomCount = 0;
                const pageCount = allPage.total;
                for (let page = 1; page <= pageCount; page += 1) {
                    await window.KitProxy.protocolItemsPage.loadProtocolItems(page);
                    pagedIds.push(...Array.from(document.querySelectorAll('.protocol-item'))
                        .map(item => Number(item.dataset.protocolId)));
                    deletedDomCount += document.querySelectorAll('.protocol-item.is-inactive').length;
                }

                return {
                    targetId: target.id,
                    total: allPage.total,
                    deletedCount: deletedPage.items.length,
                    validCount: validPage.items.length,
                    deletedOnly: deletedPage.items.every(protocol => Number(protocol.status) === 2),
                    validOnly: validPage.items.every(protocol => Number(protocol.status) === 1),
                    pagedIds,
                    uniquePagedIds: new Set(pagedIds).size,
                    deletedDomCount,
                    finalHasMore: pageState.hasMore,
                };
            } finally {
                await api.restoreProtocol(target.id);
            }
        } finally {
            if (wasRunning) {
                await api.setProjectRuntimeState(projectId, true);
            }
        }
    }, { projectId, preferredProtocolId: REAL_E2E.attachmentProtocolId });

    expect(result.deletedCount).toBe(1);
    expect(result.validCount + result.deletedCount).toBe(result.total);
    expect(result.deletedOnly).toBe(true);
    expect(result.validOnly).toBe(true);
    expect(result.pagedIds).toHaveLength(result.total);
    expect(result.uniquePagedIds).toBe(result.total);
    expect(result.pagedIds).toContain(result.targetId);
    expect(result.deletedDomCount).toBe(1);
    expect(result.finalHasMore).toBe(false);
});

/**
 * 测试思路：
 * 测什么：验证普通用户默认协议列表固定查询 status=kValid，且不能用 status=0 读取已删除项。
 * 为什么这么测：ProtocolListReq 的 status 已改为 optional；默认值和管理员全部/有效/已删除是后端
 * 权限契约，不能只测管理员页面渲染。
 * 怎么测：普通用户先删除自己项目中的有效协议，查询默认列表和显式 status=0，最后重新登录管理员
 * 恢复协议并确认它重新出现在有效列表。
 * 示例：normal -> delete protocol -> default excludes -> status=0 returns 403 -> admin restore。
 */
test('真实后端普通用户协议列表默认只返回有效项', async ({ page, context }) => {
    test.setTimeout(60_000);
    await loginReal(page, context, 'normal');
    const projectId = REAL_E2E.normalProjectId;
    const protocolId = REAL_E2E.normalProtocolId;

    let deleted = false;
    try {
        const result = await page.evaluate(async ({ projectId, protocolId }) => {
            const api = window.KitProxy.api;
            const before = await api.getProtocolList(projectId, 0, 100);
            const target = before.items.find(protocol => Number(protocol.id) === Number(protocolId));
            if (!target || Number(target.status) !== 1) {
                throw new Error('Real fixture must contain an active protocol owned by the normal user');
            }
            await api.deleteProtocol(protocolId, projectId);
            const defaultPage = await api.getProtocolList(projectId, 0, 100);
            const deletedResponse = await fetch('/protocols/list', {
                method: 'POST',
                credentials: 'same-origin',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ project_id: projectId, offset: 0, limit: 100, status: 0 }),
            });
            return {
                defaultHasDeleted: defaultPage.items.some(protocol => Number(protocol.id) === Number(protocolId)),
                defaultStatuses: defaultPage.items.map(protocol => Number(protocol.status)),
                deletedStatus: deletedResponse.status,
                deletedBody: await deletedResponse.json(),
            };
        }, { projectId, protocolId });
        deleted = true;

        expect(result.defaultHasDeleted).toBe(false);
        expect(result.defaultStatuses.every(status => status === 1)).toBe(true);
        expect(result.deletedStatus).toBe(403);
        expect(result.deletedBody.code).toBe(-403);
    } finally {
        await loginReal(page, context, 'admin');
        if (deleted) {
            await page.evaluate(async id => window.KitProxy.api.restoreProtocol(id), protocolId);
            const restored = await page.evaluate(async ({ projectId, protocolId }) => {
                const pageData = await window.KitProxy.api.getProtocolList(projectId, 0, 100, { status: 1 });
                return pageData.items.some(protocol => Number(protocol.id) === Number(protocolId));
            }, { projectId, protocolId });
            expect(restored).toBe(true);
        }
    }
});

/**
 * 测试思路：
 * 测什么：验证协议名称、请求 cfg、请求 Body 的局部更新，以及协议下线/上线都能经过真实运行时并回读。
 * 为什么这么测：这些接口同时修改数据库和 ProjectServer；只验证 HTTP 200 无法发现运行态未同步或回读
 * 字段未持久化。用项目 2 的 HTTP 协议避免依赖 TCP Pattern。
 * 怎么测：保存原始详情，确保项目运行，先下线协议后更新 name/cfg/body，再上线并读取详情；finally 按
 * 原值恢复名称、cfg、Body、协议和项目运行态。
 * 示例：On -> Off -> update cfg/body/name -> On -> read-back -> restore original。
 */
test('真实后端协议详情更新和上线状态完成回读', async ({ page, context }) => {
    test.setTimeout(60_000);
    await loginReal(page, context, 'admin');
    const projectId = REAL_E2E.attachmentProjectId;
    const protocolId = 2;
    const result = await page.evaluate(async ({ projectId, protocolId }) => {
        const api = window.KitProxy.api;
        const projectBefore = (await api.getProject(projectId))[0];
        const protocolBefore = (await api.getProtocolEditDetail(protocolId));
        const requestBodyBefore = await api.getProtocolBody(protocolId, 1);
        const originalRuntime = Number(projectBefore.runtime_state) === 1;
        const originalConfigState = Number(protocolBefore.config_state);
        const originalName = protocolBefore.name;
        const originalCfg = JSON.parse(JSON.stringify(protocolBefore.req_cfg));

        if (!originalRuntime) await api.setProjectRuntimeState(projectId, true);
        try {
            if (originalConfigState === 1) await api.setProtocolRuntime(protocolId, false);
            const updatedPath = `${originalCfg.path || '/api/test'}?real_e2e=1`;
            await api.updateProtocolName(protocolId, `${originalName} real-updated`);
            await api.updateProtocolCfg(protocolId, projectId, 1, { path: updatedPath });
            await api.updateProtocolBody(protocolId, projectId, 1, 'http', 'json', '{"real_e2e":true}');

            const stateAfterOff = Number((await api.getProtocol(protocolId))[0].config_state);
            await api.setProtocolRuntime(protocolId, true);
            const changed = await api.getProtocolEditDetail(protocolId);
            const changedBody = await api.getProtocolBody(protocolId, 1);
            return {
                name: changed.name,
                path: changed.req_cfg.path,
                bodyType: changedBody[0],
                body: new TextDecoder().decode(changedBody[1]),
                stateAfterOff,
                stateAfterOn: Number(changed.config_state),
            };
        } finally {
            try { await api.updateProtocolName(protocolId, originalName); } catch (error) { /* continue restore */ }
            try { await api.updateProtocolCfg(protocolId, projectId, 1, originalCfg); } catch (error) { /* continue restore */ }
            try {
                await api.updateProtocolBody(
                    protocolId,
                    projectId,
                    1,
                    'http',
                    requestBodyBefore[0],
                    requestBodyBefore[1],
                );
            } catch (error) { /* continue restore */ }
            try {
                const current = Number((await api.getProtocol(protocolId))[0].config_state);
                if (originalConfigState === 1 && current !== 1) await api.setProtocolRuntime(protocolId, true);
                if (originalConfigState === 0 && current === 1) await api.setProtocolRuntime(protocolId, false);
            } catch (error) { /* test reports the primary failure */ }
            if (!originalRuntime) {
                try { await api.setProjectRuntimeState(projectId, false); } catch (error) { /* continue restore */ }
            }
        }
    }, { projectId, protocolId });

    expect(result.name).toContain('real-updated');
    expect(result.path).toContain('real_e2e=1');
    expect(result.bodyType).toBe('json');
    expect(result.body).toBe('{"real_e2e":true}');
    expect(result.stateAfterOff).toBe(0);
    expect(result.stateAfterOn).toBe(1);
});

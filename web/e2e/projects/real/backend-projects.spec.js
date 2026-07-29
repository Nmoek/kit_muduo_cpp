import { test, expect } from '@playwright/test';
import { REAL_E2E } from '../../protocol-interaction/real/test_config.js';

async function loginRealAdmin(page, context, returnTo = '/html/main.html') {
    await context.clearCookies();
    await page.goto('/html/login.html');
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill(REAL_E2E.adminNote);
    await page.getByLabel('密码').fill(REAL_E2E.adminPassword);
    await Promise.all([
        page.waitForURL(url => new URL(url).pathname === returnTo),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);
}

/**
 * 测试思路：
 *
 * 测什么：验证隔离 Real C++ 后端登录后能够加载测试服务管理页，服务卡片包含名称、协议、
 * 测试模式、端点和运行状态等核心字段，并且分页容器正常渲染。
 * 为什么这么测：Mock 只能证明前端数据适配，Real 列表还依赖 Cookie、/projects/list 接口、
 * 后端 DTO 字段和真实静态资源；这是服务生命周期冒烟的必要前置。
 * 怎么测：使用现有 REAL_E2E 管理员配置登录 main.html，等待指定项目卡片和至少一张服务卡片，
 * 对卡片的字段结构及分页控件做断言，不创建或删除真实隔离库中的服务。
 * 示例：隔离 C++ 后端 -> admin -> main.html -> service-card-<projectId> 可见。
 */
test('真实后端服务列表和核心卡片字段可见', async ({ page, context }) => {
    await loginRealAdmin(page, context);

    const cards = page.locator('.service-card');
    await expect(cards.first()).toBeVisible();
    expect(await cards.count()).toBeGreaterThan(0);
    await expect(page.locator('#service-pagination')).toBeVisible();

    const projectCard = page.locator(`#service-card-${REAL_E2E.projectId}`);
    await expect(projectCard).toBeVisible();
    await expect(projectCard.locator('.service-title')).not.toBeEmpty();
    await expect(projectCard.locator('.project-protocol-type .field-value')).not.toBeEmpty();
    await expect(projectCard.locator('.project-mode .field-value')).not.toBeEmpty();
    await expect(projectCard.locator('.project-status .status, .project-status .status-deleted')).toHaveCount(1);
    await expect(projectCard.locator('.project-listen-port .field-value, .project-target-ip .field-value')).toHaveCount(1);
});

/**
 * 测试思路：
 *
 * 测什么：验证 Real 项目可以完成最小运行态生命周期：开启项目时状态变为“开启”并得到
 * 非零监听端口，停止后状态变为“未开启”且服务器端点回到未开启。
 * 为什么这么测：运行态更新同时涉及页面按钮、POST /projects/{id}/runtime_state、真实
 * ProjectServer 创建/销毁和返回端口；Mock 的内存状态不能替代这条后端联调链路。
 * 怎么测：登录后定位 REAL_E2E.projectId；如果初始已开启先停止，确认未开启后再点击启动，
 * 确认开启和监听端口，最后停止并确认恢复未开启，保证隔离后端不会留下运行中的测试服务。
 * 示例：开启 -> status=开启/port>0 -> 停止 -> status=未开启/endpoint=未开启。
 */
test('真实后端项目完成停止和启动生命周期冒烟', async ({ page, context }) => {
    await loginRealAdmin(page, context);

    const card = page.locator(`#service-card-${REAL_E2E.projectId}`);
    await expect(card).toBeVisible();
    const toggle = card.locator('.service-active-toggle');
    await expect(toggle).toBeVisible();

    if (await toggle.getAttribute('aria-label') === '停止测试服务') {
        await toggle.click();
        await expect(card.locator('.project-status .status')).toHaveText('未开启', { timeout: 10_000 });
    }

    await expect(toggle).toHaveAttribute('aria-label', '启动测试服务');
    await toggle.click();
    await expect(card.locator('.project-status .status')).toHaveText('开启', { timeout: 10_000 });
    const portText = await card.locator('.project-listen-port .field-value').textContent();
    expect(Number(portText)).toBeGreaterThan(0);
    await expect(toggle).toHaveAttribute('aria-label', '停止测试服务');

    await toggle.click();
    await expect(card.locator('.project-status .status')).toHaveText('未开启', { timeout: 10_000 });
    await expect(card.locator('.project-listen-port .field-value')).toHaveText('未开启');
});

/**
 * 测试思路：真实 Project 列表必须先在后端筛选再分页，不能只对已经取回的第一页做本地过滤。
 * 示例：limit=1 的未筛选第一页是 TCP，后续页出现 HTTP；按 HTTP 查询第一页仍应返回该后续项，且 total 是 HTTP 总数。
 */
test('真实后端 Project 筛选在分页前执行', async ({ page, context }) => {
    await loginRealAdmin(page, context);

    const result = await page.evaluate(async () => {
        const firstPage = await window.KitProxy.api.getProjectList(0, 1);
        const completePage = await window.KitProxy.api.getProjectList(0, 100);
        const firstType = firstPage.items[0] && Number(firstPage.items[0].protocol_type);
        const laterItem = completePage.items.find(project => Number(project.protocol_type) !== firstType);
        if (!laterItem) {
            throw new Error('Real fixture must contain different Project protocol types across pages');
        }

        const filteredFirstPage = await window.KitProxy.api.getProjectList(0, 1, {
            protocol_type: laterItem.protocol_type,
        });
        const filteredCompletePage = await window.KitProxy.api.getProjectList(0, 100, {
            protocol_type: laterItem.protocol_type,
        });
        return {
            firstPageId: firstPage.items[0].id,
            filteredFirstPageId: filteredFirstPage.items[0] && filteredFirstPage.items[0].id,
            filteredType: filteredFirstPage.items[0] && filteredFirstPage.items[0].protocol_type,
            filteredTotal: filteredFirstPage.total,
            filteredCompleteCount: filteredCompletePage.items.length,
        };
    });

    expect(result.filteredFirstPageId).not.toBe(result.firstPageId);
    expect(result.filteredType).toBeGreaterThan(0);
    expect(result.filteredTotal).toBe(result.filteredCompleteCount);
    expect(result.filteredTotal).toBeGreaterThan(0);
});

/**
 * 测试思路：
 * 测什么：验证 Project 新增、详情回读、改名、状态筛选、软删除和管理员恢复均经过真实后端。
 * 为什么这么测：页面列表冒烟无法发现新增请求的枚举/字段契约、删除后分页归属或恢复时运行态没有
 * 一起重置的问题；这些状态会直接影响后续协议管理。
 * 怎么测：创建一个 HTTP 服务器模式项目，回读 ID 后改名，删除后分别查询 status=0/1，最后恢复并
 * 再删除，保证临时项目不会作为有效数据留在隔离库中。
 * 示例：add -> get/name -> delete(status=0) -> restore(status=1) -> cleanup。
 */
test('真实后端 Project 完成新增改名删除恢复生命周期', async ({ page, context }) => {
    test.setTimeout(60_000);
    await loginRealAdmin(page, context);

    const result = await page.evaluate(async () => {
        const api = window.KitProxy.api;
        const name = `Real E2E project ${Date.now()}`;
        const renamed = `${name} renamed`;
        const added = await api.addProject({
            name,
            mode: 1,
            protocol_type: 1,
            target_ip: '',
            pattern_info: {},
        });
        const projectId = Number(added && (added.project_id || added.id));
        if (!Number.isInteger(projectId) || projectId <= 0) {
            throw new Error(`真实后端新增 Project 未返回有效 project_id: ${JSON.stringify(added)}`);
        }

        let deleted = false;
        try {
            const created = (await api.getProject(projectId))[0];
            if (!created || Number(created.id) !== projectId || Number(created.status) !== 1) {
                throw new Error('新增 Project 详情回读不一致');
            }

            await api.updateProjectName(projectId, renamed);
            const renamedProject = (await api.getProject(projectId))[0];
            if (!renamedProject || renamedProject.name !== renamed) {
                throw new Error('Project 改名后详情回读不一致');
            }

            await api.deleteProject(projectId);
            deleted = true;
            const deletedPage = await api.getProjectList(0, 100, { status: 0 });
            const validPageAfterDelete = await api.getProjectList(0, 100, { status: 1 });
            if (!deletedPage.items.some(project => Number(project.id) === projectId)
                || validPageAfterDelete.items.some(project => Number(project.id) === projectId)) {
                throw new Error('Project 删除后的状态筛选不一致');
            }

            await api.restoreProject(projectId);
            const restoredPage = await api.getProjectList(0, 100, { status: 1 });
            const restored = restoredPage.items.find(project => Number(project.id) === projectId);
            if (!restored || Number(restored.status) !== 1 || Number(restored.runtime_state) !== 0) {
                throw new Error('Project 恢复后状态或运行态不一致');
            }

            return { projectId, name: restored.name, status: restored.status };
        } finally {
            // 删除最终临时项目；即使断言失败也尽量不把有效服务留给后续用例。
            if (!deleted) {
                try { await api.deleteProject(projectId); } catch (error) { /* 保留原始断言 */ }
            } else {
                try { await api.deleteProject(projectId); } catch (error) { /* 已删除时幂等清理 */ }
            }
        }
    });

    expect(result.name).toContain('renamed');
    expect(result.status).toBe(1);
});

/**
 * 测试思路：
 * 测什么：验证普通用户只能看到自己的有效 Project，并且不能通过 status/user_id 参数绕过后端权限。
 * 为什么这么测：前端隐藏筛选项不是权限边界；ProjectList 的普通用户分支必须由 C++ 后端强制固定
 * status=kValid 和 user_id=current_user。
 * 怎么测：普通用户查询默认列表，再分别提交管理员可用的已删除筛选和 user_id 筛选，断言业务响应为
 * 403；同时确认返回项目的 user_note 是当前用户。
 * 示例：normal -> /projects/list -> own valid projects；status=0/user_id=1 -> forbidden。
 */
test('真实后端普通用户 Project 查询权限边界', async ({ page, context }) => {
    await context.clearCookies();
    await page.goto('/html/login.html');
    await page.getByLabel('note').fill(REAL_E2E.normalNote);
    await Promise.all([
        page.waitForURL(/\/html\/main\.html/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);

    const result = await page.evaluate(async () => {
        const api = window.KitProxy.api;
        const own = await api.getProjectList(0, 100);
        const statusError = await fetch('/projects/list', {
            method: 'POST',
            credentials: 'same-origin',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ offset: 0, limit: 100, status: 0 }),
        });
        const userError = await fetch('/projects/list', {
            method: 'POST',
            credentials: 'same-origin',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ offset: 0, limit: 100, user_id: 1 }),
        });
        return {
            ownItems: own.items,
            statusStatus: statusError.status,
            statusBody: await statusError.json(),
            userStatus: userError.status,
            userBody: await userError.json(),
        };
    });

    expect(result.ownItems.length).toBeGreaterThan(0);
    expect(result.ownItems.every(project => project.user_note === REAL_E2E.normalNote)).toBe(true);
    expect(result.statusStatus).toBe(403);
    expect(result.statusBody.code).toBe(-403);
    expect(result.userStatus).toBe(403);
    expect(result.userBody.code).toBe(-403);
});

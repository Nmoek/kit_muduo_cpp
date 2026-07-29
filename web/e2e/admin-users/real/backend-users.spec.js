import { test, expect } from '@playwright/test';
import { REAL_E2E } from '../../protocol-interaction/real/test_config.js';

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
 * 测什么：真实后端管理员页面、用户列表、状态筛选和新增入口的最小读取冒烟。
 * 为什么这么测：Mock 已覆盖用户增删改和状态流程，Real 只确认管理员权限、字段契约和列表接口可以渲染。
 * 怎么测：使用隔离后端管理员登录用户页，检查至少一行用户、状态筛选、刷新和新增按钮。
 * 示例：真实登录 -> admin_users.html -> 用户行 -> 状态筛选/新增用户可见。
 */
test('真实后端管理员页面和列表读取冒烟', async ({ page, context }) => {
    await context.clearCookies();
    await page.goto('/html/login.html');
    await page.getByRole('button', { name: '切换为管理员登录' }).click();
    await page.getByLabel('note').fill(REAL_E2E.adminNote);
    await page.getByLabel('密码').fill(REAL_E2E.adminPassword);
    await Promise.all([
        page.waitForURL(/\/html\/main\.html/),
        page.getByRole('button', { name: '登录', exact: true }).click(),
    ]);

    await page.goto('/html/admin_users.html');
    await expect(page.locator('#admin-users-body tr:not(.admin-users-empty-row)').first()).toBeVisible();
    await expect(page.locator('#user-status-filter')).toBeVisible();
    await expect(page.locator('#refresh-users-btn')).toBeVisible();
    await expect(page.locator('#add-user-btn')).toBeVisible();
});

/**
 * 测试思路：
 * 测什么：验证真实用户接口的新增、详情回读、note/角色修改、状态筛选、停用和恢复完整生命周期。
 * 为什么这么测：Mock 内存实现无法发现 UserEditReq 字段、状态字符串或密码规则与 C++ 服务漂移；用户
 * 数据又是 Project 所有者筛选和登录权限的基础。
 * 怎么测：管理员创建普通用户，改成管理员并设置密码，检查 note 候选，再停用并分别查询 disabled/
 * active，恢复后回读 active；最后再次停用临时账号。
 * 示例：normal user -> admin + password -> note candidate -> disabled -> restored -> cleanup。
 */
test('真实后端用户增改停用恢复和 note 候选完整回读', async ({ page, context }) => {
    test.setTimeout(60_000);
    await loginReal(page, context, 'admin');

    const result = await page.evaluate(async () => {
        const api = window.KitProxy.api;
        const suffix = String(Date.now()).slice(-8);
        const note = `realuser${suffix}`;
        const renamed = `realadmin${suffix}`;
        const added = await api.addUser({ note, role: 'normal', password: '' });
        const userId = Number(added && (added.user_id || added.id));
        if (!Number.isInteger(userId) || userId <= 0) {
            throw new Error(`真实后端新增 User 未返回有效 user_id: ${JSON.stringify(added)}`);
        }

        let disabled = false;
        try {
            const created = await api.getUser(userId);
            await api.updateUser(userId, {
                note: renamed,
                role: 'admin',
                status: 'active',
                password: 'RealE2Epass123',
            });
            const updated = await api.getUser(userId);
            const candidates = await api.getProjectNoteCandidates(renamed.slice(0, 8), 10);

            await api.deleteUser(userId);
            disabled = true;
            const disabledUsers = await api.listUsers(0, 200, 'disabled');
            const activeUsers = await api.listUsers(0, 200, 'active');
            await api.restoreUser(userId);
            disabled = false;
            const restored = await api.getUser(userId);

            return {
                created,
                updated,
                candidateIds: candidates.map(candidate => Number(candidate.user_id)),
                disabledIds: disabledUsers.map(user => Number(user.id)),
                activeIds: activeUsers.map(user => Number(user.id)),
                restored,
                userId,
            };
        } finally {
            try {
                if (disabled) await api.restoreUser(userId);
                await api.deleteUser(userId);
            } catch (error) { /* 保留主断言错误 */ }
        }
    });

    expect(result.created).toMatchObject({ role: 'normal', status: 'active' });
    expect(result.updated.note).toMatch(/^realadmin/);
    expect(result.updated.role).toBe('admin');
    expect(result.candidateIds).toContain(result.userId);
    expect(result.disabledIds).toContain(result.userId);
    expect(result.activeIds).not.toContain(result.userId);
    expect(result.restored.status).toBe('active');
});

/**
 * 测试思路：
 * 测什么：验证普通用户直接调用管理员 User API 和 note 候选接口时由真实后端拒绝。
 * 为什么这么测：隐藏“用户管理”菜单和页面守卫只能保护正常导航，不能阻止手工 fetch；服务端必须是
 * 最终权限边界，尤其不能泄露全部用户列表。
 * 怎么测：普通用户登录后直接 POST /users/list 与 /users/note_candidates，断言两者均返回 HTTP 403 和
 * code=-403，不通过 UI 模拟结果。
 * 示例：normal session -> direct admin API -> 403 forbidden。
 */
test('真实后端普通用户不能直调管理员 User API', async ({ page, context }) => {
    await loginReal(page, context, 'normal');

    const result = await page.evaluate(async () => {
        async function post(path, body) {
            const response = await fetch(path, {
                method: 'POST',
                credentials: 'same-origin',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(body),
            });
            const text = await response.text();
            let bodyJson = null;
            try { bodyJson = JSON.parse(text); } catch (error) { /* 保留原始响应以诊断契约错误 */ }
            return { status: response.status, body: bodyJson, text };
        }
        return {
            list: await post('/users/list', { offset: 0, limit: 20, status: 'all' }),
            candidates: await post('/users/note_candidates', { keyword: 'admin', limit: 10 }),
        };
    });

    expect(result.list.status).toBe(403);
    expect(result.list.body && result.list.body.code).toBe(-403);
    expect(result.candidates.status).toBe(403);
    if (result.candidates.body) {
        expect(result.candidates.body.code).toBe(-403);
    } else {
        // 当前后端会将 forbidden JSON 重复拼接；仍锁定权限码，同时保留该协议缺陷的证据。
        expect(result.candidates.text).toContain('"code":-403');
    }
});

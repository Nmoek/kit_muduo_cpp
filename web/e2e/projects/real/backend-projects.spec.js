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

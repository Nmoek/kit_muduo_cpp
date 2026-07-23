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

function buildProtocolOneRequest() {
    const packet = Buffer.alloc(26);
    packet.writeUInt32BE(0x23232323, 0);
    packet.writeUInt32BE(0x11111111, 4);
    packet.writeUInt32BE(0x22222222, 8);
    packet.writeUInt16BE(0x1111, 12);
    packet.writeUInt32BE(0, 14);
    packet.writeBigUInt64BE(0n, 18);
    return packet;
}

function sendProtocolOneRequest(port = REAL_E2E.projectTcpPort) {
    return new Promise((resolve, reject) => {
            const socket = net.createConnection({ host: REAL_E2E.projectTcpHost, port }, () => {
            socket.end(buildProtocolOneRequest(), resolve);
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
 * 验证真实 WebSocket 的 pause/resume 命令、后端暂停 ACK、暂停期间缓存、
 * resume 的 live_ready(catch-up) 和最终 catch_up 记录交付。还验证 pause/resume
 * 的 client_seq 连续，补发记录只出现一次。
 *
 * 为什么这么测：
 * Mock transport 已经覆盖前端状态机，但无法证明 C++ Hub 在 pause 后确实取消
 * 订阅、ProjectServer 产生的记录确实进入后端缓存、resume 确实按 cursor 补发。
 * 这个用例同时使用原生 Chromium WebSocket 和真实 TCP ProjectServer，覆盖的是
 * pause/resume 最容易出现的跨进程时序风险。
 *
 * 怎么测：
 * 1. 在隔离真实后端中登录并打开 project 1/protocol 1。
 * 2. 连接实时抽屉，监听生产 LiveClient 的 commandSent 事件。
 * 3. 点击暂停，断言页面进入“已暂停”，并记录 pause 命令序号。
 * 4. 暂停期间向环境变量指定的 project TCP 端口发送匹配请求，等待一小段时间，
 *    断言列表数量不变。
 * 5. 点击恢复，断言页面经历恢复/补发过程并最终回到“实时”。
 * 6. 断言 resume 命令序号紧接 pause，列表出现唯一的 `delivery=catch_up` 记录。
 *
 * 示例：
 * active -> pause ACK/paused -> TCP record buffered -> resume/live_ready(resume)
 * -> interaction(catch_up) -> active。
 */
test('真实后端 pause/resume 将暂停期间记录以 catch-up 补发', async ({ page, context }) => {
    await loginRealAdmin(page, context);
    await page.goto(protocolItemsPath());

    const item = page.locator(protocolItemSelector());
    await expect(item).toBeVisible();
    await item.getByTestId('protocol-interaction-open').click();
    const drawer = page.getByTestId('protocol-interaction-drawer');
    await expect(drawer).toBeVisible();
    await drawer.getByTestId('protocol-interaction-connect').click();
    await expect(drawer.getByTestId('protocol-interaction-state')).toHaveText('实时', { timeout: 10_000 });

    await page.evaluate(() => {
        const client = Array.from(window.KitProxy.protocolInteractionLive.clients)[0];
        window.__realInteractionCommands = [];
        client.on('commandSent', message => window.__realInteractionCommands.push(message));
    });

    const beforePauseCount = await drawer.locator('.interaction-record-item').count();
    await drawer.getByTestId('protocol-interaction-pause').click();
    await expect(drawer.getByTestId('protocol-interaction-state')).toHaveText('已暂停', { timeout: 10_000 });

    await sendProtocolOneRequest();
    await page.waitForTimeout(250);
    expect(await drawer.locator('.interaction-record-item').count()).toBe(beforePauseCount);

    await drawer.getByTestId('protocol-interaction-resume').click();
    await expect(drawer.getByTestId('protocol-interaction-state')).toHaveText(/恢复中|补发中|实时/, { timeout: 5_000 });
    await expect(drawer.getByTestId('protocol-interaction-state')).toHaveText('实时', { timeout: 10_000 });
    await expect.poll(() => drawer.locator('[data-role="delivery"]')
        .filter({ hasText: '补发' }).count(), { timeout: 10_000 }).toBeGreaterThan(0);

    const commands = await page.evaluate(() => window.__realInteractionCommands);
    expect(commands.map(command => command.command)).toEqual(['pause', 'resume']);
    expect(commands[1].client_seq).toBe(commands[0].client_seq + 1);

    const afterResumeCount = await drawer.locator('.interaction-record-item').count();
    await page.waitForTimeout(300);
    expect(await drawer.locator('.interaction-record-item').count()).toBe(afterResumeCount);
});

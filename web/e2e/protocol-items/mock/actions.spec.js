import { test, expect } from '@playwright/test';
import { loginAsAdmin } from '../../helpers/auth.js';

/**
 * 测试思路：
 *
 * 测什么：协议项删除和管理员恢复。
 * 为什么这么测：协议项软删除状态由列表卡片即时重绘，错误会让管理员无法识别或恢复配置。
 * 怎么测：删除 protocol 1，再在 inactive 卡片上恢复。
 * 示例：HTTP健康检查示例 -> 删除 -> 恢复协议项。
 */
test('协议项删除和管理员恢复', async ({ page, context }) => {
    await loginAsAdmin(page, context, '/html/protocol_items.html?apiMode=mock&projectId=1');
    const item = page.locator('#protocol-item-1');
    await expect(item).toBeVisible();
    page.on('dialog', dialog => dialog.accept());

    await item.locator('.delete-protocol-btn').click();
    await expect(page.locator('#protocol-item-1 .restore-protocol-btn')).toBeVisible();
    await page.locator('#protocol-item-1 .restore-protocol-btn').click();
    await expect(page.locator('#protocol-item-1 .delete-protocol-btn')).toBeVisible();
});

/**
 * 测试思路：
 *
 * 测什么：协议项上线、下线和待重配置状态的按钮语义与导航。
 * 为什么这么测：config_state 决定运行态是否可用；待重配置不能直接上线，必须进入 reconfig 表单。
 * 怎么测：对运行中的 protocol 1 切换已上线/未上线，再打开 project 2 的待重配置 protocol 5，检查进入重配置页。
 * 示例：已上线 -> 下线 -> 未上线 -> 待重配置 -> protocol_item_form.html?mode=reconfig。
 */
test('协议项上线、下线和待重配置状态', async ({ page, context }) => {
    await loginAsAdmin(page, context, '/html/protocol_items.html?apiMode=mock&projectId=1');
    const onlineItem = page.locator('#protocol-item-1');
    const runtimeButton = onlineItem.locator('.protocol-runtime-btn');
    await expect(runtimeButton).toHaveText('已上线');

    await runtimeButton.click();
    await expect(onlineItem.locator('.protocol-runtime-btn')).toHaveText('未上线');
    await onlineItem.locator('.protocol-runtime-btn').click();
    await expect(onlineItem.locator('.protocol-runtime-btn')).toHaveText('已上线');

    await page.goto('/html/protocol_items.html?apiMode=mock&projectId=2');
    const offlineItem = page.locator('#protocol-item-2');
    await expect(offlineItem.locator('.protocol-runtime-btn')).toBeDisabled();
    await expect(offlineItem.locator('.protocol-runtime-btn')).toHaveAttribute(
        'title',
        '项目未运行，不能上线或下线',
    );

    const reconfigItem = page.locator('#protocol-item-5');
    const reconfigButton = reconfigItem.locator('.protocol-runtime-btn');
    await expect(reconfigButton).toHaveText('待重配置');
    await reconfigButton.click();
    await expect(page).toHaveURL(/protocol_item_form\.html\?.*projectId=2.*protocolId=5.*mode=reconfig/);
});

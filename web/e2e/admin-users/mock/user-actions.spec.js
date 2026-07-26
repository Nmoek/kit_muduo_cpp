import { test, expect } from '@playwright/test';
import { loginAsAdmin } from '../../helpers/auth.js';

async function openAddUser(page) {
    await page.click('#add-user-btn');
    const modal = page.locator('.modal-overlay').last();
    await expect(modal.locator('.admin-user-form')).toBeVisible();
    return modal;
}

/**
 * 测试思路：
 *
 * 测什么：新增普通用户的动态表单、note 校验和列表刷新。
 * 为什么这么测：用户弹窗由 innerHTML 动态创建，角色切换和校验容易因事件绑定时机错误而失效。
 * 怎么测：管理员打开新增弹窗提交非法 note，再填写唯一普通用户 note 并提交，检查角色和列表行。
 * 示例：note=a -> 校验错误；userE2E01 -> 新增成功 -> 普通用户。
 */
test('新增���通用户', async ({ page, context }) => {
    await loginAsAdmin(page, context, '/html/admin_users.html?apiMode=mock');
    const modal = await openAddUser(page);
    await modal.locator('#admin-user-note').fill('a');
    await modal.locator('.confirm-btn').click();
    await expect(modal.locator('.admin-user-form-error')).toContainText('note 必须是 3-32 位英文字母和数字');

    await modal.locator('#admin-user-note').fill('userE2E01');
    await modal.locator('.confirm-btn').click();
    const row = page.locator('#admin-users-body tr').filter({ hasText: 'userE2E01' });
    await expect(row).toContainText('普通用户');
});

/**
 * 测试思路：
 *
 * 测什么：新增管理员时密码控件显隐、必填错误和合法提交。
 * 为什么这么测：管理员账号没有密码就无法登录，角色选择后密码字段必须立即成为必填约束。
 * 怎么测：打开新增弹窗切换 admin，先空密码提交检查错误，再填写密码提交并检查角色文本。
 * 示例：role=admin + 空密码 -> 错误；role=admin + password -> 管理员行。
 */
test('新增管理员和密码校验', async ({ page, context }) => {
    await loginAsAdmin(page, context, '/html/admin_users.html?apiMode=mock');
    const modal = await openAddUser(page);
    await modal.locator('#admin-user-role').selectOption('admin');
    await expect(modal.locator('.admin-user-password-group')).toBeVisible();
    await modal.locator('#admin-user-note').fill('adminE2E01');
    await modal.locator('.confirm-btn').click();
    await expect(modal.locator('.admin-user-form-error')).toContainText('管理员用户必须填写密码');

    await modal.locator('#admin-user-password').fill('adminE2Epass');
    await modal.locator('.confirm-btn').click();
    const row = page.locator('#admin-users-body tr').filter({ hasText: 'adminE2E01' });
    await expect(row).toContainText('管理员');
});

/**
 * 测试思路：
 *
 * 测什么：用户 note、角色、管理员密码编辑，以及停用/恢复状态转换。
 * 为什么这么测：编辑同时覆盖回填、角色切换和密码留空语义，停用/恢复还要求列表筛选和操作按钮同步。
 * 怎么测：新增一个普通用户，编辑为管理员并设置密码，随后停用、切到已停用筛选恢复，再切回正常筛选。
 * 示例：普通用户 -> 编辑为管理员 -> 停用 -> 已停用筛选 -> 恢复 -> 正常筛选。
 */
test('编辑用户信息、管理员密码、停用和恢复', async ({ page, context }) => {
    await loginAsAdmin(page, context, '/html/admin_users.html?apiMode=mock');
    const addModal = await openAddUser(page);
    await addModal.locator('#admin-user-note').fill('userE2E02');
    await addModal.locator('.confirm-btn').click();
    let row = page.locator('#admin-users-body tr').filter({ hasText: 'userE2E02' });
    await expect(row).toContainText('普通用户');

    await row.locator('.edit-user-btn').click();
    const editModal = page.locator('.modal-overlay').last();
    await editModal.locator('#admin-user-note').fill('adminE2E02');
    await editModal.locator('#admin-user-role').selectOption('admin');
    await editModal.locator('#admin-user-password').fill('adminE2Epass');
    await editModal.locator('.confirm-btn').click();
    row = page.locator('#admin-users-body tr').filter({ hasText: 'adminE2E02' });
    await expect(row).toContainText('管理员');

    page.on('dialog', dialog => dialog.accept());
    await row.locator('.disable-user-btn').click();
    await page.selectOption('#user-status-filter', 'disabled');
    row = page.locator('#admin-users-body tr').filter({ hasText: 'adminE2E02' });
    await expect(row).toContainText('已停用');
    await row.locator('.restore-user-btn').click();
    await page.selectOption('#user-status-filter', 'active');
    await expect(page.locator('#admin-users-body tr').filter({ hasText: 'adminE2E02' })).toContainText('正常');
});

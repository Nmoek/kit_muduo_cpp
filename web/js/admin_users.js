(function initAdminUsersPage(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});
    const NOTE_PATTERN = /^[A-Za-z0-9]{3,32}$/;

    const pageState = {
        currentPage: 1,
        pageSize: 10,
        status: 'all',
        hasNext: false,
        users: [],
    };

    function escape(value) {
        return KitProxy.utils.escapeHTML(value);
    }

    /**
     * @param {any} user
     * @returns {number}
     */
    function userId(user) {
        return Number(user && (user.id != null ? user.id : user.user_id));
    }

    /**
     * @param {any} user
     * @returns {string}
     */
    function userNote(user) {
        return String(user && (user.note || user.note_name) || '');
    }

    /**
     * @param {any} status
     * @returns {'active' | 'disabled'}
     */
    function normalizeStatus(status) {
        const value = String(status == null ? '' : status).toLowerCase();
        return status === 1 || value === '1' || value === 'active' ? 'active' : 'disabled';
    }

    function statusText(status) {
        return normalizeStatus(status) === 'active' ? '正常' : '已停用';
    }

    function roleText(role) {
        return role === 'admin' ? '管理员' : '普通用户';
    }

    /**
     * @param {string} message
     * @param {'info' | 'error'} type
     */
    function setPageMessage(message, type = 'info') {
        const messageBox = document.getElementById('admin-users-message');
        if (!messageBox) return;
        messageBox.textContent = message || '';
        messageBox.className = `admin-users-message ${message ? 'is-visible' : ''} ${type === 'error' ? 'is-error' : 'is-info'}`;
    }

    /**
     * @param {string} message
     */
    function showGlobalError(message) {
        if (KitProxy.utils && typeof KitProxy.utils.showGlobalError === 'function') {
            KitProxy.utils.showGlobalError(message);
        } else {
            setPageMessage(message, 'error');
        }
    }

    /**
     * 普通用户直接访问时保留页面壳，只显示无权限提示和返回入口。
     * @param {string} message
     */
    function renderAccessDenied(message) {
        const page = document.querySelector('.admin-users-page');
        const addBtn = document.getElementById('add-user-btn');
        if (addBtn) addBtn.disabled = true;
        if (!page) return;

        page.innerHTML = `
            <div class="admin-users-denied">
                <h3>无权限访问用户管理</h3>
                <p>${escape(message || '权限不足，请使用管理员账号登录')}</p>
                <div class="admin-users-denied-actions">
                    <a class="filter-reset-btn" href="${escape(KitProxy.auth.buildMainUrl())}">返回主页面</a>
                    <a class="filter-apply-btn" href="${escape(KitProxy.auth.buildLoginUrl())}">重新登录</a>
                </div>
            </div>
        `;
    }

    function updatePagination() {
        const pageInfo = document.getElementById('users-page-info');
        const prevBtn = document.getElementById('users-prev-page');
        const nextBtn = document.getElementById('users-next-page');

        if (pageInfo) pageInfo.textContent = `第 ${pageState.currentPage} 页`;
        if (prevBtn) prevBtn.disabled = pageState.currentPage <= 1;
        if (nextBtn) nextBtn.disabled = !pageState.hasNext;
    }

    function renderEmptyState() {
        const tbody = document.getElementById('admin-users-body');
        if (!tbody) return;

        tbody.innerHTML = `
            <tr class="admin-users-empty-row">
                <td colspan="7">暂无用户数据</td>
            </tr>
        `;
    }

    function renderUsers() {
        const tbody = document.getElementById('admin-users-body');
        if (!tbody) return;

        if (pageState.users.length === 0) {
            renderEmptyState();
            updatePagination();
            return;
        }

        tbody.innerHTML = pageState.users.map(user => {
            const id = userId(user);
            const status = normalizeStatus(user.status);
            const disabled = status !== 'active';
            const actionHTML = disabled
                ? `<button type="button" class="restore-user-btn" data-action="restore" data-user-id="${escape(id)}">恢复</button>`
                : `<button type="button" class="disable-user-btn" data-action="disable" data-user-id="${escape(id)}">停用</button>`;

            return `
                <tr class="${disabled ? 'is-disabled' : ''}">
                    <td>${escape(id)}</td>
                    <td class="admin-user-note">${escape(userNote(user))}</td>
                    <td><span class="admin-user-role ${user.role === 'admin' ? 'is-admin' : 'is-normal'}">${escape(roleText(user.role))}</span></td>
                    <td><span class="admin-user-status ${status === 'active' ? 'is-active' : 'is-disabled'}">${escape(statusText(status))}</span></td>
                    <td>${escape(user.ctime || '未知')}</td>
                    <td>${escape(user.utime || '未知')}</td>
                    <td class="admin-user-actions">
                        <button type="button" class="edit-user-btn" data-action="edit" data-user-id="${escape(id)}">编辑</button>
                        ${actionHTML}
                    </td>
                </tr>
            `;
        }).join('');

        updatePagination();
    }

    /**
     * @param {number=} page
     */
    async function loadUsers(page = pageState.currentPage) {
        const loading = KitProxy.utils.showLoading('正在加载用户列表...');
        pageState.currentPage = Math.max(1, Number(page) || 1);

        try {
            const offset = (pageState.currentPage - 1) * pageState.pageSize;
            const users = await KitProxy.api.listUsers(offset, pageState.pageSize + 1, pageState.status);
            pageState.hasNext = users.length > pageState.pageSize;
            pageState.users = users.slice(0, pageState.pageSize);
            setPageMessage('');
            renderUsers();
        } catch (error) {
            if (Number(error && error.status) === 403) {
                renderAccessDenied('权限不足，请使用管理员账号登录');
                return;
            }
            setPageMessage(error && error.message ? error.message : '用户列表加载失败', 'error');
        } finally {
            KitProxy.utils.hideLoading(loading);
        }
    }

    /**
     * @param {'add' | 'edit'} mode
     * @param {any=} user
     * @returns {HTMLElement}
     */
    function createUserModal(mode, user = null) {
        const isEdit = mode === 'edit';
        const modal = document.createElement('div');
        modal.className = 'modal-overlay';
        modal.innerHTML = `
            <div class="admin-user-modal">
                <div class="modal-header">
                    <h3>${isEdit ? '编辑用户' : '新增用户'}</h3>
                    <button class="close-modal" type="button">&times;</button>
                </div>
                <form class="admin-user-form" novalidate>
                    <div class="form-group">
                        <label for="admin-user-note">note</label>
                        <input id="admin-user-note" name="note" type="text" maxlength="32" autocomplete="off" value="${escape(userNote(user))}" placeholder="3-32 位英文字母和数字" required>
                    </div>
                    <div class="form-group">
                        <label for="admin-user-role">角色</label>
                        <select id="admin-user-role" name="role">
                            <option value="normal" ${user && user.role !== 'admin' ? 'selected' : ''}>普通用户</option>
                            <option value="admin" ${user && user.role === 'admin' ? 'selected' : ''}>管理员</option>
                        </select>
                    </div>
                    ${isEdit ? `
                        <div class="form-group">
                            <label for="admin-user-status">状态</label>
                            <select id="admin-user-status" name="status">
                                <option value="active" ${normalizeStatus(user && user.status) === 'active' ? 'selected' : ''}>正常</option>
                                <option value="disabled" ${normalizeStatus(user && user.status) !== 'active' ? 'selected' : ''}>已停用</option>
                            </select>
                        </div>
                    ` : ''}
                    <div class="form-group admin-user-password-group">
                        <label for="admin-user-password">管理员密码</label>
                        <input id="admin-user-password" name="password" type="password" autocomplete="new-password" placeholder="${isEdit ? '留空表示不修改密码' : '管理员用户必须填写'}">
                        <p class="form-help" id="admin-user-password-help"></p>
                    </div>
                    <div class="admin-user-form-error" aria-live="polite"></div>
                    <div class="form-actions">
                        <button type="button" class="cancel-btn">取消</button>
                        <button type="submit" class="confirm-btn">${isEdit ? '保存' : '新增'}</button>
                    </div>
                </form>
            </div>
        `;
        document.body.appendChild(modal);
        bindUserModal(modal, mode, user);
        return modal;
    }

    /**
     * @param {HTMLElement} modal
     * @param {'add' | 'edit'} mode
     * @param {any=} user
     */
    function bindUserModal(modal, mode, user = null) {
        const form = modal.querySelector('.admin-user-form');
        const noteInput = modal.querySelector('#admin-user-note');
        const roleSelect = modal.querySelector('#admin-user-role');
        const statusSelect = modal.querySelector('#admin-user-status');
        const passwordGroup = modal.querySelector('.admin-user-password-group');
        const passwordInput = modal.querySelector('#admin-user-password');
        const passwordHelp = modal.querySelector('#admin-user-password-help');
        const errorBox = modal.querySelector('.admin-user-form-error');
        const closeModal = KitProxy.utils.bindModalCloseActions(modal);

        function showFormError(message) {
            errorBox.textContent = message || '';
            errorBox.classList.toggle('is-visible', Boolean(message));
        }

        function syncPasswordControl() {
            const selectedRole = roleSelect.value;
            const editingAdmin = mode === 'edit' && user && user.role === 'admin';
            const needsPassword = selectedRole === 'admin' && (mode === 'add' || !editingAdmin);

            passwordGroup.hidden = selectedRole !== 'admin';
            passwordInput.required = needsPassword;
            passwordHelp.textContent = needsPassword ? '管理员用户必须填写密码' : '留空表示不修改密码';
            if (selectedRole !== 'admin') passwordInput.value = '';
        }

        roleSelect.addEventListener('change', function() {
            syncPasswordControl();
            showFormError('');
        });
        noteInput.addEventListener('input', function() {
            showFormError('');
        });
        passwordInput.addEventListener('input', function() {
            showFormError('');
        });

        form.addEventListener('submit', async function(event) {
            event.preventDefault();
            const note = noteInput.value.trim();
            const role = roleSelect.value === 'admin' ? 'admin' : 'normal';
            const password = passwordInput.value;

            if (!NOTE_PATTERN.test(note)) {
                showFormError('note 必须是 3-32 位英文字母和数字');
                noteInput.focus();
                return;
            }
            if (role === 'admin' && passwordInput.required && !password) {
                showFormError('管理员用户必须填写密码');
                passwordInput.focus();
                return;
            }

            const payload = {
                note,
                role,
            };
            if (mode === 'edit') {
                payload.status = statusSelect.value;
            }
            if (role === 'admin' || password) {
                payload.password = password;
            }

            try {
                if (mode === 'edit') {
                    await KitProxy.api.updateUser(userId(user), payload);
                } else {
                    await KitProxy.api.addUser(payload);
                    pageState.currentPage = 1;
                }
                closeModal();
                await loadUsers(pageState.currentPage);
            } catch (error) {
                showFormError(error && error.message ? error.message : '保存用户失败');
            }
        });

        syncPasswordControl();
        noteInput.focus();
        noteInput.select();
    }

    function findUserInPage(id) {
        return pageState.users.find(user => userId(user) === Number(id));
    }

    async function handleUserAction(action, id) {
        const user = findUserInPage(id);
        if (!user) {
            showGlobalError('用户数据已变化，请刷新后重试');
            return;
        }

        if (action === 'edit') {
            createUserModal('edit', user);
            return;
        }

        if (action === 'disable') {
            if (!confirm(`确定要停用用户 ${userNote(user)} 吗？`)) return;
            try {
                await KitProxy.api.deleteUser(id);
                await loadUsers(pageState.currentPage);
            } catch (error) {
                showGlobalError(error && error.message ? error.message : '停用用户失败');
            }
            return;
        }

        if (action === 'restore') {
            if (!confirm(`确定要恢复用户 ${userNote(user)} 吗？`)) return;
            try {
                await KitProxy.api.restoreUser(id);
                await loadUsers(pageState.currentPage);
            } catch (error) {
                showGlobalError(error && error.message ? error.message : '恢复用户失败');
            }
        }
    }

    function bindPageActions() {
        document.getElementById('add-user-btn')?.addEventListener('click', function() {
            createUserModal('add');
        });

        document.getElementById('refresh-users-btn')?.addEventListener('click', function() {
            loadUsers(pageState.currentPage);
        });

        document.getElementById('user-status-filter')?.addEventListener('change', function(event) {
            pageState.status = event.target.value;
            pageState.currentPage = 1;
            loadUsers(1);
        });

        document.getElementById('users-prev-page')?.addEventListener('click', function() {
            if (pageState.currentPage > 1) loadUsers(pageState.currentPage - 1);
        });

        document.getElementById('users-next-page')?.addEventListener('click', function() {
            if (pageState.hasNext) loadUsers(pageState.currentPage + 1);
        });

        document.getElementById('admin-users-body')?.addEventListener('click', function(event) {
            const button = event.target.closest('button[data-action]');
            if (!button) return;
            handleUserAction(button.dataset.action, Number(button.dataset.userId));
        });
    }

    async function initPage() {
        try {
            await KitProxy.auth.requireCurrentUser({ requireAdmin: true });
        } catch (error) {
            if (Number(error && error.status) === 401) return;
            renderAccessDenied(error && error.message ? error.message : '权限不足，请使用管理员账号登录');
            return;
        }

        bindPageActions();
        await loadUsers(1);
    }

    KitProxy.adminUsersPage = {
        pageState,
        initPage,
        loadUsers,
    };

    document.addEventListener('DOMContentLoaded', initPage);
})(typeof window !== 'undefined' ? window : globalThis);

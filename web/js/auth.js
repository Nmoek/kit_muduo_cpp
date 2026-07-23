(function initKitProxyAuth(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});

    const state = {
        currentUser: null,
        loadingPromise: null,
    };

    /**
     * 保留开发调试参数生成页面地址，避免 mock/后端代理模式在跳转后丢失。
     * @param {string} path
     * @returns {string}
     */
    function buildPageUrl(path) {
        const params = new URLSearchParams(global.location.search);
        const keepKeys = ['apiMode', 'apiBaseUrl', 'enableDebugLog'];
        const nextParams = new URLSearchParams();
        keepKeys.forEach(key => {
            if (params.has(key)) nextParams.set(key, params.get(key));
        });
        const query = nextParams.toString();
        try {
            const url = new URL(path, global.location.href);
            url.search = query;
            return url.pathname + (url.search ? `?${url.searchParams.toString()}` : '') + url.hash;
        } catch (error) {
            return query ? `${path}?${query}` : path;
        }
    }

    /**
     * 只允许同源 /html 目录下的单层 HTML 页面作为登录后的返回地址。
     * 这样可以保留协议项页的 projectId 和调试参数，同时拒绝开放重定向。
     * @param {string} href
     * @returns {string}
     */
    function normalizeReturnTo(href) {
        const value = String(href || '').trim();
        if (!value || value.startsWith('//') || value.startsWith('\\')
            || /^[a-z][a-z\d+.-]*:/i.test(value)) return '';
        try {
            const url = new URL(value, global.location.href);
            if (url.origin !== global.location.origin) return '';
            if (!/^\/html\/[^/?#]+\.html$/i.test(url.pathname)) return '';
            return url.pathname + url.search + url.hash;
        } catch (error) {
            return '';
        }
    }

    function currentReturnTo() {
        if (!global.location) return '';
        return normalizeReturnTo(global.location.pathname + global.location.search + global.location.hash);
    }

    /**
     * @param {{includeReturnTo?: boolean, returnTo?: string}=} options
     * @returns {string}
     */
    function buildLoginUrl(options = {}) {
        const loginUrl = new URL(buildPageUrl('/html/login.html'), global.location.href);
        if (options.includeReturnTo === false) return loginUrl.pathname + loginUrl.search + loginUrl.hash;

        const returnTo = normalizeReturnTo(
            options.returnTo !== undefined ? options.returnTo : currentReturnTo(),
        );
        if (returnTo) loginUrl.searchParams.set('returnTo', returnTo);
        return loginUrl.pathname + (loginUrl.search ? `?${loginUrl.searchParams.toString()}` : '') + loginUrl.hash;
    }

    function buildMainUrl() {
        return buildPageUrl('/html/main.html');
    }

    /**
     * @param {any} user
     * @returns {boolean}
     */
    function isAdmin(user) {
        return String(user && user.role || '').toLowerCase() === 'admin';
    }

    /**
     * @param {any} user
     * @returns {string}
     */
    function roleText(user) {
        return isAdmin(user) ? '管理员' : '普通用户';
    }

    /**
     * @param {any} user
     * @returns {string}
     */
    function noteText(user) {
        return String(user && (user.note || user.note_name) || '');
    }

    function getSafeReturnToFromLoginUrl() {
        if (!global.location) return '';
        const params = new URLSearchParams(global.location.search);
        return normalizeReturnTo(params.get('returnTo'));
    }

    function buildPostLoginUrl() {
        return getSafeReturnToFromLoginUrl() || buildMainUrl();
    }

    function currentUserId(user) {
        const value = Number(user && (user.id != null ? user.id : user.user_id));
        return Number.isInteger(value) && value > 0 ? value : 0;
    }

    function workspaceIdentityForUser(user) {
        const persistence = KitProxy.protocolInteractionPersistence;
        const backendIdentity = persistence && typeof persistence.normalizeBackendIdentity === 'function'
            ? persistence.normalizeBackendIdentity(
                KitProxy.config && KitProxy.config.apiBaseUrl,
                global.location,
            )
            : '';
        return {
            backendIdentity,
            userId: currentUserId(user),
        };
    }

    async function clearUserWorkspace(identity) {
        const persistence = KitProxy.protocolInteractionPersistence;
        if (!persistence || typeof persistence.clearUserWorkspace !== 'function') return null;
        return persistence.clearUserWorkspace(identity);
    }

    function clearRestoreMarker() {
        const persistence = KitProxy.protocolInteractionPersistence;
        if (persistence && typeof persistence.clearRestoreMarker === 'function') {
            persistence.clearRestoreMarker(global.sessionStorage);
        }
    }

    async function logout() {
        // 必须在 logout API 前捕获身份，API 成功后用户对象可能已被后端语义清空。
        const user = state.currentUser;
        const workspaceIdentity = workspaceIdentityForUser(user);
        try {
            await KitProxy.api.logout();
        } catch (error) {
            console.warn('退出登录接口失败，继续清理本地工作区:', error);
        } finally {
            try {
                await clearUserWorkspace(workspaceIdentity);
            } catch (error) {
                console.warn('清理本地实时工作区失败，继续跳转登录页:', error);
            }
            clearRestoreMarker();
            state.currentUser = null;
            redirectToLogin({ includeReturnTo: false });
        }
    }

    function redirectToLogin(options = {}) {
        if (!global.location) return;
        global.location.href = buildLoginUrl(options);
    }

    function redirectToMain() {
        if (!global.location) return;
        global.location.href = buildMainUrl();
    }

    /**
     * @param {string} href
     * @returns {boolean}
     */
    function isInternalHtmlPage(href) {
        const value = String(href || '').trim();
        if (!value || value.startsWith('#')) return false;
        if (/^(?:https?:|mailto:|tel:|javascript:)/i.test(value)) return false;
        return /\.html(?:[?#].*)?$/i.test(value);
    }

    /**
     * 侧边栏是静态 HTML，页面跳转时需要保留开发调试参数，避免 mock 模式跳转后变成 real。
     */
    function syncSidebarLinks() {
        document.querySelectorAll('.sidebar nav a[href]').forEach(link => {
            const href = link.getAttribute('href');
            if (!isInternalHtmlPage(href)) return;
            link.href = buildPageUrl(href);
        });
    }

    /**
     * @param {HTMLElement} navList
     * @param {string} selector
     * @param {string} href
     * @param {string} text
     * @returns {HTMLLIElement}
     */
    function ensureAdminNavItem(navList, selector, href, text) {
        let item = navList.querySelector(selector);
        const targetUrl = buildPageUrl(href);

        if (item) {
            const link = item.querySelector('a');
            if (link) link.href = targetUrl;
        } else {
            item = document.createElement('li');
            item.innerHTML = `<a href="${KitProxy.utils.escapeHTML(targetUrl)}">${KitProxy.utils.escapeHTML(text)}</a>`;
        }

        if (selector === '[data-admin-users-nav]') {
            item.setAttribute('data-admin-users-nav', '1');
        }
        item.hidden = false;
        return item;
    }

    /**
     * 管理员入口只在当前用户是 admin 时显示。
     * @param {any} user
     */
    function syncAdminNav(user) {
        const navList = document.querySelector('.sidebar nav ul');
        if (!navList) return;

        if (!isAdmin(user)) {
            navList.querySelectorAll('[data-admin-users-nav]').forEach(item => item.remove());
            return;
        }

        const usersItem = ensureAdminNavItem(navList, '[data-admin-users-nav]', 'admin_users.html', '用户管理');
        if (!usersItem.parentNode) navList.appendChild(usersItem);
    }

    /**
     * 在业务页面头部显示当前登录用户和退出登录按钮。
     * @param {any} user
     */
    function renderUserPanel(user) {
        const header = document.querySelector('.service-manager-header');
        if (!header || !user) return;

        const actionBar = ensureHeaderActionBar(header);
        let panel = actionBar.querySelector('.user-session-bar');
        if (!panel) {
            panel = document.createElement('div');
            panel.className = 'user-session-bar';
            actionBar.appendChild(panel);
        }

        panel.innerHTML = `
            <div class="user-session-info">
                <span class="user-note">${KitProxy.utils.escapeHTML(noteText(user))}</span>
                <span class="user-role ${isAdmin(user) ? 'is-admin' : 'is-normal'}">${KitProxy.utils.escapeHTML(roleText(user))}</span>
            </div>
            <button type="button" class="logout-btn">退出登录</button>
        `;

        panel.querySelector('.logout-btn')?.addEventListener('click', logout);
    }

    /**
     * 头部右侧按钮和用户信息条需要在同一个容器内对齐，避免 flex 直接分散三个子元素。
     * @param {HTMLElement} header
     * @returns {HTMLElement}
     */
    function ensureHeaderActionBar(header) {
        let actionBar = header.querySelector('.header-action-bar');
        if (!actionBar) {
            actionBar = document.createElement('div');
            actionBar.className = 'header-action-bar';
            header.appendChild(actionBar);
        }

        Array.from(header.children).forEach(child => {
            if (child === actionBar || child.classList.contains('page-title-block')) return;
            actionBar.appendChild(child);
        });

        return actionBar;
    }

    /**
     * 页面成功拿到当前用户后同步全局状态、导航和用户条。
     * @param {any} user
     * @returns {any}
     */
    function applyCurrentUser(user) {
        state.currentUser = user;
        if (typeof document !== 'undefined' && document.body) {
            document.body.dataset.userRole = isAdmin(user) ? 'admin' : 'normal';
            syncSidebarLinks();
            syncAdminNav(user);
            renderUserPanel(user);
        }
        return user;
    }

    /**
     * 请求当前用户。401 默认跳转登录页，调用方可关闭跳转自行处理。
     * @param {{redirectOnUnauthorized?: boolean}=} options
     * @returns {Promise<any>}
     */
    async function loadCurrentUser(options = {}) {
        const redirectOnUnauthorized = options.redirectOnUnauthorized !== false;

        if (state.currentUser) return state.currentUser;
        if (state.loadingPromise) return state.loadingPromise;

        state.loadingPromise = KitProxy.api.getCurrentUser()
            .then(applyCurrentUser)
            .catch(function(error) {
                state.currentUser = null;
                if (Number(error && error.status) === 401 && redirectOnUnauthorized) {
                    redirectToLogin();
                }
                throw error;
            })
            .finally(function() {
                state.loadingPromise = null;
            });

        return state.loadingPromise;
    }

    /**
     * 业务页面入口守卫。管理员页面设置 requireAdmin 后，普通用户会得到 403 错误。
     * @param {{requireAdmin?: boolean, redirectOnUnauthorized?: boolean}=} options
     * @returns {Promise<any>}
     */
    async function requireCurrentUser(options = {}) {
        const user = await loadCurrentUser(options);
        if (options.requireAdmin && !isAdmin(user)) {
            const error = new Error('无权限访问用户管理');
            error.status = 403;
            throw error;
        }
        return user;
    }

    KitProxy.auth = {
        state,
        buildPageUrl,
        buildLoginUrl,
        buildMainUrl,
        normalizeReturnTo,
        getSafeReturnToFromLoginUrl,
        buildPostLoginUrl,
        syncSidebarLinks,
        loadCurrentUser,
        requireCurrentUser,
        applyCurrentUser,
        redirectToLogin,
        redirectToMain,
        logout,
        isAdmin,
        roleText,
        noteText,
        getCurrentUser: function() {
            return state.currentUser;
        },
        isCurrentUserAdmin: function() {
            return isAdmin(state.currentUser);
        },
    };
})(typeof window !== 'undefined' ? window : globalThis);

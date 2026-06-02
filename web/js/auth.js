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

    function buildLoginUrl() {
        return buildPageUrl('/html/login.html');
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

    function redirectToLogin() {
        if (!global.location) return;
        global.location.href = buildLoginUrl();
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
     * @param {HTMLLIElement} item
     * @returns {boolean}
     */
    function isDashboardNavItem(item) {
        if (!item) return false;
        if (item.matches('[data-admin-dashboard-nav]')) return true;
        const link = item.querySelector('a');
        return Boolean(link && String(link.getAttribute('href') || '').includes('dashboard.html'));
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
        if (!item && selector === '[data-admin-dashboard-nav]') {
            item = Array.from(navList.querySelectorAll('li')).find(isDashboardNavItem);
        }
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
        if (selector === '[data-admin-dashboard-nav]') {
            item.setAttribute('data-admin-dashboard-nav', '1');
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
            navList.querySelectorAll('[data-admin-users-nav], [data-admin-dashboard-nav]').forEach(item => item.remove());
            Array.from(navList.querySelectorAll('li')).filter(isDashboardNavItem).forEach(item => item.remove());
            return;
        }

        const usersItem = ensureAdminNavItem(navList, '[data-admin-users-nav]', 'admin_users.html', '用户管理');
        const dashboardItem = ensureAdminNavItem(navList, '[data-admin-dashboard-nav]', 'dashboard.html', '控制面板');

        // 用户管理是管理员专属入口，固定排在“控制面板”上方，避免各页面静态导航顺序不一致。
        if (!dashboardItem.parentNode) navList.appendChild(dashboardItem);
        if (dashboardItem !== usersItem.nextElementSibling) {
            navList.insertBefore(usersItem, dashboardItem);
        }
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

        panel.querySelector('.logout-btn')?.addEventListener('click', async function() {
            try {
                await KitProxy.api.logout();
            } catch (error) {
                console.warn('退出登录接口失败，继续跳转登录页:', error);
            } finally {
                state.currentUser = null;
                redirectToLogin();
            }
        });
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
        syncSidebarLinks,
        loadCurrentUser,
        requireCurrentUser,
        applyCurrentUser,
        redirectToLogin,
        redirectToMain,
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

(function initDashboardPage(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});
    const DASHBOARD_PROJECT_LIMIT = 1000;

    /**
     * @param {string} id
     * @param {string | number} value
     */
    function setText(id, value) {
        const element = document.getElementById(id);
        if (element) element.textContent = String(value);
    }

    /**
     * @param {string} id
     * @param {boolean} success
     */
    function setTagSuccess(id, success) {
        const element = document.getElementById(id);
        if (!element) return;
        element.classList.toggle('success', Boolean(success));
    }

    /**
     * dashboard.html 是管理员页面，权限确认前不展示主体内容。
     * @param {boolean} visible
     */
    function setDashboardVisible(visible) {
        const content = document.querySelector('[data-dashboard-content]');
        if (content) content.hidden = !visible;
    }

    /**
     * @param {any} project
     * @returns {boolean}
     */
    function isDeletedProject(project) {
        return Number(project && project.status) === 0;
    }

    /**
     * @param {any} project
     * @returns {boolean}
     */
    function isActiveProject(project) {
        return !isDeletedProject(project) && Number(project && project.active) === 1;
    }

    /**
     * 控制面板统计使用统一 API 入口；mock 模式下会自动走 mock_data.js，真实模式下走后端接口。
     * @returns {Promise<Array<any>>}
     */
    async function loadDashboardProjects() {
        const projects = await KitProxy.api.getProjectList(0, DASHBOARD_PROJECT_LIMIT);
        if (!Array.isArray(projects)) {
            throw new Error('控制面板项目数据格式错误');
        }
        return projects.filter(project => !isDeletedProject(project));
    }

    /**
     * @param {Array<any>} projects
     */
    function renderDashboardStats(projects) {
        setText('dashboard-total-projects', projects.length);
        setText('dashboard-active-projects', projects.filter(isActiveProject).length);
        setText('dashboard-system-status', '正常');
        setText('dashboard-status-tag', '运行中');
        setTagSuccess('dashboard-status-tag', true);
        document.getElementById('dashboard-system-status')?.classList.add('online');
    }

    /**
     * @param {Error} error
     */
    function renderDashboardError(error) {
        setText('dashboard-total-projects', '--');
        setText('dashboard-active-projects', '--');
        setText('dashboard-system-status', '异常');
        setText('dashboard-status-tag', '异常');
        setTagSuccess('dashboard-status-tag', false);
        document.getElementById('dashboard-system-status')?.classList.remove('online');

        if (Number(error && error.status) !== 401 && KitProxy.utils) {
            KitProxy.utils.showGlobalError(error && error.message ? error.message : '控制面板加载失败');
        }
    }

    async function initPage() {
        try {
            await KitProxy.auth.requireCurrentUser({ requireAdmin: true });
        } catch (error) {
            setDashboardVisible(false);
            if (Number(error && error.status) === 403) {
                if (KitProxy.utils) KitProxy.utils.showGlobalError('普通用户无权限访问控制面板');
                KitProxy.auth.redirectToMain();
                return;
            }
            renderDashboardError(error);
            return;
        }

        setDashboardVisible(true);
        try {
            const projects = await loadDashboardProjects();
            renderDashboardStats(projects);
        } catch (error) {
            renderDashboardError(error);
        }
    }

    KitProxy.dashboardPage = {
        initPage,
        loadDashboardProjects,
        renderDashboardStats,
        setDashboardVisible,
    };

    document.addEventListener('DOMContentLoaded', initPage);
})(typeof window !== 'undefined' ? window : globalThis);

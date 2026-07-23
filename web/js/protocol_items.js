(function initProtocolItemsPage(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});
    const pageState = KitProxy.pagination.createState();
    const pageContext = {
        project: null,
        protocolRuntimeStateOverrides: new Map(),
    };
    let restoreAttempted = false;

    /**
     * 保留调试参数返回服务列表页。
     * @returns {string}
     */
    function buildMainPageUrl() {
        const params = new URLSearchParams(global.location.search);
        params.delete('projectId');
        const query = params.toString();
        return query ? `main.html?${query}` : 'main.html';
    }

    /**
     * 保留调试参数生成协议项表单页 URL。
     * @param {number | string} projectId
     * @returns {string}
     */
    function buildProtocolItemCreateUrl(projectId) {
        const params = new URLSearchParams(global.location.search);
        params.set('projectId', String(projectId));
        params.delete('protocolId');
        return `protocol_item_form.html?${params.toString()}`;
    }

    /**
     * 从 URL 中读取 projectId。
     * @returns {number}
     */
    function readProjectId() {
        const params = new URLSearchParams(global.location.search);
        const projectId = Number(params.get('projectId'));
        return Number.isInteger(projectId) && projectId > 0 ? projectId : -1;
    }

    /**
     * @returns {HTMLElement | null}
     */
    function getPageRoot() {
        return document.querySelector('.protocol-items-page');
    }

    /**
     * @returns {HTMLElement | null}
     */
    function getProtocolListElement() {
        const root = getPageRoot();
        return root ? root.querySelector('.protocol-list') : null;
    }

    function isProjectActive(project) {
        return Number(project && project.runtime_state) === 1;
    }

    function isProjectDeleted(project) {
        return Number(project && project.status) === 0;
    }

    function getProjectEndpointDisplay(project) {
        if (!project) return '未设置';
        if (isProjectDeleted(project)) {
            if (Number(project.mode) === ProjectMode.SERVER) {
                return Number(project.listen_port) > 0 ? project.listen_port : '未分配';
            }
            return project.target_ip || '未设置';
        }
        if (!isProjectActive(project)) return '未开启';
        if (Number(project.mode) === ProjectMode.SERVER) {
            return Number(project.listen_port) > 0 ? project.listen_port : '未分配';
        }
        return project.target_ip || '未设置';
    }

    function setAddButtonState(project) {
        const addBtn = document.getElementById('add-protocol-item');
        if (!addBtn) return;
        addBtn.disabled = !project || isProjectDeleted(project);
        addBtn.title = !project ? '请先选择测试服务' : (isProjectDeleted(project) ? '已删除的测试服务不能新增协议项' : '');
    }

    /**
     * @param {string} message
     */
    function renderPageError(message) {
        const addBtn = document.getElementById('add-protocol-item');
        const title = document.getElementById('protocol-items-title');
        const meta = document.getElementById('protocol-service-meta');

        if (title) title.textContent = '协议项管理';
        if (meta) meta.innerHTML = '';
        if (addBtn) addBtn.disabled = true;

        if (KitProxy.utils && typeof KitProxy.utils.showGlobalError === 'function') {
            KitProxy.utils.showGlobalError(message, {
                actionText: '返回测试服务列表',
                actionHref: buildMainPageUrl(),
                durationMs: 0,
            });
        }
    }

    /**
     * @param {any} project
     */
    function renderProjectContext(project) {
        const escape = KitProxy.utils && KitProxy.utils.escapeHTML
            ? KitProxy.utils.escapeHTML
            : function(value) { return String(value == null ? '' : value); };
        const root = getPageRoot();
        const title = document.getElementById('protocol-items-title');
        const meta = document.getElementById('protocol-service-meta');
        const backLink = document.getElementById('back-service-list');
        const addBtn = document.getElementById('add-protocol-item');

        if (root) {
            root.id = `service-card-${project.id}`;
            root.dataset.protocolType = String(project.protocol_type);
            root.dataset.runtimeState = String(isProjectActive(project) ? 1 : 0);
            root.dataset.active = String(isProjectActive(project) ? 1 : 0);
        }

        if (title) {
            title.textContent = `协议项管理：${project.name || ''}`;
        }

        if (backLink) {
            backLink.href = buildMainPageUrl();
        }

        setAddButtonState(project);

        if (meta) {
            const statusText = isProjectDeleted(project) ? '已删除' : (isProjectActive(project) ? '开启' : '未开启');
            meta.innerHTML = `
                <div class="service-field project-protocol-type">
                    <span class="field-label">协议种类</span>
                    <span class="field-value">${escape(ProtocolTypeStr[project.protocol_type] || '未知协议')}</span>
                </div>
                <div class="service-field project-mode">
                    <span class="field-label">测试模式</span>
                    <span class="field-value">${escape(ProjectModeStr[project.mode] || '未知模式')}</span>
                </div>
                <div class="service-field project-${project.mode === ProjectMode.SERVER ? 'listen-port' : 'target-ip'}">
                    <span class="field-label">${project.mode === ProjectMode.SERVER ? '监听端口' : '目标IP/端口'}</span>
                    <span class="field-value">${escape(getProjectEndpointDisplay(project))}</span>
                </div>
                <button type="button" class="service-field project-status service-active-toggle" data-next-active="${isProjectActive(project) ? '0' : '1'}" aria-label="${isProjectActive(project) ? '停止测试服务' : '启动测试服务'}" ${isProjectDeleted(project) ? 'disabled' : ''}>
                    <span class="field-label">服务状态</span>
                    <span class="field-value status ${isProjectDeleted(project) ? 'status-deleted' : (isProjectActive(project) ? 'status-active' : 'status-inactive')}">
                        ${escape(statusText)}
                    </span>
                </button>
                ${isProjectDeleted(project) ? '' : ProtocolTypeRegistry.serviceExtraFieldsHTML(project)}
            `;
        }

        if (root && !isProjectDeleted(project)) {
            ProtocolTypeRegistry.bindServiceExtraActions(root, project);
        }

        if (!isProjectDeleted(project)) {
            bindProjectActiveToggle();
        }
    }

    function bindProjectActiveToggle() {
        const toggleButton = document.querySelector('#protocol-service-meta .service-active-toggle');
        if (!toggleButton) return;

        toggleButton.addEventListener('click', async function(event) {
            event.preventDefault();
            event.stopPropagation();
            if (!pageContext.project) return;

            const nextActive = toggleButton.dataset.nextActive === '1';
            toggleButton.disabled = true;
            toggleButton.classList.add('is-busy');

            try {
                const runtimeData = await KitProxy.api.setProjectRuntimeState(pageContext.project.id, nextActive);
                pageContext.project = Object.assign({}, pageContext.project, runtimeData || {}, {
                    runtime_state: nextActive ? 1 : 0,
                    active: nextActive ? 1 : 0,
                });
                if (!nextActive && Number(pageContext.project.mode) === ProjectMode.SERVER) {
                    pageContext.project.listen_port = 0;
                }
                renderProjectContext(pageContext.project);
                syncProtocolRuntimeControls();
                if (!nextActive && KitProxy.protocolInteractionDrawer
                    && typeof KitProxy.protocolInteractionDrawer.cleanupProject === 'function') {
                    KitProxy.protocolInteractionDrawer.cleanupProject(pageContext.project.id);
                }
            } catch (error) {
                KitProxy.utils.showGlobalError(`${nextActive ? '启动' : '停止'}测试服务失败：${error.message}`);
                toggleButton.disabled = false;
                toggleButton.classList.remove('is-busy');
            }
        });
    }

    function syncProtocolRuntimeControls() {
        const root = getPageRoot();
        if (!root || typeof global.refreshProtocolRuntimeControl !== 'function') return;

        const runtimeState = String(isProjectActive(pageContext.project) ? 1 : 0);
        root.dataset.runtimeState = runtimeState;
        root.dataset.active = runtimeState;
        root.querySelectorAll('.protocol-item').forEach(protocolItem => {
            protocolItem.dataset.projectRuntimeState = runtimeState;
            global.refreshProtocolRuntimeControl(protocolItem, protocolItem.dataset.configState);
        });
    }

    /**
     * 协议项列表空状态。
     */
    function checkProtocolEmptyState() {
        const protocolList = getProtocolListElement();
        if (!protocolList) return;

        const existingEmptyState = protocolList.querySelector('.empty-state');
        const realItemCount = Array.from(protocolList.children)
            .filter(child => !child.classList.contains('empty-state'))
            .length;

        if (realItemCount === 0) {
            if (!existingEmptyState) {
                const emptyState = document.createElement('div');
                emptyState.className = 'empty-state';
                emptyState.innerHTML = `
                    <div class="empty-message">
                        <p>暂无协议项，点击按钮添加</p>
                    </div>
                `;
                protocolList.appendChild(emptyState);
            }
        } else if (existingEmptyState) {
            existingEmptyState.remove();
        }
    }

    /**
     * 渲染协议项分页条。
     */
    function renderProtocolPagination() {
        KitProxy.pagination.render(document.getElementById('protocol-pagination'), pageState, {
            pageSizeOptions: KitProxy.pagination.DEFAULT_PAGE_SIZE_OPTIONS,
            onPageSizeChange: function(pageSize) {
                pageState.pageSize = pageSize;
                pageState.currentPage = 1;
                loadProtocolItems(1);
            },
            onPrev: function() {
                loadProtocolItems(pageState.currentPage - 1);
            },
            onNext: function() {
                loadProtocolItems(pageState.currentPage + 1);
            },
        });
    }

    /**
     * 记录刚刚由运行态命令确认成功的协议项状态，避免列表接口短暂返回旧值时把按钮回刷为旧状态。
     * @param {number | string} protocolId
     * @param {number | string} configState
     */
    function rememberProtocolRuntimeState(protocolId, configState) {
        const id = Number(protocolId);
        const state = Number(configState);
        if (!Number.isInteger(id) || id <= 0 || ![0, 1, 2].includes(state)) return;

        pageContext.protocolRuntimeStateOverrides.set(id, state);
    }

    function clearProtocolRuntimeStateOverrides() {
        pageContext.protocolRuntimeStateOverrides.clear();
    }

    /**
     * 应用本页已确认的运行态命令结果；当列表接口返回同值后移除覆盖。
     * @param {any} protocol
     * @returns {any}
     */
    function applyProtocolRuntimeStateOverride(protocol) {
        const protocolId = Number(protocol && protocol.id);
        if (!Number.isInteger(protocolId) || !pageContext.protocolRuntimeStateOverrides.has(protocolId)) {
            return protocol;
        }

        const overrideState = pageContext.protocolRuntimeStateOverrides.get(protocolId);
        if (Number(protocol.config_state) === overrideState) {
            pageContext.protocolRuntimeStateOverrides.delete(protocolId);
            return protocol;
        }

        return Object.assign({}, protocol, {
            config_state: overrideState,
        });
    }

    function currentWorkspaceIdentity(protocolId) {
        const persistence = KitProxy.protocolInteractionPersistence;
        const user = KitProxy.auth && typeof KitProxy.auth.getCurrentUser === 'function'
            ? KitProxy.auth.getCurrentUser()
            : null;
        if (!persistence || typeof persistence.createWorkspaceIdentity !== 'function' || !pageContext.project) {
            return null;
        }
        return persistence.createWorkspaceIdentity({
            apiBaseUrl: KitProxy.config && KitProxy.config.apiBaseUrl,
            user,
            projectId: pageContext.project.id,
            protocolId,
        });
    }

    function restoreMarkedDrawer() {
        if (restoreAttempted) return { status: 'already_attempted' };
        restoreAttempted = true;
        const persistence = KitProxy.protocolInteractionPersistence;
        const drawerApi = KitProxy.protocolInteractionDrawer;
        if (!persistence || !drawerApi || typeof drawerApi.restore !== 'function' || !pageContext.project) {
            return { status: 'unavailable' };
        }
        const markerStorage = global.sessionStorage;
        const rawMarker = typeof persistence.readRestoreMarker === 'function'
            ? persistence.readRestoreMarker(markerStorage)
            : null;
        if (!rawMarker || rawMarker.drawerOpen !== true) return { status: 'no_marker' };
        const identity = currentWorkspaceIdentity(rawMarker.protocolId);
        const marker = identity && typeof persistence.readValidRestoreMarker === 'function'
            ? persistence.readValidRestoreMarker(identity, markerStorage)
            : null;
        if (!marker) return { status: 'invalid_marker' };
        const protocolItem = document.querySelector(
            `.protocol-item[data-project-id="${marker.projectId}"][data-protocol-id="${marker.protocolId}"]`,
        );
        if (!protocolItem) {
            persistence.clearRestoreMarker(markerStorage);
            return { status: 'target_missing' };
        }
        const entry = {
            projectId: marker.projectId,
            protocolId: marker.protocolId,
            name: protocolItem.querySelector('.protocol-name')
                ? protocolItem.querySelector('.protocol-name').textContent
                : '',
            protocolType: protocolItem.dataset.protocolType || '',
            projectRuntimeState: Number(protocolItem.dataset.projectRuntimeState),
            configState: Number(protocolItem.dataset.configState),
            deleted: protocolItem.dataset.status === 'inactive',
            triggerButton: protocolItem.querySelector('.protocol-interaction-btn'),
        };
        if (!drawerApi.isEligible(entry)) {
            persistence.clearRestoreMarker(markerStorage);
            return { status: 'target_unavailable' };
        }
        return drawerApi.restore(entry) ? { status: 'restored', entry } : { status: 'restore_failed' };
    }

    /**
     * 加载当前服务的协议项分页列表。
     * @param {number=} page
     */
    async function loadProtocolItems(page = pageState.currentPage) {
        const root = getPageRoot();
        const protocolList = getProtocolListElement();
        if (!root || !protocolList || !pageContext.project) return;

        const loading = showLoading('正在加载协议项列表...');

        try {
            pageState.currentPage = Math.max(1, Number(page) || 1);

            const protocols = await getProtocolList(
                pageContext.project.id,
                KitProxy.pagination.getOffset(pageState),
                KitProxy.pagination.getRequestLimit(pageState),
            );

            if(!Array.isArray(protocols)) {
                throw new Error('协议项列表数据格式错误!');
            }

            protocolList.innerHTML = '';
            KitProxy.pagination.takeVisibleItems(protocols, pageState).forEach(protocol => {
                addProtocolItem(root, applyProtocolRuntimeStateOverride(protocol));
            });

            checkProtocolEmptyState();
            renderProtocolPagination();
            restoreMarkedDrawer();
        } catch(error) {
            console.error('加载协议项列表出错:', error);
            KitProxy.utils.showGlobalError('加载协议项列表出错： ' + error.message);
        } finally {
            await delay(500);
            hideLoading(loading);
        }
    }

    /**
     * 绑定新增协议项按钮。
     */
    function bindAddProtocolButton() {
        const addBtn = document.getElementById('add-protocol-item');
        const root = getPageRoot();
        if (!addBtn || !root) return;

        addBtn.addEventListener('click', function(e) {
            e.preventDefault();

            if (!pageContext.project) {
                KitProxy.utils.showGlobalError('请先选择测试服务');
                return;
            }
            const targetUrl = buildProtocolItemCreateUrl(pageContext.project.id);
            addBtn.dataset.protocolItemFormUrl = targetUrl;
            const navigateEvent = new CustomEvent('protocol-items:navigate-create', {
                bubbles: true,
                cancelable: true,
                detail: {
                    projectId: pageContext.project.id,
                    url: targetUrl,
                },
            });
            addBtn.dispatchEvent(navigateEvent);
            if (navigateEvent.defaultPrevented) return;
            global.location.href = targetUrl;
        });
    }

    /**
     * 初始化协议项页。
     */
    async function initPage() {
        try {
            await KitProxy.auth.requireCurrentUser();
        } catch (error) {
            if (Number(error && error.status) !== 401) {
                renderPageError(error && error.message ? error.message : '登录态校验失败');
            }
            return;
        }

        const projectId = readProjectId();
        const backLink = document.getElementById('back-service-list');
        if (backLink) backLink.href = buildMainPageUrl();

        if (projectId < 0) {
            renderPageError('缺少或非法的测试服务 ID，无法加载协议项。');
            return;
        }

        const loading = showLoading('正在加载测试服务信息...');

        try {
            const projects = await getProjectReq(projectId);
            pageContext.project = projects[0];
            renderProjectContext(pageContext.project);
            bindAddProtocolButton();
            if (isProjectDeleted(pageContext.project)) {
                checkProtocolEmptyState();
                renderProtocolPagination();
                return;
            }
            await loadProtocolItems(1);
        } catch(error) {
            console.error('加载测试服务信息失败:', error);
            renderPageError('测试服务信息加载失败：' + error.message);
        } finally {
            await delay(300);
            hideLoading(loading);
        }
    }

    KitProxy.protocolItemsPage = {
        pageState,
        buildProtocolItemCreateUrl,
        initPage,
        loadProtocolItems,
        restoreMarkedDrawer,
        rememberProtocolRuntimeState,
        clearProtocolRuntimeStateOverrides,
        handleProtocolAdded: async function() {
            await loadProtocolItems(1);
        },
        handleProtocolDeleted: async function() {
            if (KitProxy.protocolInteractionDrawer
                && typeof KitProxy.protocolInteractionDrawer.cleanupProtocol === 'function') {
                const protocolId = arguments[0] && arguments[0].dataset
                    ? arguments[0].dataset.protocolId
                    : null;
                if (protocolId) {
                    KitProxy.protocolInteractionDrawer.cleanupProtocol(pageContext.project && pageContext.project.id, protocolId);
                }
            }
            const protocolList = getProtocolListElement();
            const visibleCountBeforeDelete = protocolList
                ? protocolList.querySelectorAll('.protocol-item').length
                : 0;
            const nextPage = KitProxy.pagination.nextPageAfterDelete(pageState, visibleCountBeforeDelete);
            await loadProtocolItems(nextPage);
        },
    };

    if (!KitProxy.__disableAutoInitProtocolItems) {
        document.addEventListener('DOMContentLoaded', initPage);
    }
})(typeof window !== 'undefined' ? window : globalThis);

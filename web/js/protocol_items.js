(function initProtocolItemsPage(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});
    const pageState = KitProxy.pagination.createState();
    const pageContext = {
        project: null,
    };

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

    /**
     * @param {string} message
     */
    function renderPageError(message) {
        const errorBox = document.getElementById('protocol-page-error');
        const addBtn = document.getElementById('add-protocol-item');
        const title = document.getElementById('protocol-items-title');
        const meta = document.getElementById('protocol-service-meta');

        if (title) title.textContent = '协议项管理';
        if (meta) meta.innerHTML = '';
        if (addBtn) addBtn.disabled = true;

        if (errorBox) {
            errorBox.style.display = 'block';
            errorBox.innerHTML = `
                <p>${message}</p>
                <a class="back-link" href="${buildMainPageUrl()}">返回测试服务列表</a>
            `;
        }
    }

    /**
     * @param {any} project
     */
    function renderProjectContext(project) {
        const root = getPageRoot();
        const title = document.getElementById('protocol-items-title');
        const meta = document.getElementById('protocol-service-meta');
        const backLink = document.getElementById('back-service-list');
        const addBtn = document.getElementById('add-protocol-item');

        if (root) {
            root.id = `service-card-${project.id}`;
            root.dataset.protocolType = String(project.protocol_type);
        }

        if (title) {
            title.textContent = `协议项管理：${project.name}`;
        }

        if (backLink) {
            backLink.href = buildMainPageUrl();
        }

        if (addBtn) {
            addBtn.disabled = false;
        }

        if (meta) {
            meta.innerHTML = `
                <div class="service-field project-protocol-type">
                    <span class="field-label">协议种类</span>
                    <span class="field-value">${ProtocolTypeStr[project.protocol_type] || '未知协议'}</span>
                </div>
                <div class="service-field project-mode">
                    <span class="field-label">测试模式</span>
                    <span class="field-value">${ProjectModeStr[project.mode] || '未知模式'}</span>
                </div>
                <div class="service-field project-${project.mode === ProjectMode.SERVER ? 'listen-port' : 'target-ip'}">
                    <span class="field-label">${project.mode === ProjectMode.SERVER ? '监听端口' : '目标IP/端口'}</span>
                    <span class="field-value">${project.mode === ProjectMode.SERVER ? project.listen_port : project.target_ip || '未设置'}</span>
                </div>
                <div class="service-field project-status">
                    <span class="field-label">服务状态</span>
                    <span class="field-value status ${project.status ? 'status-active' : 'status-inactive'}">
                        ${project.status ? '开启' : '未开启'}
                    </span>
                </div>
                ${ProtocolTypeRegistry.serviceExtraFieldsHTML(project)}
            `;
        }

        if (root) {
            ProtocolTypeRegistry.bindServiceExtraActions(root, project);
        }
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
            onPrev: function() {
                loadProtocolItems(pageState.currentPage - 1);
            },
            onNext: function() {
                loadProtocolItems(pageState.currentPage + 1);
            },
        });
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
                addProtocolItem(root, protocol);
            });

            checkProtocolEmptyState();
            renderProtocolPagination();
        } catch(error) {
            console.error('加载协议项列表出错:', error);
            alert('加载协议项列表出错： ' + error.message);
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
                alert('请先选择测试服务');
                return;
            }

            const modalFactory = ProtocolTypeRegistry.getAddProtocolModal(pageContext.project.protocol_type);
            if (!modalFactory) {
                alert('当前协议类型暂不支持添加协议项');
                return;
            }

            const modal = createAddProtocolModal(modalFactory, root);
            if (modal) {
                document.body.appendChild(modal);
            }
        });
    }

    /**
     * 初始化协议项页。
     */
    async function initPage() {
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
        loadProtocolItems,
        handleProtocolAdded: async function() {
            await loadProtocolItems(1);
        },
        handleProtocolDeleted: async function() {
            const protocolList = getProtocolListElement();
            const visibleCountBeforeDelete = protocolList
                ? protocolList.querySelectorAll('.protocol-item').length
                : 0;
            const nextPage = KitProxy.pagination.nextPageAfterDelete(pageState, visibleCountBeforeDelete);
            await loadProtocolItems(nextPage);
        },
    };

    document.addEventListener('DOMContentLoaded', initPage);
})(typeof window !== 'undefined' ? window : globalThis);

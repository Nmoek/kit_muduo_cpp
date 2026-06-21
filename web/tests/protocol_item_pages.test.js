import { describe, expect, it, vi } from 'vitest';
import { createBrowserContext, createProtocolItemFormContext, createProtocolItemPageContext, flushPromises, loadCoreScripts, loadProtocolListScripts, loginMockUser, readRepoFile, repoFileExists, runScript } from './helpers/browser_context.js';

describe('V1.5 protocol item form page and compact cards', () => {
    /**
     * 测试思路：协议项管理页的新增按钮只负责跳转到独立表单页，并保留当前调试参数。
     * 示例：projectId=1 且 apiMode=mock 时，点击按钮应生成 protocol_item_form.html?apiMode=mock&projectId=1。
     */
    it('协议项页添加按钮跳转到独立表单页', async () => {
        const context = createProtocolItemPageContext(1);

        loadCoreScripts(context);
        await loginMockUser(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);

        context.KitProxy.protocolItemsPage.initPage?.();
        context.document.dispatchEvent(new context.Event('DOMContentLoaded'));
        await flushPromises(8);

        const addButton = context.document.getElementById('add-protocol-item');
        let targetUrl = '';
        addButton.addEventListener('protocol-items:navigate-create', event => {
            event.preventDefault();
            targetUrl = event.detail.url;
        });
        addButton.click();

        expect(addButton.dataset.protocolItemFormUrl).toBe('protocol_item_form.html?apiMode=mock&projectId=1');
        expect(targetUrl).toBe('protocol_item_form.html?apiMode=mock&projectId=1');
    });

    /**
     * 测试思路：active 只表示服务运行态，不应阻止协议项配置维护。
     * 示例：Mock projectId=2 active=0，初始化后新增按钮仍可进入协议项表单。
     */
    it('协议项管理页未开启服务时仍允许新增协议项', async () => {
        const context = createProtocolItemPageContext(2);

        loadCoreScripts(context);
        await loginMockUser(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);

        await context.KitProxy.protocolItemsPage.initPage?.();
        await flushPromises(12);

        const addButton = context.document.getElementById('add-protocol-item');
        let targetUrl = '';
        addButton.addEventListener('protocol-items:navigate-create', event => {
            event.preventDefault();
            targetUrl = event.detail.url;
        });
        addButton.click();

        expect(addButton.disabled).toBe(false);
        expect(addButton.dataset.protocolItemFormUrl).toBe('protocol_item_form.html?apiMode=mock&projectId=2');
        expect(targetUrl).toBe('protocol_item_form.html?apiMode=mock&projectId=2');
        expect(context.document.querySelector('#protocol-service-meta .status-inactive').textContent.trim()).toBe('未开启');
    });

    /**
     * 测试思路：协议项管理页分页条要支持切换每页数量，并用 pageSize+1 请求下一页探测数据。
     * 示例：选择 5 条/页后，页面状态 pageSize=5，列表请求 limit=6。
     */
    it('协议项管理页分页条支持每页数量切换', async () => {
        const context = createProtocolItemPageContext(1);

        loadCoreScripts(context);
        await loginMockUser(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        context.delay = function delayImmediately() {
            return Promise.resolve();
        };
        loadProtocolListScripts(context);

        const getProtocolList = vi.spyOn(context.KitProxy.api, 'getProtocolList');
        await context.KitProxy.protocolItemsPage.initPage?.();
        await flushPromises(12);

        const select = context.document.querySelector('#protocol-pagination .pagination-page-size');
        expect(select).toBeTruthy();
        expect(Array.from(select.options).map(option => option.value)).toEqual(['5', '10', '20', '50']);

        select.value = '5';
        select.dispatchEvent(new context.Event('change', { bubbles: true }));
        await flushPromises(12);

        expect(context.KitProxy.protocolItemsPage.pageState.pageSize).toBe(5);
        expect(getProtocolList).toHaveBeenLastCalledWith(1, 0, 6, {});
    });

    /**
     * 测试思路：协议项卡片默认只展示摘要，详情展开和编辑跳转都应由明确按钮触发。
     * 示例：初始 aria-expanded=false，点击展开按钮后变 true，点击修改按钮生成带 protocolId 的表单 URL。
     */
    it('协议项卡片默认折叠，支持展开收起和修改跳转', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitMain = true;
        runScript(context, 'js/main.js');

        const root = context.document.createElement('div');
        root.className = 'protocol-items-page';
        root.id = 'service-card-1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);

        const protocolItem = context.addProtocolItem(root, {
            id: 1,
            name: '接口1',
            project_id: 1,
            type: 'HTTP',
            req_cfg: { method: 'GET', path: '/api/test1' },
            resp_cfg: {},
            req_body_status: 0,
            resp_body_status: 0,
            ctime: '2025-12-02 06:01:03',
            utime: '2025-12-02 06:01:03',
        });

        const details = protocolItem.querySelector('.protocol-details');
        const toggleButton = protocolItem.querySelector('.protocol-toggle-btn');
        expect(details.classList.contains('is-expanded')).toBe(false);
        expect(toggleButton.querySelector('.protocol-toggle-icon')).toBeTruthy();
        expect(toggleButton.getAttribute('aria-label')).toBe('展开协议项详情');
        expect(toggleButton.getAttribute('aria-expanded')).toBe('false');
        expect(protocolItem.querySelector('.delete-protocol-btn')).toBeTruthy();
        expect(readRepoFile('css/main.css')).toContain('chevron-down.svg');
        expect(repoFileExists('assets/icons/chevron-down.svg')).toBe(true);

        toggleButton.click();
        expect(details.classList.contains('is-expanded')).toBe(true);
        expect(toggleButton.getAttribute('aria-label')).toBe('收起协议项详情');
        expect(toggleButton.getAttribute('aria-expanded')).toBe('true');

        toggleButton.click();
        expect(details.classList.contains('is-expanded')).toBe(false);
        expect(toggleButton.getAttribute('aria-expanded')).toBe('false');

        let targetUrl = '';
        protocolItem.addEventListener('protocol-item:navigate-form', event => {
            event.preventDefault();
            targetUrl = event.detail.url;
        });
        protocolItem.querySelector('.edit-protocol-btn').click();
        expect(protocolItem.dataset.protocolItemFormUrl).toBe('protocol_item_form.html?apiMode=mock&projectId=1&protocolId=1');
        expect(targetUrl).toBe('protocol_item_form.html?apiMode=mock&projectId=1&protocolId=1');
    });

    /**
     * 测试思路：协议卡片 config_state=0 且项目 running 时，状态按钮应调用 setProtocolRuntime(true)。
     * 示例：未上线协议点击“未上线”后，按钮状态跟随后端返回 config_state=1 刷新为“已上线”。
     */
    it('协议卡片未上线状态点击调用 setProtocolRuntime(true)', async () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitMain = true;
        runScript(context, 'js/main.js');

        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.dataset.runtimeState = '1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);

        const setProtocolRuntime = vi.spyOn(context.KitProxy.api, 'setProtocolRuntime')
            .mockResolvedValue({ protocol_id: 1, config_state: 1 });
        const protocolItem = context.addProtocolItem(root, {
            id: 1,
            name: '接口1',
            project_id: 1,
            type: 'HTTP',
            config_state: 0,
            req_cfg: { method: 'GET', path: '/api/test1' },
            resp_cfg: {},
            req_body_status: 0,
            resp_body_status: 0,
            ctime: '2025-12-02 06:01:03',
            utime: '2025-12-02 06:01:03',
        });

        protocolItem.querySelector('.protocol-runtime-btn').click();
        await flushPromises(8);

        expect(setProtocolRuntime).toHaveBeenCalledWith(1, true);
        expect(protocolItem.dataset.configState).toBe('1');
        expect(protocolItem.querySelector('.protocol-runtime-btn').textContent).toBe('已上线');
    });

    /**
     * 测试思路：运行态命令成功后，协议项列表刷新可能短时间返回旧的 config_state，页面应优先展示命令确认状态。
     * 示例：点击“未上线”后 setProtocolRuntime 返回 config_state=1，但 getProtocolList 仍返回 config_state=0，按钮仍保持“已上线”。
     */
    it('协议项页上线成功后列表旧状态不覆盖按钮状态', async () => {
        const context = createProtocolItemPageContext(1);

        loadCoreScripts(context);
        await loginMockUser(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        context.delay = function delayImmediately() {
            return Promise.resolve();
        };
        loadProtocolListScripts(context);

        const staleProtocol = {
            id: 10,
            name: '接口1',
            project_id: 1,
            type: 'HTTP',
            config_state: 0,
            req_cfg: { method: 'GET', path: '/api/test1' },
            resp_cfg: {},
            req_body_status: 0,
            resp_body_status: 0,
            ctime: '2025-12-02 06:01:03',
            utime: '2025-12-02 06:01:03',
        };
        vi.spyOn(context.KitProxy.api, 'getProtocolList').mockResolvedValue([staleProtocol]);
        const setProtocolRuntime = vi.spyOn(context.KitProxy.api, 'setProtocolRuntime')
            .mockResolvedValue({ protocol_id: 10, config_state: 1, persisted: 1, runtime_applied: 1 });

        await context.KitProxy.protocolItemsPage.initPage?.();
        await flushPromises(12);

        expect(context.document.querySelector('.protocol-runtime-btn').textContent).toBe('未上线');

        context.document.querySelector('.protocol-runtime-btn').click();
        await flushPromises(12);

        const runtimeButton = context.document.querySelector('.protocol-runtime-btn');
        expect(setProtocolRuntime).toHaveBeenCalledWith(10, true);
        expect(context.document.querySelector('.protocol-item').dataset.configState).toBe('1');
        expect(runtimeButton.textContent).toBe('已上线');
    });

    /**
     * 测试思路：协议卡片 config_state=1 且项目 running 时，状态按钮应调用 setProtocolRuntime(false)。
     * 示例：已上线协议点击“已上线”后，按钮状态跟随后端返回 config_state=0 刷新为“未上线”。
     */
    it('协议卡片已上线状态点击调用 setProtocolRuntime(false)', async () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitMain = true;
        runScript(context, 'js/main.js');

        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.dataset.runtimeState = '1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);

        const setProtocolRuntime = vi.spyOn(context.KitProxy.api, 'setProtocolRuntime')
            .mockResolvedValue({ protocol_id: 1, config_state: 0 });
        const protocolItem = context.addProtocolItem(root, {
            id: 1,
            name: '接口1',
            project_id: 1,
            type: 'HTTP',
            config_state: 1,
            req_cfg: { method: 'GET', path: '/api/test1' },
            resp_cfg: {},
            req_body_status: 0,
            resp_body_status: 0,
            ctime: '2025-12-02 06:01:03',
            utime: '2025-12-02 06:01:03',
        });

        protocolItem.querySelector('.protocol-runtime-btn').click();
        await flushPromises(8);

        expect(setProtocolRuntime).toHaveBeenCalledWith(1, false);
        expect(protocolItem.dataset.configState).toBe('0');
        expect(protocolItem.querySelector('.protocol-runtime-btn').textContent).toBe('未上线');
    });

    /**
     * 测试思路：协议项管理页切换项目运行态后，已渲染卡片的上线/下线按钮也要同步禁用状态。
     * 示例：running 服务下未上线协议按钮可点击，停止服务后按钮应置灰且不再调用 setProtocolRuntime。
     */
    it('协议项页停止项目后同步置灰协议上线下线按钮', async () => {
        const context = createProtocolItemPageContext(1);

        loadCoreScripts(context);
        await loginMockUser(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        context.delay = function delayImmediately() {
            return Promise.resolve();
        };
        loadProtocolListScripts(context);

        vi.spyOn(context.KitProxy.api, 'getProtocolList').mockResolvedValue([
            {
                id: 10,
                name: '未上线接口',
                project_id: 1,
                type: 'HTTP',
                config_state: 0,
                req_cfg: { method: 'GET', path: '/api/offline' },
                resp_cfg: {},
                req_body_status: 0,
                resp_body_status: 0,
                ctime: '2025-12-02 06:01:03',
                utime: '2025-12-02 06:01:03',
            },
        ]);
        vi.spyOn(context.KitProxy.api, 'setProjectRuntimeState')
            .mockResolvedValue({ runtime_state: 0, listen_port: 0 });
        const setProtocolRuntime = vi.spyOn(context.KitProxy.api, 'setProtocolRuntime');

        await context.KitProxy.protocolItemsPage.initPage?.();
        await flushPromises(12);

        const runtimeButton = context.document.querySelector('.protocol-runtime-btn');
        expect(runtimeButton.disabled).toBe(false);

        context.document.querySelector('#protocol-service-meta .service-active-toggle').click();
        await flushPromises(12);

        expect(context.document.querySelector('.protocol-items-page').dataset.runtimeState).toBe('0');
        expect(runtimeButton.disabled).toBe(true);
        runtimeButton.click();
        await flushPromises(4);
        expect(setProtocolRuntime).not.toHaveBeenCalled();
    });

    /**
     * 测试思路：TCP 项目格式修改会让该服务下协议项进入待重配置，保存成功后列表必须立即刷新。
     * 示例：projectId=2 保存 TCP 格式后，列表里的 TCP 协议项按钮从旧状态刷新为“待重配置”。
     */
    it('协议项页 TCP 格式保存成功后刷新列表为待重配置状态', async () => {
        const context = createProtocolItemPageContext(2);

        loadCoreScripts(context);
        await loginMockUser(context, {
            note: 'admin',
            loginType: 'admin',
            password: 'admin123',
        });
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        context.delay = function delayImmediately() {
            return Promise.resolve();
        };
        context.confirm = () => true;
        loadProtocolListScripts(context);

        await context.KitProxy.protocolItemsPage.initPage?.();
        await flushPromises(12);

        const firstRuntimeButton = context.document.querySelector('.protocol-runtime-btn');
        expect(firstRuntimeButton.textContent).toBe('未上线');

        const loadProtocolItems = vi.spyOn(context.KitProxy.protocolItemsPage, 'loadProtocolItems');
        context.document.querySelector('.project-pattern').click();
        await flushPromises(12);

        const modal = context.document.querySelector('.config-pattern-modal');
        expect(modal).toBeTruthy();
        modal.querySelector('#config-pattern-modal-form')
            .dispatchEvent(new context.Event('submit', { bubbles: true, cancelable: true }));
        await flushPromises(20);

        expect(loadProtocolItems).toHaveBeenCalled();
        const runtimeTexts = Array.from(context.document.querySelectorAll('.protocol-runtime-btn'))
            .map(button => button.textContent);
        expect(runtimeTexts).toContain('待重配置');
        expect(context.document.querySelector('#protocol-item-2 .protocol-runtime-btn').textContent).toBe('待重配置');
    });

    /**
     * 测试思路：协议卡片 config_state=2 表示待重配置，应直接跳转 mode=reconfig 页面。
     * 示例：无论项目是否 running，点击“待重配置”都生成 protocol_item_form.html?...&mode=reconfig。
     */
    it('协议卡片待重配置状态点击跳转 reconfig URL', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitMain = true;
        runScript(context, 'js/main.js');

        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.dataset.runtimeState = '0';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);

        const protocolItem = context.addProtocolItem(root, {
            id: 2,
            name: '待重配置接口',
            project_id: 1,
            type: 'HTTP',
            config_state: 2,
            req_cfg: { method: 'GET', path: '/api/reconfig' },
            resp_cfg: {},
            req_body_status: 0,
            resp_body_status: 0,
            ctime: '2025-12-02 06:01:03',
            utime: '2025-12-02 06:01:03',
        });

        let targetUrl = '';
        protocolItem.addEventListener('protocol-item:navigate-reconfig', event => {
            event.preventDefault();
            targetUrl = event.detail.url;
        });
        protocolItem.querySelector('.protocol-runtime-btn').click();

        expect(protocolItem.dataset.protocolItemFormUrl).toBe('protocol_item_form.html?apiMode=mock&projectId=1&protocolId=2&mode=reconfig');
        expect(targetUrl).toBe('protocol_item_form.html?apiMode=mock&projectId=1&protocolId=2&mode=reconfig');
    });

    /**
     * 测试思路：注册表创建的详情网格应是纯展示结构，编辑行为由协议项卡片层绑定。
     * 示例：直接点击 method/path/body/fields 网格单元，不应创建 modal-overlay。
     */
    it('协议项详情网格本身不附加编辑 modal 副作用', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=2');
        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));

        const httpGrid = context.ProtocolTypeRegistry.createProtocolItemGrid({
            id: 1,
            type: 'HTTP',
            req_cfg: { method: 'GET', path: '/api/test' },
            resp_cfg: {},
            req_body_status: 1,
            resp_body_status: 0,
        });
        context.document.body.appendChild(httpGrid);
        httpGrid.querySelector('[data-field-name="method"]').click();
        httpGrid.querySelector('[data-field-name="path"]').click();
        httpGrid.querySelector('.request-body').click();
        expect(context.document.querySelector('.modal-overlay')).toBeNull();

        const tcpGrid = context.ProtocolTypeRegistry.createProtocolItemGrid({
            id: 2,
            type: 'TCP',
            req_cfg: {
                function_code: 'H1000',
                fields: { 4: 'H00000001' },
            },
            resp_cfg: {
                function_code: 'H1080',
                fields: {},
            },
            req_body_status: 0,
            resp_body_status: 0,
        });
        context.document.body.appendChild(tcpGrid);
        expect(tcpGrid.querySelector('[data-field-name="function_code"]')).toBeNull();
        expect(tcpGrid.textContent).toContain('请求头部字段值');
        expect(tcpGrid.textContent).toContain('响应头部字段值');
        expect(tcpGrid.querySelectorAll('[data-field-name="fields"]').length).toBe(2);
        tcpGrid.querySelector('[data-field-name="fields"]').click();
        expect(context.document.querySelector('.modal-overlay')).toBeNull();
    });

    /**
     * 测试思路：协议项卡片层绑定 Body 编辑行为，点击 Body 字段应加载详情并打开 Body Editor 弹窗。
     * 示例：req_body_status=1 的 request-body 字段点击后，应出现 edit-body-modal 和 body-editor-textarea。
     */
    it('协议项 Body 字段点击会重新弹出编辑 modal', async () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitMain = true;
        runScript(context, 'js/main.js');
        context.delay = function delayImmediately() {
            return Promise.resolve();
        };

        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);

        const protocolItem = context.addProtocolItem(root, {
            id: 1,
            name: '接口1',
            project_id: 1,
            type: 'HTTP',
            req_cfg: { method: 'GET', path: '/api/test1' },
            resp_cfg: {},
            req_body_status: 1,
            resp_body_status: 0,
            ctime: '2025-12-02 06:01:03',
            utime: '2025-12-02 06:01:03',
        });

        protocolItem.querySelector('.request-body').click();
        await flushPromises(12);

        expect(context.document.querySelector('.edit-body-modal')).toBeTruthy();
        expect(context.document.querySelector('.body-editor-textarea')).toBeTruthy();
    });

    /**
     * 测试思路：协议卡片 Body 弹窗和独立协议项表单应使用同一套 BodyEditor 字体变量。
     * 示例：protocol-body-content 不能被普通弹窗 textarea 的 monospace/resize 样式覆盖，应和 protocol-form-body-content 一样使用 --body-editor-font-size、--body-editor-line-height。
     */
    it('协议卡片 Body 弹窗字体样式和表单页 BodyEditor 对齐', () => {
        const mainCss = readRepoFile('css/main.css');
        const modalCss = readRepoFile('css/modal_styles.css');

        expect(mainCss).toContain('.protocol-body-section .body-editor-input-wrap');
        expect(modalCss).toContain('.edit-body-modal .body-editor-input-wrap');
        expect(modalCss).toContain('--body-editor-font-size: 14px;');
        expect(modalCss).toContain('--body-editor-line-height: 20px;');
        expect(modalCss).toContain('.edit-body-modal .body-editor-lines,\n.edit-body-modal .body-editor-highlight,\n.edit-body-modal .body-editor-textarea');
        expect(modalCss).toContain('font-size: var(--body-editor-font-size);');
        expect(modalCss).toContain('line-height: var(--body-editor-line-height);');
        expect(modalCss).toContain('resize: none;');
    });

    /**
     * 测试思路：HTTP 简单配置字段应使用行内编辑，不再弹出旧的 method/path/status modal。
     * 示例：把 method 改成 POST、path 改成 /api/changed、status 改成 201 后，卡片值同步更新。
     */
    it('协议项 HTTP method/path/status 字段点击使用行内编辑', async () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitMain = true;
        runScript(context, 'js/main.js');
        context.alert = vi.fn();

        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);

        const protocolItem = context.addProtocolItem(root, {
            id: 1,
            name: '接口1',
            project_id: 1,
            type: 'HTTP',
            req_cfg: { method: 'GET', path: '/api/test1' },
            resp_cfg: { status_code: 200 },
            req_body_status: 0,
            resp_body_status: 0,
            ctime: '2025-12-02 06:01:03',
            utime: '2025-12-02 06:01:03',
        });

        protocolItem.querySelector('[data-field-name="method"]').click();
        expect(context.document.querySelector('.edit-method-modal')).toBeNull();
        expect(protocolItem.querySelector('[data-field-name="method"] .inline-field-editor')).toBeTruthy();
        protocolItem.querySelector('[data-field-name="method"] .inline-field-control').value = 'POST';
        protocolItem.querySelector('[data-field-name="method"] .inline-field-save').click();
        await flushPromises(8);
        expect(protocolItem.querySelector('[data-field-name="method"] .value').textContent).toBe('POST');

        protocolItem.querySelector('[data-field-name="path"]').click();
        expect(context.document.querySelector('.edit-path-modal')).toBeNull();
        protocolItem.querySelector('[data-field-name="path"] .inline-field-control').value = '/api/changed';
        protocolItem.querySelector('[data-field-name="path"] .inline-field-save').click();
        await flushPromises(8);
        expect(protocolItem.querySelector('[data-field-name="path"] .value').textContent).toBe('/api/changed');
        expect(protocolItem.querySelector('[data-field-name="path"] .value').getAttribute('title')).toBe('/api/changed');

        protocolItem.querySelector('[data-field-name="status_code"]').click();
        protocolItem.querySelector('[data-field-name="status_code"] .inline-field-control').value = '201';
        protocolItem.querySelector('[data-field-name="status_code"] .inline-field-save').click();
        await flushPromises(8);
        expect(protocolItem.querySelector('[data-field-name="status_code"] .value').textContent).toBe('201');
    });

    /**
     * 测试思路：HTTP 卡片详情应复用表单页 Headers 配置能力，并按请求/响应分两行展示。
     * 示例：请求行包含 method/path/请求 Headers/请求 Body，响应行包含 status/响应 Headers/响应 Body；点击请求 Headers 保存后只更新 req_cfg.headers。
     */
    it('协议项 HTTP 卡片展示并保存请求和响应 Headers', async () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitMain = true;
        runScript(context, 'js/main.js');

        const updateCfg = vi.spyOn(context.KitProxy.api, 'updateProtocolCfg');
        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);

        const protocolItem = context.addProtocolItem(root, {
            id: 1,
            name: '接口1',
            project_id: 1,
            type: 'HTTP',
            req_cfg: {
                method: 'GET',
                path: '/api/test1',
                headers: { 'X-Req': 'old' },
            },
            resp_cfg: {
                status_code: 200,
                headers: { 'X-Resp': 'ok' },
            },
            req_body_status: 0,
            resp_body_status: 1,
            ctime: '2025-12-02 06:01:03',
            utime: '2025-12-02 06:01:03',
        });

        const grid = protocolItem.querySelector('.details-grid.http');
        expect(grid.querySelectorAll('.http-details-row')).toHaveLength(2);
        expect(grid.querySelector('.http-details-row-title')).toBeNull();
        expect(grid.querySelector('.http-request-config').querySelectorAll('.protocol-field')).toHaveLength(4);
        expect(grid.querySelector('.http-response-config').querySelectorAll('.protocol-field')).toHaveLength(4);
        expect(grid.querySelector('.http-response-config .http-empty-slot')).toBeTruthy();
        expect(grid.querySelector('.http-request-config').textContent).toContain('请求 Headers');
        expect(grid.querySelector('.http-response-config').textContent).toContain('响应 Headers');
        const mainCss = readRepoFile('css/main.css');
        expect(mainCss).toContain('.http-details-row');
        expect(mainCss).toContain('grid-template-columns: repeat(4, minmax(0, 1fr));');
        expect(mainCss).toContain('.details-grid.http .protocol-field.http-empty-slot');
        expect(mainCss).not.toContain('.http-details-row-title');
        expect(grid.querySelector('[data-http-headers-side="request"] .value').textContent).toBe('已设置 1 条');
        expect(grid.querySelector('[data-http-headers-side="response"] .value').textContent).toBe('已设置 1 条');

        grid.querySelector('[data-http-headers-side="request"]').click();
        const modal = context.document.querySelector('.http-headers-modal');
        expect(modal).toBeTruthy();
        expect(modal.querySelector('.modal-header').textContent).toContain('配置请求 Headers');
        modal.querySelector('.http-header-name').value = 'X-Req-New';
        modal.querySelector('.http-header-value').value = 'new';
        modal.querySelector('.confirm-btn').click();
        await flushPromises(8);

        expect(updateCfg).toHaveBeenCalledWith(1, 1, 1, {
            headers: { 'X-Req-New': 'new' },
        });
        expect(context.document.querySelector('.http-headers-modal')).toBeNull();
        expect(grid.querySelector('[data-http-headers-side="request"] .value').textContent).toBe('已设置 1 条');
        expect(JSON.parse(grid.querySelector('[data-http-headers-side="request"]').dataset.headers)).toEqual({
            'X-Req-New': 'new',
        });
    });

    /**
     * 测试思路：服务 active=0 只影响运行态，不应阻止维护 TCP 协议项头部字段值。
     * 示例：未开启的 TCP 服务仍能打开字段值弹窗，修改功能码和普通字段后调用 updateProtocolCfg。
     */
    it('协议项未开启服务时仍允许配置 TCP 头部字段值', async () => {
        const context = createBrowserContext('?apiMode=mock&projectId=2');
        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitMain = true;
        runScript(context, 'js/main.js');
        context.alert = vi.fn();
        context.delay = function delayImmediately() {
            return Promise.resolve();
        };
        const updateCfg = vi.spyOn(context.KitProxy.api, 'updateProtocolCfg');
        vi.spyOn(context.KitProxy.api, 'getProtocolDetailsCfg').mockResolvedValue({
            req_cfg: {
                function_code: 'H1000',
                fields: { 4: 'H00000001' },
            },
            resp_cfg: {
                function_code: 'H1080',
                fields: {},
            },
        });

        const root = context.document.createElement('div');
        root.id = 'service-card-2';
        root.dataset.active = '0';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);

        const protocolItem = context.addProtocolItem(root, {
            id: 3,
            name: 'TCP接口',
            project_id: 2,
            type: 'TCP',
            req_cfg: {
                function_code: 'H1000',
                fields: { 4: 'H00000001' },
            },
            resp_cfg: {
                function_code: 'H1080',
                fields: {},
            },
            req_body_status: 0,
            resp_body_status: 0,
            ctime: '2025-12-02 06:01:03',
            utime: '2025-12-02 06:01:03',
        });

        expect(protocolItem.querySelector('[data-field-name="function_code"]')).toBeNull();
        expect(protocolItem.querySelector('[data-field-name="fields"] .value').textContent).toBe('已设置 2 个');

        protocolItem.querySelector('[data-field-name="fields"]').click();
        await flushPromises(12);
        const modal = context.document.querySelector('.config-pattern-modal');
        expect(modal).toBeTruthy();
        expect(modal.querySelector('.pattern-field-info-header').textContent).toContain('请求头部字段值');

        const fieldNodes = Array.from(modal.querySelectorAll('.pattern-field-container'));
        const functionNode = fieldNodes.find(node => node.querySelector('.pattern-field-role')?.value === 'function_code');
        const commonNode = fieldNodes.find(node => Number(node.querySelector('.pattern-field-byte-pos')?.value) === 4);
        const startNode = fieldNodes.find(node => node.querySelector('.pattern-field-role')?.value === 'start_magic');
        const lengthNode = fieldNodes.find(node => node.querySelector('.pattern-field-role')?.value === 'body_length');
        expect(functionNode).toBeTruthy();
        expect(commonNode).toBeTruthy();
        expect(startNode).toBeTruthy();
        expect(lengthNode).toBeTruthy();
        expect(startNode.querySelector('.pattern-value-editor-input').disabled).toBe(true);
        expect(lengthNode.querySelector('.pattern-value-editor-input').disabled).toBe(true);
        expect(functionNode.querySelector('.pattern-value-editor-input').disabled).toBe(false);
        expect(commonNode.querySelector('.pattern-value-editor-input').disabled).toBe(false);
        const previewText = Array.from(modal.querySelectorAll('.pattern-byte-block'))
            .map(block => block.textContent)
            .join('\n');
        expect(previewText).toContain('H23232323');
        expect(previewText).toContain('H1000');
        expect(previewText).toContain('H00000001');
        expect(previewText).not.toContain('UINT32 · 4 Byte');

        functionNode.querySelector('.pattern-value-editor-input').value = '';
        functionNode.querySelector('.pattern-value-editor-input').dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(modal.querySelector('.pattern-summary-item.is-error').textContent).toContain('校验状态');
        expect(modal.querySelector('.pattern-validation-errors').textContent).toContain('功能码必须配置');

        functionNode.querySelector('.pattern-value-editor-input').value = '2000';
        functionNode.querySelector('.pattern-value-editor-input').dispatchEvent(new context.Event('input', { bubbles: true }));
        commonNode.querySelector('.pattern-value-editor-input').value = '00000002';
        commonNode.querySelector('.pattern-value-editor-input').dispatchEvent(new context.Event('input', { bubbles: true }));
        expect(modal.querySelector('.pattern-summary-item.is-ok').textContent).toContain('校验状态');
        expect(modal.querySelector('.pattern-validation-errors').style.display).toBe('none');
        modal.querySelector('#config-pattern-modal-form')
            .dispatchEvent(new context.Event('submit', { bubbles: true, cancelable: true }));
        await flushPromises(12);

        expect(updateCfg).toHaveBeenCalledWith(3, 2, 1, {
            function_code: 'H2000',
            fields: {
                4: 'H00000002',
            },
        });
        expect(protocolItem.querySelector('[data-field-name="fields"] .value').textContent).toBe('已设置 2 个');
        expect(context.alert).not.toHaveBeenCalledWith('请先开启测试服务，再执行该操作');
    });

    /**
     * 测试思路：HTTP 表单在请求/响应 Body tab 之间切换时，要分别保存 Body 内容和类型。
     * 示例：请求 Body 保持 JSON，切到响应后改为 text=ok，最终 payload 分别输出 request_body 和 response_body。
     */
    it('表单页新增 HTTP payload 与 Body 切换状态保持一致', async () => {
        const context = createProtocolItemFormContext('?apiMode=mock&projectId=1');

        loadCoreScripts(context);
        await loginMockUser(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitProtocolItemForm = true;
        runScript(context, 'js/protocol_item_form.js');

        const state = context.KitProxy.protocolItemForm.pageState;
        state.projectId = 1;
        state.project = context.KitProxy.mocks.state.projects.find(project => project.id === 1);
        state.protocolType = context.ProtocolType.HTTP;
        context.KitProxy.protocolItemForm.initPage();

        return flushPromises(8).then(() => {
            context.document.getElementById('protocol-item-name').value = '新增HTTP';
            context.document.querySelector('input[name="request-method"][value="POST"]').checked = true;
            context.document.getElementById('request-path').value = '/api/new';
            expect(context.document.querySelectorAll('.protocol-config-card')).toHaveLength(2);

            context.document.getElementById('req-http-headers').click();
            let headersModal = context.document.querySelector('.http-headers-modal');
            expect(headersModal).toBeTruthy();
            expect(headersModal.querySelector('.modal-header').textContent).toContain('配置请求 Headers');
            headersModal.querySelector('.add-http-header-btn').click();
            headersModal.querySelector('.http-header-name').value = 'X-Req';
            headersModal.querySelector('.http-header-value').value = '1';
            headersModal.querySelector('.confirm-btn').click();
            expect(context.document.querySelector('.http-headers-modal')).toBeNull();

            context.document.getElementById('resp-http-headers').click();
            headersModal = context.document.querySelector('.http-headers-modal');
            expect(headersModal).toBeTruthy();
            expect(headersModal.querySelector('.modal-header').textContent).toContain('配置响应 Headers');
            headersModal.querySelector('.add-http-header-btn').click();
            headersModal.querySelector('.http-header-name').value = 'X-Resp';
            headersModal.querySelector('.http-header-value').value = '2';
            headersModal.querySelector('.confirm-btn').click();
            expect(context.document.querySelector('.http-headers-modal')).toBeNull();

            const editor = state.bodyEditor;
            editor.setValue('{"req":true}');
            context.KitProxy.protocolItemForm.setActiveBodyTab('response');
            editor.setType('text');
            editor.setValue('ok');

            const data = context.KitProxy.protocolItemForm.collectFormData();
            const payload = context.KitProxy.protocolItemForm.buildAddPayload(data);

            expect(payload.cfg_header.type).toBe('HTTP');
            expect(payload.cfg_header.config_state).toBe(0);
            expect(payload.req_cfg).toEqual({
                method: 'POST',
                path: '/api/new',
                headers: { 'X-Req': '1' },
            });
            expect(payload.resp_cfg).toEqual({
                status_code: '200',
                headers: { 'X-Resp': '2' },
            });
            expect(payload.cfg_header.req_body_type).toBe('json');
            expect(payload.cfg_header.resp_body_type).toBe('text');
            expect(payload.request_body).toBe('{"req":true}');
            expect(payload.response_body).toBe('ok');
        });
    });

    /**
     * 测试思路：TCP 协议项表单提交新结构，只输出 { function_code, fields }。
     * 示例：功能码在“头部字段值”中填写为 H1000，普通字段 byte_pos=4 的值为 H00000209，payload 不应含旧 function_code_filed_value/common_fields。
     */
    it('表单页新增 TCP payload 输出 function_code 和 fields', async () => {
        const context = createProtocolItemFormContext('?apiMode=mock&projectId=2');

        loadCoreScripts(context);
        await loginMockUser(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitProtocolItemForm = true;
        runScript(context, 'js/protocol_item_form.js');

        await context.KitProxy.protocolItemForm.initPage();
        await flushPromises(8);

        const state = context.KitProxy.protocolItemForm.pageState;
        context.document.getElementById('protocol-item-name').value = '新增TCP';
        expect(context.document.getElementById('req-function-code-value')).toBeNull();
        expect(context.document.getElementById('resp-function-code-value')).toBeNull();
        expect(context.document.getElementById('protocol-type-fields').textContent).toContain('校验请求头部字段值');
        expect(context.document.getElementById('protocol-type-fields').textContent).toContain('目标响应头部字段值');

        const reqFunctionField = state.reqPatternFields.find(field => field.role === 'function_code');
        const respFunctionField = state.respPatternFields.find(field => field.role === 'function_code');
        const reqCommonField = state.reqPatternFields.find(field => field.role === 'common' && Number(field.byte_pos) === 4);
        const reqStartField = state.reqPatternFields.find(field => field.role === 'start_magic');
        const reqLengthField = state.reqPatternFields.find(field => field.role === 'body_length');
        expect(reqFunctionField).toBeTruthy();
        expect(respFunctionField).toBeTruthy();
        expect(reqCommonField).toBeTruthy();
        expect(reqStartField.value).toBe('H23232323');
        expect(reqStartField.value_editable).toBe(false);
        expect(reqLengthField.value_editable).toBe(false);

        reqFunctionField.value = 'H1000';
        respFunctionField.value = 'H1080';
        reqCommonField.value = 'H00000209';

            const data = context.KitProxy.protocolItemForm.collectFormData();
            const payload = context.KitProxy.protocolItemForm.buildAddPayload(data);

            expect(payload.cfg_header.type).toBe('TCP');
            expect(payload.cfg_header.config_state).toBe(0);
            expect(payload.req_cfg).toEqual({
            function_code: 'H1000',
            fields: { 4: 'H00000209' },
        });
        expect(payload.resp_cfg).toEqual({
            function_code: 'H1080',
            fields: {},
        });
        expect(payload.req_cfg).not.toHaveProperty('function_code_filed_value');
        expect(payload.req_cfg).not.toHaveProperty('common_fields');
    });

    /**
     * 测试思路：新增页主按钮“保存”只能提交保存态，不应自动上线。
     * 示例：填写 HTTP 协议项后提交表单，addProtocol 收到 cfg_header.config_state=0。
     */
    it('表单页新增默认保存提交 config_state=0', async () => {
        const context = createProtocolItemFormContext('?apiMode=mock&projectId=1');

        loadCoreScripts(context);
        await loginMockUser(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitProtocolItemForm = true;
        runScript(context, 'js/protocol_item_form.js');

        const addProtocol = vi.spyOn(context.KitProxy.api, 'addProtocol').mockResolvedValue({ protocol_id: 99 });
        context.document.addEventListener('protocol-item-form:navigate-back', event => {
            event.preventDefault();
        });

        await context.KitProxy.protocolItemForm.initPage();
        await flushPromises(8);

        context.document.getElementById('protocol-item-name').value = '保存协议';
        context.document.getElementById('request-path').value = '/api/save';
        context.document.getElementById('protocol-item-form')
            .dispatchEvent(new context.Event('submit', { bubbles: true, cancelable: true }));
        await flushPromises(12);

        expect(addProtocol).toHaveBeenCalledTimes(1);
        expect(addProtocol.mock.calls[0][0].cfg_header.config_state).toBe(0);
    });

    /**
     * 测试思路：running 项目允许点击“保存并上线”，且只通过 AddProtocol(config_state=1) 一步完成。
     * 示例：projectId=1 runtime_state=1，点击下拉项后 addProtocol 收到 config_state=1。
     */
    it('表单页 running 项目保存并上线提交 config_state=1', async () => {
        const context = createProtocolItemFormContext('?apiMode=mock&projectId=1');

        loadCoreScripts(context);
        await loginMockUser(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitProtocolItemForm = true;
        runScript(context, 'js/protocol_item_form.js');

        const addProtocol = vi.spyOn(context.KitProxy.api, 'addProtocol').mockResolvedValue({ protocol_id: 100 });
        const setProtocolRuntime = vi.spyOn(context.KitProxy.api, 'setProtocolRuntime');
        context.document.addEventListener('protocol-item-form:navigate-back', event => {
            event.preventDefault();
        });

        await context.KitProxy.protocolItemForm.initPage();
        await flushPromises(8);

        context.document.getElementById('protocol-item-name').value = '上线协议';
        context.document.getElementById('request-path').value = '/api/online';
        expect(context.document.getElementById('save-and-online-protocol').disabled).toBe(false);
        context.document.getElementById('save-and-online-protocol').click();
        await flushPromises(12);

        expect(addProtocol).toHaveBeenCalledTimes(1);
        expect(addProtocol.mock.calls[0][0].cfg_header.config_state).toBe(1);
        expect(setProtocolRuntime).not.toHaveBeenCalled();
    });

    /**
     * 测试思路：stopped 项目不能“保存并上线”，按钮应置灰且不触发新增请求。
     * 示例：projectId=2 runtime_state=0，点击保存并上线按钮后 addProtocol 不应被调用。
     */
    it('表单页 stopped 项目保存并上线置灰', async () => {
        const context = createProtocolItemFormContext('?apiMode=mock&projectId=2');

        loadCoreScripts(context);
        await loginMockUser(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitProtocolItemForm = true;
        runScript(context, 'js/protocol_item_form.js');

        const addProtocol = vi.spyOn(context.KitProxy.api, 'addProtocol');
        await context.KitProxy.protocolItemForm.initPage();
        await flushPromises(8);

        const saveAndOnline = context.document.getElementById('save-and-online-protocol');
        expect(saveAndOnline.disabled).toBe(true);
        expect(saveAndOnline.title).toContain('项目未运行');
        saveAndOnline.click();
        await flushPromises(4);

        expect(addProtocol).not.toHaveBeenCalled();
    });

    /**
     * 测试思路：TCP item cfg 校验必须按项目 Pattern 中功能码和普通字段 byte_len 执行。
     * 示例：功能码少 1 字节、普通字段 byte_pos=4 少 1 字节，都应失败。
     */
    it('TCP item cfg 校验拦截功能码和普通字段长度不匹配', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        runScript(context, 'js/tcp_pattern_modal.js');
        const patternInfo = context.KitProxy.mocks.state.patternInfos[2];

        const validation = context.KitProxy.tcpPatternEditor.validateTcpItemCfg(patternInfo, {
            function_code: 'H10',
            fields: { 4: 'H0209' },
        });

        expect(validation.valid).toBe(false);
        expect(validation.errors.join('\n')).toContain('功能码字节数必须等于 2');
        expect(validation.errors.join('\n')).toContain('字段值字节数必须等于 4');
    });

    /**
     * 测试思路：编辑协议项时只提交发生变化的名称、配置或 Body，避免无意义更新。
     * 示例：只修改名称和请求 path 时，应调用 updateProtocolName 和一次 request cfg 更新，不调用 updateProtocolBody。
     */
    it('表单页编辑模式只提交变更字段', async () => {
        const context = createProtocolItemFormContext('?apiMode=mock&projectId=1&protocolId=1');

        loadCoreScripts(context);
        await loginMockUser(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitProtocolItemForm = true;
        runScript(context, 'js/protocol_item_form.js');

        const updateName = vi.spyOn(context.KitProxy.api, 'updateProtocolName');
        const updateCfg = vi.spyOn(context.KitProxy.api, 'updateProtocolCfg');
        const updateBody = vi.spyOn(context.KitProxy.api, 'updateProtocolBody');

        await context.KitProxy.protocolItemForm.initPage();
        await flushPromises(8);

        context.document.addEventListener('protocol-item-form:navigate-back', event => {
            event.preventDefault();
        });

        context.document.getElementById('protocol-item-name').value = 'test1修改';
        context.document.getElementById('request-path').value = '/api/changed';
        context.document.getElementById('protocol-item-form')
            .dispatchEvent(new context.Event('submit', { bubbles: true, cancelable: true }));
        await flushPromises(12);

        expect(updateName).toHaveBeenCalledWith(1, 'test1修改');
        expect(updateCfg).toHaveBeenCalledWith(1, 1, 1, {
            method: 'GET',
            path: '/api/changed',
            headers: {},
        });
        expect(updateCfg).toHaveBeenCalledTimes(1);
        expect(updateBody).not.toHaveBeenCalled();
    });

    /**
     * 测试思路：重配置模式必须重新查询当前协议项完整详情，并把名称、请求配置、响应配置和 Body 都回填到控件。
     * 示例：getProtocolEditDetail 返回数据库当前值后，页面显示原 path/status，Body 切换到响应侧也能看到查出的响应 Body。
     */
    it('表单页 reconfig 模式回填并调用 reconfigProtocol', async () => {
        const context = createProtocolItemFormContext('?apiMode=mock&projectId=1&protocolId=1&mode=reconfig');

        loadCoreScripts(context);
        await loginMockUser(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitProtocolItemForm = true;
        runScript(context, 'js/protocol_item_form.js');

        const reconfigProtocol = vi.spyOn(context.KitProxy.api, 'reconfigProtocol').mockResolvedValue({ protocol_id: 1, config_state: 0 });
        const getProtocolEditDetail = vi.spyOn(context.KitProxy.api, 'getProtocolEditDetail').mockResolvedValue({
            id: 1,
            name: '数据库HTTP协议',
            project_id: 1,
            type: 'HTTP',
            req_cfg: {
                method: 'POST',
                path: '/api/from-db',
                headers: { 'X-Trace': 'db' },
            },
            resp_cfg: {
                status_code: 201,
                headers: { 'X-Resp': 'ok' },
            },
            req_body_type: 'json',
            resp_body_type: 'text',
            request_body: '{\n  "from": "db-request"\n}',
            response_body: 'db-response-body',
        });
        const updateName = vi.spyOn(context.KitProxy.api, 'updateProtocolName');
        const updateCfg = vi.spyOn(context.KitProxy.api, 'updateProtocolCfg');
        const updateBody = vi.spyOn(context.KitProxy.api, 'updateProtocolBody');
        context.document.addEventListener('protocol-item-form:navigate-back', event => {
            event.preventDefault();
        });

        await context.KitProxy.protocolItemForm.initPage();
        await flushPromises(12);

        expect(getProtocolEditDetail).toHaveBeenCalledWith(1);
        expect(context.document.getElementById('protocol-form-title').textContent).toBe('重配置协议项');
        expect(context.document.getElementById('protocol-item-name').value).toBe('数据库HTTP协议');
        expect(context.document.querySelector('input[name="request-method"]:checked').value).toBe('POST');
        expect(context.document.getElementById('request-path').value).toBe('/api/from-db');
        expect(context.document.getElementById('response-status-code').value).toBe('201');
        expect(context.KitProxy.protocolItemForm.pageState.bodyEditor.getType()).toBe('json');
        expect(context.KitProxy.protocolItemForm.pageState.bodyEditor.getValue()).toContain('"from": "db-request"');
        context.KitProxy.protocolItemForm.setActiveBodyTab('response');
        expect(context.KitProxy.protocolItemForm.pageState.bodyEditor.getType()).toBe('text');
        expect(context.KitProxy.protocolItemForm.pageState.bodyEditor.getValue()).toBe('db-response-body');
        context.KitProxy.protocolItemForm.setActiveBodyTab('request');

        context.document.getElementById('protocol-item-name').value = '重配置HTTP';
        context.document.getElementById('request-path').value = '/api/reconfig';
        context.document.getElementById('protocol-item-form')
            .dispatchEvent(new context.Event('submit', { bubbles: true, cancelable: true }));
        await flushPromises(12);

        expect(reconfigProtocol).toHaveBeenCalledTimes(1);
        expect(reconfigProtocol.mock.calls[0][0]).toBe(1);
        expect(reconfigProtocol.mock.calls[0][1].cfg_header.config_state).toBe(0);
        expect(reconfigProtocol.mock.calls[0][1].cfg_header.name).toBe('重配置HTTP');
        expect(reconfigProtocol.mock.calls[0][1].req_cfg.path).toBe('/api/reconfig');
        expect(reconfigProtocol.mock.calls[0][1].request_body).toContain('"from": "db-request"');
        expect(reconfigProtocol.mock.calls[0][1].response_body).toBe('db-response-body');
        expect(updateName).not.toHaveBeenCalled();
        expect(updateCfg).not.toHaveBeenCalled();
        expect(updateBody).not.toHaveBeenCalled();
    });

    /**
     * 测试思路：重配置页要按查出的协议详情渲染并回填对应协议控件，不能因为项目协议类型是字符串而隐藏请求/响应配置。
     * 示例：getProtocolEditDetail 返回 type="TCP" 时，请求侧和响应侧 TCP 头部字段控件都应显示并回填字段值。
     */
    it('表单页 reconfig 模式支持字符串 TCP 类型并展示请求响应控件', async () => {
        const context = createProtocolItemFormContext('?apiMode=mock&projectId=2&protocolId=2&mode=reconfig');

        loadCoreScripts(context);
        await loginMockUser(context, {
            note: 'admin',
            loginType: 'admin',
            password: 'admin123',
        });
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));
        context.KitProxy.__disableAutoInitProtocolItemForm = true;
        runScript(context, 'js/protocol_item_form.js');

        vi.spyOn(context.KitProxy.api, 'getProject').mockResolvedValue([{
            id: 2,
            name: '真实TCP服务',
            protocol_type: 'TCP',
            mode: 1,
            runtime_state: 1,
            listen_port: 18082,
            status: 1,
            length_policy: 'body_length',
        }]);
        const getProtocolEditDetail = vi.spyOn(context.KitProxy.api, 'getProtocolEditDetail').mockResolvedValue({
            id: 2,
            name: 'TCP开包检测示例',
            project_id: 2,
            type: 'TCP',
            req_cfg: {
                function_code: 'H1000',
                fields: { 4: 'H00000209' },
            },
            resp_cfg: {
                function_code: 'H1080',
                fields: { 4: 'H00000209' },
            },
            req_body_type: 'json',
            resp_body_type: 'json',
            request_body: '',
            response_body: '',
        });

        await context.KitProxy.protocolItemForm.initPage();
        await flushPromises(12);

        const reqButton = context.document.getElementById('req-pattern-infos');
        const respButton = context.document.getElementById('resp-pattern-infos');
        expect(getProtocolEditDetail).toHaveBeenCalledWith(2);
        expect(context.KitProxy.protocolItemForm.pageState.protocolType).toBe(context.ProtocolType.CUSTOM_TCP);
        expect(reqButton).toBeTruthy();
        expect(respButton).toBeTruthy();
        expect(context.document.querySelector('.protocol-config-card-request').textContent).toContain('请求侧配置');
        expect(context.document.querySelector('.protocol-config-card-response').textContent).toContain('响应侧配置');

        const reqFields = JSON.parse(reqButton.dataset.patternInfos).fields;
        const respFields = JSON.parse(respButton.dataset.patternInfos).fields;
        expect(reqFields.some(field => field.role === 'function_code' && field.value === 'H1000')).toBe(true);
        expect(respFields.some(field => field.role === 'function_code' && field.value === 'H1080')).toBe(true);
    });
	});

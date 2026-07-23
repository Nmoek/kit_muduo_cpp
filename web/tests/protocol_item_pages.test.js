import { describe, expect, it, vi } from 'vitest';
import { createBrowserContext, createProtocolItemFormContext, createProtocolItemPageContext, flushPromises, loadCoreScripts, loadProtocolListScripts, loginMockUser, readRepoFile, repoFileExists, runScript } from './helpers/browser_context.js';

describe('V1.5 protocol item form page and compact cards', () => {
    /**
     * 测试思路：项目运行且协议项 config_state=1 时，实时详情入口应可用并打开右侧抽屉。
     * 示例：project runtime_state=1、protocol config_state=1，按钮 disabled=false，点击后 dialog 可见。
     */
    it('协议项在线且项目运行时启用实时交互详情入口', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);
        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.className = 'protocol-items-page';
        root.dataset.runtimeState = '1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);
        const item = context.addProtocolItem(root, {
            id: 10, name: '实时 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1,
            req_cfg: { method: 'GET', path: '/health' }, resp_cfg: {},
        });

        const button = item.querySelector('.protocol-interaction-btn');
        expect(button.disabled).toBe(false);
        button.click();
        expect(context.KitProxy.protocolInteractionDrawer.isOpen()).toBe(true);
        expect(context.document.querySelector('.protocol-interaction-drawer').getAttribute('role')).toBe('dialog');
        context.KitProxy.protocolInteractionDrawer.close();
        context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 10);
    });

    /**
     * 测试思路：项目停止、协议待重配置和协议未上线都必须禁用入口，并给出对应原因。
     * 示例：runtime=0 -> 请先启动测试服务；runtime=1/config_state=2 -> 协议项待重配置；config_state=0 -> 请先上线协议项。
     */
    it('项目或协议运行态不满足时禁用实时交互详情入口', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);
        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.className = 'protocol-items-page';
        root.dataset.runtimeState = '0';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);
        const item = context.addProtocolItem(root, {
            id: 11, name: '不可连接 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1,
            req_cfg: { method: 'GET', path: '/health' }, resp_cfg: {},
        });
        const button = item.querySelector('.protocol-interaction-btn');
        expect(button.disabled).toBe(true);
        expect(button.title).toBe('请先启动测试服务');

        root.dataset.runtimeState = '1';
        item.dataset.projectRuntimeState = '1';
        item.dataset.configState = '2';
        context.refreshProtocolInteractionEntry(item);
        expect(button.disabled).toBe(true);
        expect(button.title).toBe('协议项待重配置');

        item.dataset.configState = '0';
        context.refreshProtocolInteractionEntry(item);
        expect(button.disabled).toBe(true);
        expect(button.title).toBe('请先上线协议项');

        item.dataset.projectRuntimeState = '0';
        item.dataset.status = 'inactive';
        context.refreshProtocolInteractionEntry(item);
        expect(button.disabled).toBe(true);
        expect(button.title).toBe('协议项已删除');
    });

    /**
     * 测试思路：抽屉显隐、全屏切换和连接控制是三种独立动作，打开不自动连接，收起/全屏不能调用 disconnect 或重建 client。
     * 示例：点击入口后 connect 为 0，点击连接按钮后为 1，再点击全屏和关闭，disconnect 仍为 0。
     */
    it('抽屉收起和全屏切换不改变实时连接生命周期', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);
        const connect = vi.fn();
        const disconnect = vi.fn();
        const fakeClient = {
            on: vi.fn(() => () => {}),
            connect,
            disconnect,
            destroy: vi.fn(),
            pause: vi.fn(),
            resume: vi.fn(),
            getState: vi.fn(() => ({
                connectionState: 'disconnected', desiredConnected: false, socket: null,
                visibleRecords: [], pendingRecords: [], warnings: [], lastError: '', selectedRecordKey: null,
            })),
            getRecord: vi.fn(() => null),
            selectRecord: vi.fn(),
            getAttachment: vi.fn(),
        };
        context.KitProxy.protocolInteractionLive.createClient = vi.fn(() => fakeClient);

        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.className = 'protocol-items-page';
        root.dataset.runtimeState = '1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);
        const item = context.addProtocolItem(root, {
            id: 12, name: '生命周期 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1,
            req_cfg: { method: 'GET', path: '/health' }, resp_cfg: {},
        });
        item.querySelector('.protocol-interaction-btn').click();
        expect(connect).not.toHaveBeenCalled();
        const drawer = context.document.querySelector('.protocol-interaction-drawer');
        drawer.querySelector('[data-action="connect"]').click();
        expect(connect).toHaveBeenCalledTimes(1);
        drawer.querySelector('[data-action="fullscreen"]').click();
        expect(drawer.classList.contains('is-fullscreen')).toBe(true);
        drawer.querySelector('[data-action="close"]').click();
        expect(disconnect).not.toHaveBeenCalled();
        context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 12);
    });

    /**
     * 测试思路：后端 head_text、错误文本和 Raw/Hex 必须通过 textContent 输出，恶意字符串不能创建 HTML 节点。
     * 示例：head_text='<img src=x onerror=1>'，Request Tab 中应只有文本，不应出现 img 元素。
     */
    it('抽屉详情安全渲染后端文本字段', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);
        const record = {
            _key: 'protocol:1:1', scope: 'protocol', project_id: 1, protocol_id: 13,
            cache_instance_id: 1, seq: 1, protocol_type: 'http', time_ms: 1,
            peer_addr: 'peer', result: 'matched', _delivery: 'live',
            request: { head_text: '<img src=x onerror=1>', body: { kind: 'text', expect_kind: 'text', text: '<b>raw</b>', attachments: [] } },
            response: { head_text: 'ok', body: { kind: 'empty', expect_kind: 'empty', text: '', attachments: [] } },
        };
        const fakeClient = {
            on: vi.fn(() => () => {}),
            connect: vi.fn(), disconnect: vi.fn(), pause: vi.fn(), resume: vi.fn(),
            destroy: vi.fn(),
            getState: vi.fn(() => ({ connectionState: 'active', desiredConnected: true, socket: {}, warnings: [], lastError: '', visibleRecords: [record], pendingRecords: [], selectedRecordKey: record._key })),
            getRecord: vi.fn(() => record), selectRecord: vi.fn(), getAttachment: vi.fn(),
        };
        context.KitProxy.protocolInteractionLive.createClient = vi.fn(() => fakeClient);
        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.className = 'protocol-items-page';
        root.dataset.runtimeState = '1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);
        const item = context.addProtocolItem(root, {
            id: 13, name: '安全渲染 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1,
            req_cfg: { method: 'GET', path: '/health' }, resp_cfg: {},
        });
        item.querySelector('.protocol-interaction-btn').click();
        const panel = context.document.querySelector('.protocol-interaction-drawer');
        panel.querySelector('[data-tab="request"]').click();
        expect(panel.querySelector('.interaction-record-detail').textContent).toContain('<img src=x onerror=1>');
        expect(panel.querySelector('.interaction-record-detail img')).toBeNull();
        context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 13);
    });

    /**
     * 测试思路：窄屏单栏流程由选中记录进入详情，并能通过返回按钮恢复列表。
     * 示例：点击 protocol:1:1 后抽屉增加 is-mobile-detail，点击“返回列表”后移除该状态。
     */
    it('移动端列表和详情状态可往返切换', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);
        const record = {
            _key: 'protocol:1:1', scope: 'protocol', project_id: 1, protocol_id: 14,
            cache_instance_id: 1, seq: 1, protocol_type: 'http', time_ms: 1,
            peer_addr: 'peer', result: 'matched', _delivery: 'live',
            request: { meta: { method: 'GET', path: '/mobile' }, body: { attachments: [] } },
            response: { body: { attachments: [] } },
        };
        const state = { connectionState: 'active', desiredConnected: true, socket: {}, warnings: [], lastError: '', visibleRecords: [record], pendingRecords: [], selectedRecordKey: null };
        const fakeClient = {
            on: vi.fn(() => () => {}), connect: vi.fn(), disconnect: vi.fn(), pause: vi.fn(), resume: vi.fn(), destroy: vi.fn(),
            getState: vi.fn(() => state), getRecord: vi.fn(key => key === record._key ? record : null),
            selectRecord: vi.fn(key => { state.selectedRecordKey = key; }), getAttachment: vi.fn(),
        };
        context.KitProxy.protocolInteractionLive.createClient = vi.fn(() => fakeClient);
        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.className = 'protocol-items-page';
        root.dataset.runtimeState = '1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);
        const item = context.addProtocolItem(root, { id: 14, name: '移动端 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1, req_cfg: {}, resp_cfg: {} });

        item.querySelector('.protocol-interaction-btn').click();
        const panel = context.document.querySelector('.protocol-interaction-drawer');
        panel.querySelector('[data-record-key="protocol:1:1"]').click();
        expect(panel.classList.contains('is-mobile-detail')).toBe(true);
        panel.querySelector('[data-action="back-to-list"]').click();
        expect(panel.classList.contains('is-mobile-detail')).toBe(false);
        context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 14);
    });

    /**
     * 测试思路：已读状态必须独立于当前选中项，依次查看多条记录后，已查看记录都应保持已读。
     * 示例：先点击 seq=1，再点击 seq=2；seq=1 和 seq=2 的“未读”徽标都消失，未点击的 seq=3 仍显示“未读”。
     */
    it('实时记录已读状态不会随当前选中项互斥切换', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);
        const makeRecord = seq => ({
            _key: `protocol:1:${seq}`, scope: 'protocol', project_id: 1, protocol_id: 18,
            cache_instance_id: 1, seq, protocol_type: 'http', time_ms: seq,
            peer_addr: 'peer', result: 'matched', _delivery: 'live',
            request: { meta: { method: 'POST', path: '/api/upload' }, body: { kind: 'image', size: 1024, attachments: [] } },
            response: { meta: { status_code: 200 }, body: { attachments: [] } },
        });
        const records = [makeRecord(1), makeRecord(2), makeRecord(3)];
        const state = {
            connectionState: 'active', desiredConnected: true, socket: {}, warnings: [], lastError: '',
            visibleRecords: records, pendingRecords: [], selectedRecordKey: null,
        };
        const fakeClient = {
            on: vi.fn(() => () => {}), connect: vi.fn(), disconnect: vi.fn(), pause: vi.fn(), resume: vi.fn(), destroy: vi.fn(),
            getState: vi.fn(() => state), getRecord: vi.fn(key => records.find(record => record._key === key) || null),
            selectRecord: vi.fn(key => { state.selectedRecordKey = key; }), getAttachment: vi.fn(),
        };
        context.KitProxy.protocolInteractionLive.createClient = vi.fn(() => fakeClient);
        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.className = 'protocol-items-page';
        root.dataset.runtimeState = '1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);
        const item = context.addProtocolItem(root, {
            id: 18, name: '已读状态 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1,
            req_cfg: {}, resp_cfg: {},
        });

        item.querySelector('.protocol-interaction-btn').click();
        const panel = context.document.querySelector('.protocol-interaction-drawer');
        panel.querySelector('[data-record-key="protocol:1:1"]').click();
        panel.querySelector('[data-action="back-to-list"]').click();
        panel.querySelector('[data-record-key="protocol:1:2"]').click();

        const first = panel.querySelector('[data-record-key="protocol:1:1"]');
        const second = panel.querySelector('[data-record-key="protocol:1:2"]');
        const third = panel.querySelector('[data-record-key="protocol:1:3"]');
        expect(first.querySelector('[data-role="read-state"]')).toBeNull();
        expect(second.querySelector('[data-role="read-state"]')).toBeNull();
        expect(third.querySelector('[data-role="read-state"]')).toBeNull();
        expect(first.classList.contains('is-unread')).toBe(false);
        expect(second.classList.contains('is-unread')).toBe(false);
        expect(third.classList.contains('is-unread')).toBe(true);
        expect(second.classList.contains('is-selected')).toBe(true);
        context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 18);
    });

    /**
     * 测试思路：未读状态使用 demo 的黄色事件卡片视觉，不额外渲染“未读”文字标签。
     * 示例：CSS 中 .is-unread 包含浅黄色背景和左侧橙色强调线，记录 DOM 中不存在 read-state 节点。
     */
    it('未读记录保持黄色卡片且不显示未读标签', () => {
        const css = readRepoFile('css/protocol_interaction_drawer.css');
        const unreadRule = css.match(/\.interaction-record-item\.is-unread\s*\{([^}]*)\}/);
        const selectedRule = css.match(/\.interaction-record-item\.is-selected,\s*\.interaction-record-item\.is-selected:hover\s*\{([^}]*)\}/);
        expect(unreadRule).toBeTruthy();
        expect(selectedRule).toBeTruthy();
        expect(unreadRule[1]).toContain('background: #fff8e7');
        expect(unreadRule[1]).toContain('inset 4px 0 0 var(--interaction-orange)');
        expect(selectedRule[1]).toContain('background: #edf8f0');
        expect(selectedRule[1]).toContain('inset 4px 0 0 var(--interaction-green)');
        expect(readRepoFile('js/protocol_interaction_drawer.js')).not.toContain('data-role="read-state"');
    });

    /**
     * 测试思路：实时/补发来源 badge 只对管理员渲染，普通用户的记录 DOM 不应包含这些控件。
     * 示例：admin -> 同时看到“实时”和“补发”；normal -> 两个 data-role=delivery 节点都不存在。
     */
    it('实时和补发来源 badge 只对管理员可见', () => {
        const createContext = role => {
            const context = createBrowserContext('?apiMode=mock&projectId=1');
            loadCoreScripts(context);
            context.KitProxy.__disableAutoInitMain = true;
            context.KitProxy.__disableAutoInitProtocolItems = true;
            loadProtocolListScripts(context);
            context.KitProxy.auth.applyCurrentUser({ note: role, role });
            const records = ['live', 'catch_up'].map((delivery, index) => ({
                _key: `protocol:1:${index + 1}`, scope: 'protocol', project_id: 1, protocol_id: 19,
                cache_instance_id: 1, seq: index + 1, protocol_type: 'http', time_ms: index + 1,
                peer_addr: 'peer', result: 'matched', _delivery: delivery,
                request: { meta: { method: 'GET', path: '/source' }, body: { attachments: [] } },
                response: { meta: { status_code: 200 }, body: { attachments: [] } },
            }));
            const state = {
                connectionState: 'active', desiredConnected: true, socket: {}, warnings: [], lastError: '',
                visibleRecords: records, pendingRecords: [], selectedRecordKey: null,
            };
            const fakeClient = {
                on: vi.fn(() => () => {}), connect: vi.fn(), disconnect: vi.fn(), pause: vi.fn(), resume: vi.fn(), destroy: vi.fn(),
                getState: vi.fn(() => state), getRecord: vi.fn(key => records.find(record => record._key === key) || null),
                selectRecord: vi.fn(), getAttachment: vi.fn(), clearRecords: vi.fn(),
            };
            context.KitProxy.protocolInteractionLive.createClient = vi.fn(() => fakeClient);
            const root = context.document.createElement('div');
            root.id = 'service-card-1';
            root.className = 'protocol-items-page';
            root.dataset.runtimeState = '1';
            root.innerHTML = '<div class="protocol-list"></div>';
            context.document.body.appendChild(root);
            const item = context.addProtocolItem(root, {
                id: 19, name: '来源权限 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1,
                req_cfg: {}, resp_cfg: {},
            });
            item.querySelector('.protocol-interaction-btn').click();
            return { context, panel: context.document.querySelector('.protocol-interaction-drawer') };
        };

        const admin = createContext('admin');
        expect(admin.panel.querySelectorAll('[data-role="delivery"]')).toHaveLength(2);
        admin.context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 19);

        const normal = createContext('normal');
        expect(normal.panel.querySelectorAll('[data-role="delivery"]')).toHaveLength(0);
        normal.context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 19);
    });

    /**
     * 测试思路：Notice 原因应进入标题行，Notice badge 位于结果 badge 左侧；“协议项”与“项目 Notice”筛选都显示 seq，全部筛选隐藏 seq。
     * 示例：Notice error_message=“项目级 Notice：没有协议项可以处理”时，标题只显示“没有协议项可以处理”。
     */
    it('Notice 标题、badge 顺序和筛选序号显示符合列表规则', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);
        context.KitProxy.auth.applyCurrentUser({ note: 'normal', role: 'normal' });
        const protocol = {
            _key: 'protocol:1:1', scope: 'protocol', project_id: 1, protocol_id: 20,
            cache_instance_id: 1, seq: 1, protocol_type: 'http', time_ms: 1,
            peer_addr: 'peer', result: 'matched', _delivery: 'live',
            request: { meta: { method: 'GET', path: '/ok' }, body: { attachments: [] } },
            response: { meta: { status_code: 200 }, body: { attachments: [] } },
        };
        const notice = {
            _key: 'project:2:2', scope: 'project', project_id: 1, protocol_id: 0,
            cache_instance_id: 2, seq: 2, protocol_type: 'http', time_ms: 2,
            peer_addr: 'peer', result: 'route_not_found', _delivery: 'live',
            error_message: '项目级 Notice：没有协议项可以处理',
            request: { meta: { method: 'GET', path: '/missing' }, body: { attachments: [] } },
            response: { meta: { status_code: 404 }, body: { attachments: [] } },
        };
        const state = {
            connectionState: 'active', desiredConnected: true, socket: {}, warnings: [], lastError: '',
            visibleRecords: [protocol, notice], pendingRecords: [], selectedRecordKey: null,
        };
        const fakeClient = {
            on: vi.fn(() => () => {}), connect: vi.fn(), disconnect: vi.fn(), pause: vi.fn(), resume: vi.fn(), destroy: vi.fn(),
            getState: vi.fn(() => state), getRecord: vi.fn(key => [protocol, notice].find(record => record._key === key) || null),
            selectRecord: vi.fn(), getAttachment: vi.fn(), clearRecords: vi.fn(),
        };
        context.KitProxy.protocolInteractionLive.createClient = vi.fn(() => fakeClient);
        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.className = 'protocol-items-page';
        root.dataset.runtimeState = '1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);
        const item = context.addProtocolItem(root, {
            id: 20, name: '筛选规则 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1,
            req_cfg: {}, resp_cfg: {},
        });
        item.querySelector('.protocol-interaction-btn').click();
        const panel = context.document.querySelector('.protocol-interaction-drawer');
        const allNotice = panel.querySelector('[data-record-key="project:2:2"]');
        expect(allNotice.querySelector('[data-role="title"]').textContent).toBe('没有协议项可以处理');
        expect(allNotice.querySelector('[data-role="scope"]').parentElement.className).toContain('interaction-record-item-top');
        const allTopRoles = Array.from(allNotice.querySelector('.interaction-record-item-top').children)
            .map(child => child.dataset.role);
        expect(allTopRoles).toEqual(['title', 'scope', 'result']);
        expect(allNotice.querySelector('[data-role="seq"]').hidden).toBe(true);

        panel.querySelector('[data-filter="protocol"]').click();
        expect(panel.querySelector('[data-record-key="protocol:1:1"] [data-role="seq"]').hidden).toBe(false);
        panel.querySelector('[data-filter="notice"]').click();
        expect(panel.querySelector('[data-record-key="project:2:2"] [data-role="seq"]').hidden).toBe(false);
        panel.querySelector('[data-filter="all"]').click();
        expect(panel.querySelector('[data-record-key="protocol:1:1"] [data-role="seq"]').hidden).toBe(true);
        expect(panel.querySelector('[data-filter="protocol"]').textContent).toBe('协议项');
        context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 20);
    });

    /**
     * 测试思路：列表排序只根据 time_ms 改变浏览器显示顺序，清空只清理本地记录，不断开实时 client。
     * 示例：默认正序显示 3、2、1，切换倒序后显示 1、2、3，点击清空后列表为空且 clearRecords 被调用。
     */
        it('实时记录支持按时间排序和一键清空本地列表', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);
        const records = [3, 1, 2].map(seq => ({
            _key: `protocol:1:${seq}`, scope: 'protocol', project_id: 1, protocol_id: 21,
            cache_instance_id: 1, seq, protocol_type: 'http', time_ms: seq * 1000,
            peer_addr: 'peer', result: 'matched', _delivery: 'live',
            request: { meta: { method: 'GET', path: `/${seq}` }, body: { attachments: [] } },
            response: { meta: { status_code: 200 }, body: { attachments: [] } },
        }));
        const state = {
            connectionState: 'active', desiredConnected: true, socket: {}, warnings: [], lastError: '',
            visibleRecords: records, pendingRecords: [], selectedRecordKey: null,
        };
        const clearRecords = vi.fn(() => {
            state.visibleRecords = [];
            state.pendingRecords = [];
            state.selectedRecordKey = null;
        });
        const fakeClient = {
            on: vi.fn(() => () => {}), connect: vi.fn(), disconnect: vi.fn(), pause: vi.fn(), resume: vi.fn(), destroy: vi.fn(),
            getState: vi.fn(() => state), getRecord: vi.fn(), selectRecord: vi.fn(), getAttachment: vi.fn(), clearRecords,
            setBufferLimits: vi.fn(),
            setRecordOrder: vi.fn(),
            updatePersistenceUiState: vi.fn(),
        };
        context.KitProxy.protocolInteractionLive.createClient = vi.fn(() => fakeClient);
        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.className = 'protocol-items-page';
        root.dataset.runtimeState = '1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);
        const item = context.addProtocolItem(root, {
            id: 21, name: '排序 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1,
            req_cfg: {}, resp_cfg: {},
        });
        item.querySelector('.protocol-interaction-btn').click();
        const panel = context.document.querySelector('.protocol-interaction-drawer');
        const keys = () => Array.from(panel.querySelectorAll('.interaction-record-item')).map(item => item.dataset.recordKey);
        expect(keys()).toEqual(['protocol:1:3', 'protocol:1:2', 'protocol:1:1']);
        expect(fakeClient.setBufferLimits).toHaveBeenCalledWith(
            { maxVisibleRecords: 10, maxPendingRecords: 20 },
            { reflowVisibleRecords: records },
        );
        expect(panel.querySelector('[data-action="toggle-sort"]').dataset.sortDirection).toBe('asc');
        const descendingIcon = panel.querySelector('[data-action="toggle-sort"] svg').outerHTML;
        panel.querySelector('[data-action="toggle-sort"]').click();
        expect(keys()).toEqual(['protocol:1:1', 'protocol:1:2', 'protocol:1:3']);
        expect(fakeClient.setRecordOrder).toHaveBeenCalledWith('asc');
        expect(panel.querySelector('[data-action="toggle-sort"]').dataset.sortDirection).toBe('desc');
        expect(panel.querySelector('[data-action="toggle-sort"] svg').outerHTML).not.toBe(descendingIcon);
        panel.querySelector('[data-action="clear-records"]').click();
        const confirmation = panel.querySelector('.interaction-clear-confirm');
        expect(confirmation.hidden).toBe(false);
        expect(clearRecords).not.toHaveBeenCalled();
        confirmation.querySelector('[data-action="cancel-clear"]').click();
        expect(confirmation.hidden).toBe(true);
        panel.querySelector('[data-action="clear-records"]').click();
        confirmation.querySelector('[data-action="confirm-clear"]').click();
        expect(clearRecords).toHaveBeenCalledTimes(1);
        expect(panel.querySelectorAll('.interaction-record-item')).toHaveLength(0);
        expect(fakeClient.disconnect).not.toHaveBeenCalled();
        context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 21);
    });

    /**
     * 测试思路：抽屉的筛选、详情 Tab、全屏、排序和已读动作都要通知快照层，关闭时 drawerOpen 必须为 false。
     * 示例：依次点击 Notice/附件/全屏/记录，最后关闭，adapter 通知中能找到对应字段和最终 drawerOpen=false。
     */
    it('抽屉 UI 状态变化写入实时 workspace 快照', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);
        const record = {
            _key: 'protocol:1:1', scope: 'protocol', project_id: 1, protocol_id: 24,
            cache_instance_id: 1, seq: 1, protocol_type: 'http', time_ms: 1000,
            peer_addr: 'peer', result: 'matched', _delivery: 'live',
            request: { meta: { method: 'GET', path: '/' }, body: { attachments: [] } },
            response: { meta: { status_code: 200 }, body: { attachments: [] } },
        };
        const state = {
            connectionState: 'active', desiredConnected: true, socket: {}, warnings: [], lastError: '',
            visibleRecords: [record], pendingRecords: [], selectedRecordKey: null,
        };
        const updatePersistenceUiState = vi.fn();
        const fakeClient = {
            on: vi.fn(() => () => {}), connect: vi.fn(), disconnect: vi.fn(), pause: vi.fn(), resume: vi.fn(), destroy: vi.fn(),
            getState: vi.fn(() => state), getRecord: vi.fn(key => key === record._key ? record : null),
            selectRecord: vi.fn(), getAttachment: vi.fn(), clearRecords: vi.fn(), setBufferLimits: vi.fn(),
            setRecordOrder: vi.fn(), updatePersistenceUiState,
        };
        context.KitProxy.protocolInteractionLive.createClient = vi.fn(() => fakeClient);
        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.className = 'protocol-items-page';
        root.dataset.runtimeState = '1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);
        const item = context.addProtocolItem(root, {
            id: 24, name: '快照 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1,
            req_cfg: {}, resp_cfg: {},
        });
        item.querySelector('.protocol-interaction-btn').click();
        const panel = context.document.querySelector('.protocol-interaction-drawer');
        panel.querySelector('[data-record-key="protocol:1:1"]').click();
        panel.querySelector('[data-filter="notice"]').click();
        panel.querySelector('[data-tab="attachments"]').click();
        panel.querySelector('[data-action="fullscreen"]').click();
        const latest = updatePersistenceUiState.mock.calls.at(-1);
        expect(latest[0]).toMatchObject({
            filter: 'notice', detailTab: 'attachments', fullscreen: true,
            mobileDetail: true, drawerOpen: true, readRecordKeys: ['protocol:1:1'],
        });
        context.KitProxy.protocolInteractionDrawer.close();
        const finalCall = updatePersistenceUiState.mock.calls.at(-1);
        expect(finalCall[0].drawerOpen).toBe(false);
        context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 24);
    });

    /**
     * 测试思路：普通模式列表固定为 10 个显示槽位，全屏固定为左列 10 条、右列 10 条，列表不得依赖滚动条展示。
     * 示例：CSS 使用 repeat(10) 行、全屏 grid-auto-flow: column，并将 interaction-record-items 设置为 overflow:hidden。
     */
    it('列表固定一屏展示十条，全屏按列优先展示二十条', () => {
        const css = readRepoFile('css/protocol_interaction_drawer.css');
        expect(css).toContain('grid-template-rows: repeat(10, minmax(0, 1fr));');
        expect(css).toContain('grid-auto-flow: row;');
        expect(css).toContain('grid-auto-flow: column;');
        expect(css).toContain('overflow: hidden;');
        expect(css).toContain('grid-template-columns: 580px minmax(0, 1fr);');
    });

    /**
     * 测试思路：实时层淘汰可见记录时，旧卡片淡出，新卡片在同一过渡窗口淡入；中速动画必须在下一次 1000ms 消费前释放。
     * 示例：列表从 1、2 替换为 2、3，淘汰的 1 有 is-exiting，新记录 3 有 is-entering，690ms 后旧记录移除。
     */
    it('可见记录淘汰时显示淡出动画并在结束后移除', () => {
        vi.useFakeTimers();
        try {
            const context = createBrowserContext('?apiMode=mock&projectId=1');
            loadCoreScripts(context);
            context.KitProxy.__disableAutoInitMain = true;
            context.KitProxy.__disableAutoInitProtocolItems = true;
            loadProtocolListScripts(context);
            const makeRecord = (seq, timeMs) => ({
                _key: `protocol:1:${seq}`, scope: 'protocol', project_id: 1, protocol_id: 22,
                cache_instance_id: 1, seq, protocol_type: 'http', time_ms: timeMs,
                peer_addr: 'peer', result: 'matched', _delivery: 'live',
                request: { meta: { method: 'GET', path: `/${seq}` }, body: { attachments: [] } },
                response: { body: { attachments: [] } },
            });
            const first = makeRecord(1, 1);
            const second = makeRecord(2, 2);
            const third = makeRecord(3, 3);
            const state = {
                connectionState: 'active', desiredConnected: true, socket: {}, warnings: [], lastError: '',
                visibleRecords: [first, second], pendingRecords: [], selectedRecordKey: null,
            };
            const listeners = {};
            const fakeClient = {
                on: vi.fn((type, callback) => { listeners[type] = callback; return () => {}; }),
                connect: vi.fn(), disconnect: vi.fn(), pause: vi.fn(), resume: vi.fn(), destroy: vi.fn(),
                getState: vi.fn(() => state),
                getRecord: vi.fn(key => state.visibleRecords.find(record => record._key === key) || null),
                selectRecord: vi.fn(), getAttachment: vi.fn(),
            };
            context.KitProxy.protocolInteractionLive.createClient = vi.fn(() => fakeClient);
            const root = context.document.createElement('div');
            root.id = 'service-card-1';
            root.className = 'protocol-items-page';
            root.dataset.runtimeState = '1';
            root.innerHTML = '<div class="protocol-list"></div>';
            context.document.body.appendChild(root);
            const item = context.addProtocolItem(root, {
                id: 22, name: '淘汰动画 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1,
                req_cfg: {}, resp_cfg: {},
            });
            item.querySelector('.protocol-interaction-btn').click();
            const panel = context.document.querySelector('.protocol-interaction-drawer');
            expect(panel.querySelector('[data-record-key="protocol:1:1"]')).toBeTruthy();

            state.visibleRecords = [second, third];
            listeners.recordEvicted({ record: first, reason: 'visible_capacity' });
            listeners.recordVisible({ record: third });
            expect(panel.querySelector('[data-record-key="protocol:1:1"]').classList.contains('is-exiting')).toBe(true);
            expect(panel.querySelector('[data-record-key="protocol:1:3"]').classList.contains('is-entering-front')).toBe(true);

            expect(panel.style.getPropertyValue('--interaction-record-transition-duration')).toBe('650ms');
            vi.advanceTimersByTime(690);
            expect(panel.querySelector('[data-record-key="protocol:1:1"]')).toBeNull();
            context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 22);
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：列表未满时连续到达的事件不能因为 pending 数量变化而重建正在淡入的卡片。
     * 示例：记录 2 开始淡入后记录 3 到达，记录 2 的 DOM 引用和动画 class 保持不变，不能闪烁重播。
     */
    it('连续新事件到达时稳定保持当前淡入动画', () => {
        vi.useFakeTimers();
        try {
            const context = createBrowserContext('?apiMode=mock&projectId=1');
            loadCoreScripts(context);
            context.KitProxy.__disableAutoInitMain = true;
            context.KitProxy.__disableAutoInitProtocolItems = true;
            loadProtocolListScripts(context);
            const makeRecord = seq => ({
                _key: `protocol:1:${seq}`, scope: 'protocol', project_id: 1, protocol_id: 23,
                cache_instance_id: 1, seq, protocol_type: 'http', time_ms: seq,
                peer_addr: 'peer', result: 'matched', _delivery: 'live',
                request: { body: { attachments: [] } }, response: { body: { attachments: [] } },
            });
            const first = makeRecord(1);
            const second = makeRecord(2);
            const third = makeRecord(3);
            const state = {
                connectionState: 'active', desiredConnected: true, socket: {}, warnings: [], lastError: '',
                visibleRecords: [first], pendingRecords: [], selectedRecordKey: null,
            };
            const listeners = {};
            const fakeClient = {
                on: vi.fn((type, callback) => { listeners[type] = callback; return () => {}; }),
                connect: vi.fn(), disconnect: vi.fn(), pause: vi.fn(), resume: vi.fn(), destroy: vi.fn(),
                getState: vi.fn(() => state),
                getRecord: vi.fn(key => state.visibleRecords.find(record => record._key === key) || null),
                selectRecord: vi.fn(), getAttachment: vi.fn(),
            };
            context.KitProxy.protocolInteractionLive.createClient = vi.fn(() => fakeClient);
            const root = context.document.createElement('div');
            root.id = 'service-card-1';
            root.className = 'protocol-items-page';
            root.dataset.runtimeState = '1';
            root.innerHTML = '<div class="protocol-list"></div>';
            context.document.body.appendChild(root);
            const item = context.addProtocolItem(root, {
                id: 23, name: '连续淡入 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1,
                req_cfg: {}, resp_cfg: {},
            });
            item.querySelector('.protocol-interaction-btn').click();
            const panel = context.document.querySelector('.protocol-interaction-drawer');

            state.visibleRecords = [first, second];
            listeners.recordVisible({ record: second });
            const secondItem = panel.querySelector('[data-record-key="protocol:1:2"]');
            expect(secondItem.classList.contains('is-entering-front')).toBe(true);

            state.visibleRecords = [first, second, third];
            listeners.recordVisible({ record: third });
            expect(panel.querySelector('[data-record-key="protocol:1:2"]')).toBe(secondItem);
            expect(secondItem.classList.contains('is-entering-front')).toBe(true);
            const thirdItem = panel.querySelector('[data-record-key="protocol:1:3"]');
            expect(thirdItem).toBeTruthy();
            expect(thirdItem.classList.contains('is-entering-front')).toBe(false);

            context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 23);
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：全屏切回非全屏时保留较旧的 10 条，最新的 10 条回到 pending 队首；切换完成后新到达记录不能覆盖保留列表。
     * 示例：20 条可见记录切成 10 条时保留 seq=1..10，回退 seq=20..11，seq=21 在切换后仍存在于 pending。
     */
    it('全屏切回非全屏保留较旧记录并接纳切换期间新事件', () => {
        vi.useFakeTimers();
        try {
            const live = createBrowserContext('?apiMode=mock&projectId=1');
            loadCoreScripts(live);
            live.KitProxy.__disableAutoInitMain = true;
            live.KitProxy.__disableAutoInitProtocolItems = true;
            loadProtocolListScripts(live);
            const records = Array.from({ length: 20 }, (_, index) => ({
                _key: `protocol:1:${index + 1}`,
                scope: 'protocol', project_id: 1, protocol_id: 26,
                cache_instance_id: 1, seq: index + 1, protocol_type: 'http', time_ms: index + 1,
                peer_addr: 'peer', result: 'matched', _delivery: 'live',
                request: { body: { attachments: [] } }, response: { body: { attachments: [] } },
            }));
            const state = {
                connectionState: 'active', desiredConnected: true, socket: {}, warnings: [], lastError: '',
                visibleRecords: records.slice(0, 10), pendingRecords: records.slice(10), selectedRecordKey: null,
            };
            const fakeClient = {
                on: vi.fn(() => () => {}), connect: vi.fn(), disconnect: vi.fn(), pause: vi.fn(), resume: vi.fn(), destroy: vi.fn(),
                getState: vi.fn(() => state), getRecord: vi.fn(key => state.visibleRecords.find(record => record._key === key) || state.pendingRecords.find(record => record._key === key) || null),
                selectRecord: vi.fn(), getAttachment: vi.fn(),
                setBufferLimits: vi.fn((limits, placement = {}) => {
                    const preserve = new Set(placement.preserveVisibleKeys || []);
                    const all = state.visibleRecords.concat(state.pendingRecords);
                    const preferred = preserve.size
                        ? all.filter(record => preserve.has(record._key))
                        : all.slice(0, limits.maxVisibleRecords);
                    const visibleKeys = new Set(preferred.map(record => record._key));
                    const remaining = all.filter(record => !visibleKeys.has(record._key));
                    state.visibleRecords = preferred.slice(0, limits.maxVisibleRecords);
                    state.pendingRecords = remaining.slice(0, limits.maxPendingRecords);
                }),
            };
            live.KitProxy.protocolInteractionLive.createClient = vi.fn(() => fakeClient);
            const root = live.document.createElement('div');
            root.id = 'service-card-1';
            root.className = 'protocol-items-page';
            root.dataset.runtimeState = '1';
            root.innerHTML = '<div class="protocol-list"></div>';
            live.document.body.appendChild(root);
            const item = live.addProtocolItem(root, { id: 26, name: '全屏切换 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1, req_cfg: {}, resp_cfg: {} });
            item.querySelector('.protocol-interaction-btn').click();
            const panel = live.document.querySelector('.protocol-interaction-drawer');
            panel.querySelector('[data-action="fullscreen"]').click();
            panel.querySelector('[data-action="fullscreen"]').click();
            expect(fakeClient.setBufferLimits).toHaveBeenLastCalledWith(
                { maxVisibleRecords: 10, maxPendingRecords: 20 },
                {
                    reflowVisibleRecords: records,
                },
            );
            expect(state.visibleRecords.map(record => record.seq)).toEqual(records.slice(0, 10).map(record => record.seq));
            expect(state.pendingRecords.map(record => record.seq)).toEqual(records.slice(10).map(record => record.seq));

            const newRecord = Object.assign({}, records[19], { _key: 'protocol:1:21', seq: 21, time_ms: 21 });
            fakeClient.getState.mockReturnValueOnce(state);
            state.pendingRecords.push(newRecord);
            expect(state.pendingRecords.map(record => record.seq)).toContain(21);
            expect(state.visibleRecords.map(record => record.seq)).not.toContain(21);
            live.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 26);
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：使用真实 live client 走完打开、填充 12 条、全屏、切回非全屏的完整路径，逐条检查回放动画。
     * 示例：切回后每 1000ms 只新增 1 条，10 条回放记录都带淡入 class，不再隔条直接跳入。
     */
    it('真实抽屉回退后按中速逐条从列表首项淡入', () => {
        vi.useFakeTimers();
        try {
            const context = createBrowserContext('?apiMode=real&projectId=1');
            loadCoreScripts(context);
            context.KitProxy.__disableAutoInitMain = true;
            context.KitProxy.__disableAutoInitProtocolItems = true;
            loadProtocolListScripts(context);
            let client;
            const originalCreateClient = context.KitProxy.protocolInteractionLive.createClient;
            context.KitProxy.protocolInteractionLive.createClient = options => {
                client = originalCreateClient(options);
                return client;
            };
            const root = context.document.createElement('div');
            root.id = 'service-card-1';
            root.className = 'protocol-items-page';
            root.dataset.runtimeState = '1';
            root.innerHTML = '<div class="protocol-list"></div>';
            context.document.body.appendChild(root);
            const item = context.addProtocolItem(root, {
                id: 29, name: '真实回退 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1,
                req_cfg: {}, resp_cfg: {},
            });
            item.querySelector('.protocol-interaction-btn').click();
            client.setBufferLimits({ maxVisibleRecords: 20, maxPendingRecords: 20 });

            for (let seq = 1; seq <= 12; seq += 1) {
                client.receiveText(JSON.stringify({
                    type: 'interaction', delivery: 'live', record: {
                        seq,
                        scope: 'protocol',
                        project_id: 1,
                        protocol_id: 29,
                        cache_instance_id: 7,
                        protocol_type: 'http',
                        time_ms: seq,
                        peer_addr: '127.0.0.1:50000',
                        result: 'matched',
                        error_message: '',
                        request: { meta: { method: 'GET', path: `/test/${seq}` }, body: { kind: 'text', size: 1, attachments: [] } },
                        response: { meta: { status_code: 200 }, body: { kind: 'text', size: 1, text: 'ok', attachments: [] } },
                    },
                }));
                vi.advanceTimersByTime(1000);
            }
            expect(client.getState().visibleRecords).toHaveLength(12);

            const panel = context.document.querySelector('.protocol-interaction-drawer');
            panel.querySelector('[data-action="fullscreen"]').click();
            panel.querySelector('[data-action="fullscreen"]').click();
            vi.advanceTimersByTime(0);
            expect(client.getState().mergePaused).toBe(false);
            expect(client.getState().pendingQueues.reflow).toHaveLength(10);
            expect(panel.style.getPropertyValue('--interaction-record-transition-duration')).toBe('650ms');

            const consumed = [];
            client.on('recordVisible', payload => consumed.push(payload.record.seq));
            for (let index = 0; index < 10; index += 1) {
                vi.advanceTimersByTime(999);
                expect(consumed).toHaveLength(index);
                expect(client.getState().pendingQueues.reflow).toHaveLength(10 - index);
                vi.advanceTimersByTime(1);
                expect(consumed).toHaveLength(index + 1);
                expect(consumed[index]).toBe(index + 3);
                expect(client.getState().pendingQueues.reflow).toHaveLength(9 - index);
                const firstItem = panel.querySelector('.interaction-record-items .interaction-record-item');
                expect(firstItem.dataset.recordKey).toBe(`protocol:7:${index + 3}`);
                expect(firstItem.classList.contains('is-entering-front')).toBe(true);
            }
            context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 29);
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：消费速度切换必须同步更新卡片动画时长；若切换时上一条仍在过渡，应先结束旧过渡，再按新间隔消费。
     * 示例：慢速 820ms 动画中切到快速时立即清理旧 class，600ms 后的下一条使用 380ms 动画。
     */
    it('三档消费速度同步调整回放动画时长', () => {
        vi.useFakeTimers();
        try {
            const context = createBrowserContext('?apiMode=real&projectId=1');
            loadCoreScripts(context);
            context.KitProxy.__disableAutoInitMain = true;
            context.KitProxy.__disableAutoInitProtocolItems = true;
            loadProtocolListScripts(context);
            let client;
            const originalCreateClient = context.KitProxy.protocolInteractionLive.createClient;
            context.KitProxy.protocolInteractionLive.createClient = options => {
                client = originalCreateClient(options);
                return client;
            };
            const root = context.document.createElement('div');
            root.id = 'service-card-1';
            root.className = 'protocol-items-page';
            root.dataset.runtimeState = '1';
            root.innerHTML = '<div class="protocol-list"></div>';
            context.document.body.appendChild(root);
            const item = context.addProtocolItem(root, {
                id: 30, name: '动画速度 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1,
                req_cfg: {}, resp_cfg: {},
            });
            item.querySelector('.protocol-interaction-btn').click();
            const panel = context.document.querySelector('.protocol-interaction-drawer');
            const speedButton = speed => panel.querySelector(`[data-action="set-merge-speed"][data-speed="${speed}"]`);
            const makeRecordMessage = seq => JSON.stringify({
                type: 'interaction', delivery: 'live', record: {
                    seq, scope: 'protocol', project_id: 1, protocol_id: 30,
                    cache_instance_id: 7, protocol_type: 'http', time_ms: seq,
                    peer_addr: '127.0.0.1:50000', result: 'matched', error_message: '',
                    request: { body: { kind: 'empty', size: 0, attachments: [] } },
                    response: { body: { kind: 'empty', size: 0, attachments: [] } },
                },
            });

            expect(panel.style.getPropertyValue('--interaction-record-transition-duration')).toBe('650ms');
            speedButton('slow').click();
            expect(client.getState().mergeIntervalMs).toBe(1500);
            expect(panel.style.getPropertyValue('--interaction-record-transition-duration')).toBe('820ms');
            speedButton('medium').click();
            expect(client.getState().mergeIntervalMs).toBe(1000);
            expect(panel.style.getPropertyValue('--interaction-record-transition-duration')).toBe('650ms');
            speedButton('fast').click();
            expect(client.getState().mergeIntervalMs).toBe(600);
            expect(panel.style.getPropertyValue('--interaction-record-transition-duration')).toBe('380ms');

            speedButton('slow').click();
            client.receiveText(makeRecordMessage(1));
            vi.advanceTimersByTime(1500);
            expect(panel.querySelector('[data-record-key="protocol:7:1"]').classList.contains('is-entering-front')).toBe(true);
            speedButton('fast').click();
            expect(panel.querySelector('[data-record-key="protocol:7:1"]').classList.contains('is-entering-front')).toBe(false);
            client.receiveText(makeRecordMessage(2));
            vi.advanceTimersByTime(599);
            expect(client.getState().visibleRecords).toHaveLength(1);
            vi.advanceTimersByTime(1);
            expect(panel.querySelector('[data-record-key="protocol:7:2"]').classList.contains('is-entering-front')).toBe(true);
            context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 30);
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：非全屏使用 10 条可见记录和固定 20 条待显示队列，全屏只增加可见容量到 20；
     * 清空策略对协议项和 Notice 共用同一 client 配置。
     * 示例：打开抽屉调用 setBufferLimits(10,20)，全屏后调用 setBufferLimits(20,20)，且列表容器进入双列状态。
     */
    it('全屏切换实时列表容量并进入双列布局', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);
        const state = {
            connectionState: 'active', desiredConnected: true, socket: {}, warnings: [], lastError: '',
            visibleRecords: [], pendingRecords: [], selectedRecordKey: null,
        };
        const setBufferLimits = vi.fn();
        const fakeClient = {
            on: vi.fn(() => () => {}), connect: vi.fn(), disconnect: vi.fn(), pause: vi.fn(), resume: vi.fn(), destroy: vi.fn(),
            getState: vi.fn(() => state), getRecord: vi.fn(), selectRecord: vi.fn(), getAttachment: vi.fn(),
            setBufferLimits,
        };
        context.KitProxy.protocolInteractionLive.createClient = vi.fn(() => fakeClient);
        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.className = 'protocol-items-page';
        root.dataset.runtimeState = '1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);
        const item = context.addProtocolItem(root, {
            id: 23, name: '容量 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1,
            req_cfg: {}, resp_cfg: {},
        });
        item.querySelector('.protocol-interaction-btn').click();
        const panel = context.document.querySelector('.protocol-interaction-drawer');
        const items = panel.querySelector('.interaction-record-items');
        expect(setBufferLimits).toHaveBeenLastCalledWith({ maxVisibleRecords: 10, maxPendingRecords: 20 });
        panel.querySelector('[data-action="fullscreen"]').click();
        expect(setBufferLimits).toHaveBeenLastCalledWith({ maxVisibleRecords: 20, maxPendingRecords: 20 });
        expect(panel.classList.contains('is-fullscreen')).toBe(true);
        expect(readRepoFile('css/protocol_interaction_drawer.css')).toContain('grid-template-columns: repeat(2, minmax(0, 1fr));');
        expect(items.classList.contains('interaction-record-items')).toBe(true);
        context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 23);
    });

    /**
     * 测试思路：新事件触发重绘时，只在重绘前焦点已经位于当前选中条目的情况下恢复焦点，不能把用户焦点强行移走。
     * 示例：选中 seq=1 后焦点在 seq=1，新事件 seq=2 到达并重绘，document.activeElement 仍是 seq=1。
     */
    it('新事件到达时保持已选中记录焦点', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);
        const makeRecord = seq => ({
            _key: `protocol:1:${seq}`, scope: 'protocol', project_id: 1, protocol_id: 24,
            cache_instance_id: 1, seq, protocol_type: 'http', time_ms: seq,
            peer_addr: 'peer', result: 'matched', _delivery: 'live',
            request: { meta: { method: 'GET', path: `/${seq}` }, body: { attachments: [] } },
            response: { body: { attachments: [] } },
        });
        const first = makeRecord(1);
        const second = makeRecord(2);
        const state = {
            connectionState: 'active', desiredConnected: true, socket: {}, warnings: [], lastError: '',
            visibleRecords: [first], pendingRecords: [], selectedRecordKey: null,
        };
        const listeners = {};
        const fakeClient = {
            on: vi.fn((type, callback) => { listeners[type] = callback; return () => {}; }),
            connect: vi.fn(), disconnect: vi.fn(), pause: vi.fn(), resume: vi.fn(), destroy: vi.fn(),
            getState: vi.fn(() => state),
            getRecord: vi.fn(key => state.visibleRecords.find(record => record._key === key) || null),
            selectRecord: vi.fn(key => { state.selectedRecordKey = key; }),
            getAttachment: vi.fn(),
        };
        context.KitProxy.protocolInteractionLive.createClient = vi.fn(() => fakeClient);
        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.className = 'protocol-items-page';
        root.dataset.runtimeState = '1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);
        const item = context.addProtocolItem(root, {
            id: 24, name: '焦点保持 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1,
            req_cfg: {}, resp_cfg: {},
        });
        item.querySelector('.protocol-interaction-btn').click();
        const panel = context.document.querySelector('.protocol-interaction-drawer');
        panel.querySelector('[data-record-key="protocol:1:1"]').click();
        const firstItem = panel.querySelector('[data-record-key="protocol:1:1"]');
        firstItem.focus();
        expect(context.document.activeElement.dataset.recordKey).toBe('protocol:1:1');
        state.visibleRecords = [first, second];
        listeners.recordsChanged({ reason: 'merge' });
        expect(context.document.activeElement.dataset.recordKey).toBe('protocol:1:1');
        context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 24);
    });

    /**
     * 测试思路：选中记录被淘汰后，抽屉必须清除选择和焦点，不能自动聚焦新到达的记录。
     * 示例：选中 seq=1，淘汰 seq=1 并显示 seq=2/3，activeElement 不得变为 seq=2 或 seq=3。
     */
    it('选中记录淘汰后焦点自动失效且不转移', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);
        const makeRecord = seq => ({
            _key: `protocol:1:${seq}`, scope: 'protocol', project_id: 1, protocol_id: 28,
            cache_instance_id: 1, seq, protocol_type: 'http', time_ms: seq,
            peer_addr: 'peer', result: 'matched', _delivery: 'live',
            request: { meta: { method: 'GET', path: `/${seq}` }, body: { attachments: [] } },
            response: { body: { attachments: [] } },
        });
        const first = makeRecord(1);
        const second = makeRecord(2);
        const third = makeRecord(3);
        const state = {
            connectionState: 'active', desiredConnected: true, socket: {}, warnings: [], lastError: '',
            visibleRecords: [first], pendingRecords: [], selectedRecordKey: null,
        };
        const listeners = {};
        const fakeClient = {
            on: vi.fn((type, callback) => { listeners[type] = callback; return () => {}; }),
            connect: vi.fn(), disconnect: vi.fn(), pause: vi.fn(), resume: vi.fn(), destroy: vi.fn(),
            getState: vi.fn(() => state),
            getRecord: vi.fn(key => state.visibleRecords.find(record => record._key === key) || null),
            selectRecord: vi.fn(key => { state.selectedRecordKey = key; }),
            getAttachment: vi.fn(),
        };
        context.KitProxy.protocolInteractionLive.createClient = vi.fn(() => fakeClient);
        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.className = 'protocol-items-page';
        root.dataset.runtimeState = '1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);
        const item = context.addProtocolItem(root, {
            id: 28, name: '选中淘汰 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1,
            req_cfg: {}, resp_cfg: {},
        });
        item.querySelector('.protocol-interaction-btn').click();
        const panel = context.document.querySelector('.protocol-interaction-drawer');
        panel.querySelector('[data-record-key="protocol:1:1"]').click();
        panel.querySelector('[data-record-key="protocol:1:1"]').focus();
        expect(context.document.activeElement.dataset.recordKey).toBe('protocol:1:1');

        state.visibleRecords = [second, third];
        state.selectedRecordKey = null;
        listeners.selectionChanged({ record: null, reason: 'selected_record_evicted' });
        listeners.recordEvicted({ record: first, reason: 'visible_capacity' });
        listeners.recordVisible({ record: third });

        expect(panel.querySelector('.interaction-record-item.is-selected')).toBeNull();
        expect(context.document.activeElement.dataset.recordKey).not.toBe('protocol:1:2');
        expect(context.document.activeElement.dataset.recordKey).not.toBe('protocol:1:3');
        context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 28);
    });

    /**
     * 测试思路：动画方向通过 CSS 关键帧表达，正序淡入必须从右向左，正序淘汰必须向右上淡出；倒序仍从下方淡入并向上淡出。
     * 示例：正序 enter 使用 translateX(16px)->translateX(0)，exit 使用 translateX(12px) translateY(-10px)。
     */
    it('正序插入和淘汰动画使用新的横向方向', () => {
        const css = readRepoFile('css/protocol_interaction_drawer.css');
        expect(css).toContain('transform: translateX(16px);');
        expect(css).toContain('transform: translateX(0);');
        expect(css).toContain('transform: translateX(12px) translateY(-10px);');
        expect(css).toContain('animation: interaction-record-exit var(--interaction-record-transition-duration) ease both;');
    });

    /**
     * 测试思路：可用的媒体附件要按 kind 生成浏览器原生预览控件，不能全部降级为下载链接。
     * 示例：audio/video 生成带 controls 的媒体元素，PDF 生成 iframe 预览和新窗口打开链接。
     */
    it('附件按音频视频和 PDF 类型提供原生预览', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);
        const attachments = ['audio', 'video', 'pdf'].map(kind => ({ attachment_id: kind, kind, size: 3, captured_size: 3, binary_available: true }));
        const record = {
            _key: 'protocol:1:1', scope: 'protocol', project_id: 1, protocol_id: 15,
            cache_instance_id: 1, seq: 1, protocol_type: 'http', time_ms: 1,
            peer_addr: 'peer', result: 'matched', _delivery: 'live',
            request: { body: { attachments } }, response: { body: { attachments: [] } },
        };
        const fakeClient = {
            on: vi.fn(() => () => {}), connect: vi.fn(), disconnect: vi.fn(), pause: vi.fn(), resume: vi.fn(), destroy: vi.fn(),
            getState: vi.fn(() => ({ connectionState: 'active', desiredConnected: true, socket: {}, warnings: [], lastError: '', visibleRecords: [record], pendingRecords: [], selectedRecordKey: record._key })),
            getRecord: vi.fn(() => record), selectRecord: vi.fn(),
            getAttachment: vi.fn((currentRecord, ref) => ({ objectUrl: `blob:${ref.kind}` })),
        };
        context.KitProxy.protocolInteractionLive.createClient = vi.fn(() => fakeClient);
        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.className = 'protocol-items-page';
        root.dataset.runtimeState = '1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);
        const item = context.addProtocolItem(root, { id: 15, name: '媒体 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1, req_cfg: {}, resp_cfg: {} });

        item.querySelector('.protocol-interaction-btn').click();
        const panel = context.document.querySelector('.protocol-interaction-drawer');
        panel.querySelector('[data-tab="attachments"]').click();
        expect(panel.querySelector('audio[controls]')).toBeTruthy();
        expect(panel.querySelector('video[controls]')).toBeTruthy();
        expect(panel.querySelector('iframe[src="blob:pdf"]')).toBeTruthy();
        expect(panel.querySelector('a[target="_blank"][rel="noopener"]')).toBeTruthy();
        context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 15);
    });

    /**
     * 测试思路：请求 Body 的实际类型和期望类型都为 image 时，概览与 Request Tab 共用可放大图片预览，保存动作由旁侧下载图标按钮触发，附件 Tab 只保留其他附件。
     * 示例：点击图片打开遮罩预览，点击下载按钮使用 download 属性保存；request-image 不出现在附件页，response-image 仍保留。
     */
    it('匹配的请求图片在概览和 Request 预览且不重复出现在附件页', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);
        const requestImage = {
            attachment_id: 'request-image', kind: 'image', size: 8, captured_size: 8, binary_available: true,
        };
        const responseImage = { attachment_id: 'response-image', kind: 'image', size: 9, captured_size: 9, binary_available: true };
        const record = {
            _key: 'protocol:1:1', scope: 'protocol', project_id: 1, protocol_id: 25,
            cache_instance_id: 1, seq: 1, protocol_type: 'http', time_ms: 1,
            peer_addr: 'peer', result: 'matched', _delivery: 'live',
            request: { head_text: 'POST /upload', body: { kind: 'image', expect_kind: 'image', attachments: [requestImage] } },
            response: { head_text: 'HTTP/1.1 200', body: { kind: 'image', expect_kind: 'image', attachments: [responseImage] } },
        };
        const fakeClient = {
            on: vi.fn(() => () => {}), connect: vi.fn(), disconnect: vi.fn(), pause: vi.fn(), resume: vi.fn(), destroy: vi.fn(),
            getState: vi.fn(() => ({ connectionState: 'active', desiredConnected: true, socket: {}, warnings: [], lastError: '', visibleRecords: [record], pendingRecords: [], selectedRecordKey: record._key })),
            getRecord: vi.fn(() => record), selectRecord: vi.fn(),
            getAttachment: vi.fn((currentRecord, ref) => ({
                objectUrl: `blob:${ref.attachment_id}`,
                bytes: ref.attachment_id === 'request-image'
                    ? new Uint8Array([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])
                    : new Uint8Array([1, 2, 3]),
            })),
        };
        context.KitProxy.protocolInteractionLive.createClient = vi.fn(() => fakeClient);
        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.className = 'protocol-items-page';
        root.dataset.runtimeState = '1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);
        const item = context.addProtocolItem(root, { id: 25, name: '请求图片 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1, req_cfg: {}, resp_cfg: {} });
        item.querySelector('.protocol-interaction-btn').click();
        const panel = context.document.querySelector('.protocol-interaction-drawer');

        expect(panel.querySelector('.summary-panel')).toBeTruthy();
        expect(panel.querySelector('.detail-grid')).toBeTruthy();
        const previewButton = panel.querySelector('[data-action="preview-image"]');
        const downloadButton = panel.querySelector('[data-action="download-image"]');
        expect(previewButton).toBeTruthy();
        expect(previewButton.querySelector('img[src="blob:request-image"]')).toBeTruthy();
        expect(downloadButton.querySelector('svg')).toBeTruthy();
        expect(downloadButton.closest('.interaction-code-heading')).toBeTruthy();
        previewButton.click();
        expect(panel.ownerDocument.querySelector('.interaction-image-preview[hidden]')).toBeNull();
        expect(panel.ownerDocument.querySelector('.interaction-image-preview-image[src="blob:request-image"]')).toBeTruthy();
        panel.ownerDocument.querySelector('[data-action="close-image-preview"]').click();
        expect(panel.ownerDocument.querySelector('.interaction-image-preview[hidden]')).toBeTruthy();

        panel.querySelector('[data-tab="request"]').click();
        expect(panel.querySelector('[data-action="preview-image"] img[src="blob:request-image"]')).toBeTruthy();
        expect(panel.querySelector('[data-action="download-image"] svg')).toBeTruthy();

        panel.querySelector('[data-tab="attachments"]').click();
        expect(panel.querySelector('[data-role="attachment-title"]')).toBeNull();
        expect(panel.textContent).not.toContain('request-image');
        expect(panel.textContent).toContain('response-image');
        context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 25);
    });

    /**
     * 测试思路：Multiform Body 的文本 part 直接读取 ref.text，二进制 part 通过完整 attachment_id 取 payload。
     * 示例：同一个请求包含一个文本字段和一个文件字段，概览、Request、附件三个视图都要分别展示。
     */
    it('实时抽屉按 Multiform part 展示文本和二进制附件', async () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);

        const textRef = {
            attachment_id: 'request.body.multiform.1:textpart',
            side: 'request', flag: 'request.body.multiform.1', kind: 'text', text: 'token=abc',
            size: 9, captured_size: 9, binary_available: false, truncated: false, sha1: 'text-sha1',
        };
        const binaryRef = {
            attachment_id: 'request.body.multiform.2:binarypart',
            side: 'request', flag: 'request.body.multiform.2', kind: 'binary', text: '',
            size: 4, captured_size: 4, binary_available: true, truncated: false, sha1: 'binary-sha1',
        };
        const record = {
            _key: 'protocol:1:1', scope: 'protocol', project_id: 1, protocol_id: 27,
            cache_instance_id: 1, seq: 1, protocol_type: 'http', time_ms: 1,
            peer_addr: 'peer', result: 'matched', _delivery: 'live',
            request: {
                head_text: 'POST /upload',
                body: {
                    kind: 'multiform', expect_kind: 'unknown', size: 13, captured_size: 13,
                    text: '', attachments: [textRef, binaryRef],
                },
            },
            response: { body: { kind: 'text', expect_kind: 'text', text: 'ok', attachments: [] } },
        };
        const fakeClient = {
            on: vi.fn(() => () => {}), connect: vi.fn(), disconnect: vi.fn(), pause: vi.fn(), resume: vi.fn(), destroy: vi.fn(),
            getState: vi.fn(() => ({
                connectionState: 'active', desiredConnected: false, socket: null, warnings: [], lastError: '',
                visibleRecords: [record], pendingRecords: [], selectedRecordKey: record._key,
            })),
            getRecord: vi.fn(() => record), selectRecord: vi.fn(),
            getAttachment: vi.fn((currentRecord, ref) => ref.attachment_id === binaryRef.attachment_id
                ? { objectUrl: 'blob:multiform-binary', bytes: new Uint8Array([1, 2, 3, 4]) }
                : null),
        };
        context.KitProxy.protocolInteractionLive.createClient = vi.fn(() => fakeClient);
        const writeText = vi.fn(() => Promise.resolve());
        Object.defineProperty(context.navigator, 'clipboard', {
            configurable: true,
            value: { writeText },
        });

        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.className = 'protocol-items-page';
        root.dataset.runtimeState = '1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);
        const item = context.addProtocolItem(root, {
            id: 27, name: 'Multiform HTTP', project_id: 1, type: 'HTTP',
            config_state: 1, status: 1, req_cfg: {}, resp_cfg: {},
        });
        item.querySelector('.protocol-interaction-btn').click();
        const panel = context.document.querySelector('.protocol-interaction-drawer');

        expect(panel.querySelectorAll('.interaction-multiform-part')).toHaveLength(2);
        expect(panel.querySelector('[data-attachment-id="request.body.multiform.1:textpart"]')).toBeTruthy();
        expect(panel.textContent).toContain('token=abc');
        const copyButton = panel.querySelector('[data-action="copy-code"][data-copy-text="token=abc"]');
        expect(copyButton).toBeTruthy();
        copyButton.click();
        await flushPromises(2);
        expect(writeText).toHaveBeenCalledWith('token=abc');
        expect(panel.ownerDocument.querySelector('.interaction-copy-toast').textContent).toBe('复制成功');
        expect(panel.ownerDocument.querySelector('.interaction-copy-toast').hidden).toBe(false);
        expect(panel.querySelector('[data-attachment-id="request.body.multiform.2:binarypart"] a[download="request.body.multiform.2:binarypart"]')).toBeTruthy();

        const firstPart = panel.querySelector('[data-role="multiform-part"]');
        const firstToggle = firstPart.querySelector('[data-action="toggle-multiform-part"]');
        expect(firstToggle.getAttribute('aria-expanded')).toBe('true');
        firstToggle.click();
        expect(firstPart.classList.contains('is-collapsed')).toBe(true);
        expect(firstToggle.getAttribute('aria-expanded')).toBe('false');
        firstToggle.click();
        expect(firstPart.classList.contains('is-collapsed')).toBe(false);

        panel.querySelector('[data-tab="request"]').click();
        expect(panel.querySelectorAll('.interaction-multiform-part')).toHaveLength(2);

        panel.querySelector('[data-tab="attachments"]').click();
        expect(panel.textContent).toContain('request · Part 1');
        expect(panel.textContent).toContain('request · Part 2');
        expect(panel.textContent).toContain('token=abc');
        expect(panel.querySelector('a[download="request.body.multiform.2:binarypart"]')).toBeTruthy();
        const attachmentPart = panel.querySelector('[data-role="multiform-part"]');
        expect(attachmentPart).toBeTruthy();
        const attachmentToggle = attachmentPart.querySelector('[data-action="toggle-multiform-part"]');
        attachmentToggle.click();
        expect(attachmentPart.classList.contains('is-collapsed')).toBe(true);
        context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 27);
    });

    /**
     * 测试思路：列表改为固定槽位的一屏展示模式后，不再依赖 scrollTop 保存历史浏览位置。
     * 示例：CSS 明确使用 overflow:hidden，新的记录重绘不会产生可滚动列表。
     */
    it('固定槽位列表不启用滚动条', () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        loadProtocolListScripts(context);
        const makeRecord = (seq, timeMs) => ({
            _key: `protocol:1:${seq}`, scope: 'protocol', project_id: 1, protocol_id: 16,
            cache_instance_id: 1, seq, protocol_type: 'http', time_ms: timeMs,
            peer_addr: 'peer', result: 'matched', _delivery: 'live',
            request: { meta: { method: 'GET', path: `/${seq}` }, body: { attachments: [] } },
            response: { body: { attachments: [] } },
        });
        const state = {
            connectionState: 'active', desiredConnected: true, socket: {}, warnings: [], lastError: '',
            visibleRecords: [makeRecord(1, 10), makeRecord(2, 20), makeRecord(3, 30)],
            pendingRecords: [], selectedRecordKey: null,
        };
        const fakeClient = {
            on: vi.fn(() => () => {}),
            connect: vi.fn(), disconnect: vi.fn(), pause: vi.fn(), resume: vi.fn(), destroy: vi.fn(),
            getState: vi.fn(() => state), getRecord: vi.fn(key => state.visibleRecords.find(record => record._key === key) || null),
            selectRecord: vi.fn(), getAttachment: vi.fn(),
        };
        context.KitProxy.protocolInteractionLive.createClient = vi.fn(() => fakeClient);
        const root = context.document.createElement('div');
        root.id = 'service-card-1';
        root.className = 'protocol-items-page';
        root.dataset.runtimeState = '1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);
        const item = context.addProtocolItem(root, { id: 16, name: '锚定 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1, req_cfg: {}, resp_cfg: {} });
        item.querySelector('.protocol-interaction-btn').click();

        const panel = context.document.querySelector('.protocol-interaction-drawer');
        const list = panel.querySelector('.interaction-record-items');
        expect(readRepoFile('css/protocol_interaction_drawer.css')).toContain('overflow: hidden;');
        expect(readRepoFile('css/protocol_interaction_drawer.css')).toContain('grid-template-rows: repeat(10, minmax(0, 1fr));');
        expect(list.querySelectorAll('.interaction-record-item')).toHaveLength(3);
        context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 16);
    });

    /**
     * 测试思路：抽屉收起只隐藏 UI，Mock client 应继续接收和合并记录，再次展开复用原 client。
     * 示例：open -> live_ready -> close drawer -> HTTP/TCP/Notice -> merge -> reopen，createClient 仍只调用 1 次且列表已有记录。
     */
    it('抽屉收起后 Mock 继续接收并在重新展开时复用状态', async () => {
        vi.useFakeTimers();
        try {
            const context = createBrowserContext('?apiMode=mock&projectId=1');
            loadCoreScripts(context);
            context.KitProxy.__disableAutoInitMain = true;
            context.KitProxy.__disableAutoInitProtocolItems = true;
            loadProtocolListScripts(context);
            const originalCreateClient = context.KitProxy.protocolInteractionLive.createClient;
            const createClient = vi.spyOn(context.KitProxy.protocolInteractionLive, 'createClient')
                .mockImplementation(options => originalCreateClient(options));
            const root = context.document.createElement('div');
            root.id = 'service-card-1';
            root.className = 'protocol-items-page';
            root.dataset.runtimeState = '1';
            root.innerHTML = '<div class="protocol-list"></div>';
            context.document.body.appendChild(root);
            const item = context.addProtocolItem(root, { id: 17, name: 'Mock 缓冲 HTTP', project_id: 1, type: 'HTTP', config_state: 1, status: 1, req_cfg: {}, resp_cfg: {} });
            const entryButton = item.querySelector('.protocol-interaction-btn');

            entryButton.click();
            vi.advanceTimersByTime(1);
            await Promise.resolve();
            context.document.querySelector('.protocol-interaction-drawer [data-action="connect"]').click();
            vi.advanceTimersByTime(1);
            await Promise.resolve();
            context.KitProxy.protocolInteractionDrawer.close();
            expect(context.KitProxy.protocolInteractionDrawer.isOpen()).toBe(false);

            vi.advanceTimersByTime(2100);
            await Promise.resolve();
            entryButton.click();
            const panel = context.document.querySelector('.protocol-interaction-drawer');
            expect(createClient).toHaveBeenCalledTimes(1);
            expect(panel.querySelectorAll('[data-record-key]').length).toBeGreaterThan(0);
            expect(panel.querySelector('[data-role="status"]').textContent).toBe('实时');
            context.KitProxy.protocolInteractionDrawer.cleanupProtocol(1, 17);
        } finally {
            vi.useRealTimers();
        }
    });

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
     * 测试思路：协议列表渲染完成后，只能根据当前标签有效 marker 自动重开指定协议项抽屉。
     * 示例：projectId=1、marker 指向 protocolId=1，列表卡片就绪后 restore 只调用一次且目标仍在当前列表。
     */
    it('协议列表就绪后按 marker 自动恢复目标抽屉', async () => {
        const context = createProtocolItemPageContext(1);
        loadCoreScripts(context);
        const user = await loginMockUser(context, { note: 'admin', loginType: 'admin', password: 'admin123' });
        context.KitProxy.auth.applyCurrentUser(user);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        context.delay = () => Promise.resolve();
        loadProtocolListScripts(context);
        const persistence = context.KitProxy.protocolInteractionPersistence;
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: context.KitProxy.config.apiBaseUrl,
            user: context.KitProxy.auth.getCurrentUser(),
            projectId: 1,
            protocolId: 1,
        });
        persistence.writeRestoreMarker(identity, { drawerOpen: true }, context.sessionStorage);
        const restore = vi.spyOn(context.KitProxy.protocolInteractionDrawer, 'restore').mockReturnValue(true);

        await context.KitProxy.protocolItemsPage.initPage();

        expect(context.document.querySelector('.protocol-item[data-protocol-id="1"]')).toBeTruthy();
        expect(restore).toHaveBeenCalledTimes(1);
        expect(restore.mock.calls[0][0]).toMatchObject({ projectId: 1, protocolId: 1 });
    });

    /**
     * 测试思路：marker 指向已删除或当前页不存在协议项时，不构造脱离列表实体的抽屉，并只清理当前恢复 marker。
     * 示例：protocolId=999 不在列表，initPage 后 restore 不调用，marker 被删除。
     */
    it('目标协议不存在时清理 marker 而不创建抽屉', async () => {
        const context = createProtocolItemPageContext(1);
        loadCoreScripts(context);
        const user = await loginMockUser(context, { note: 'admin', loginType: 'admin', password: 'admin123' });
        context.KitProxy.auth.applyCurrentUser(user);
        context.KitProxy.__disableAutoInitMain = true;
        context.KitProxy.__disableAutoInitProtocolItems = true;
        context.delay = () => Promise.resolve();
        loadProtocolListScripts(context);
        const persistence = context.KitProxy.protocolInteractionPersistence;
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: context.KitProxy.config.apiBaseUrl,
            user: context.KitProxy.auth.getCurrentUser(),
            projectId: 1,
            protocolId: 999,
        });
        persistence.writeRestoreMarker(identity, { drawerOpen: true }, context.sessionStorage);
        const restore = vi.spyOn(context.KitProxy.protocolInteractionDrawer, 'restore').mockReturnValue(true);

        await context.KitProxy.protocolItemsPage.initPage();

        expect(restore).not.toHaveBeenCalled();
        expect(persistence.readRestoreMarker(context.sessionStorage)).toBeNull();
        expect(context.document.querySelector('.protocol-interaction-drawer')).toBeNull();
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
        root.dataset.runtimeState = '1';
        root.innerHTML = '<div class="protocol-list"></div>';
        context.document.body.appendChild(root);

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

        const details = protocolItem.querySelector('.protocol-details');
        const toggleButton = protocolItem.querySelector('.protocol-toggle-btn');
        const interactionButton = protocolItem.querySelector('.protocol-interaction-btn');
        const headerActions = protocolItem.querySelector('.protocol-header-actions');
        expect(details.classList.contains('is-expanded')).toBe(false);
        expect(toggleButton.querySelector('.protocol-toggle-icon')).toBeTruthy();
        expect(toggleButton.getAttribute('aria-label')).toBe('展开协议项详情');
        expect(toggleButton.getAttribute('aria-expanded')).toBe('false');
        expect(interactionButton).toBeTruthy();
        expect(interactionButton.querySelector('.protocol-interaction-icon')).toBeTruthy();
        expect(interactionButton.getAttribute('aria-label')).toBe('查看协议项实时交互详情');
        expect(Array.from(headerActions.children).indexOf(interactionButton)).toBe(
            Array.from(headerActions.children).indexOf(toggleButton) - 1,
        );
        expect(protocolItem.querySelector('.delete-protocol-btn')).toBeTruthy();
        expect(readRepoFile('css/main.css')).toContain('chevron-down.svg');
        expect(readRepoFile('css/main.css')).toContain('activity.svg');
        expect(repoFileExists('assets/icons/chevron-down.svg')).toBe(true);
        expect(repoFileExists('assets/icons/activity.svg')).toBe(true);

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
        const cleanupProtocol = vi.fn();
        context.KitProxy.protocolInteractionDrawer = { cleanupProtocol };
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
        expect(cleanupProtocol).toHaveBeenCalledWith(1, 1);
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
        const cleanupProject = vi.spyOn(context.KitProxy.protocolInteractionDrawer, 'cleanupProject');

        await context.KitProxy.protocolItemsPage.initPage?.();
        await flushPromises(12);

        const runtimeButton = context.document.querySelector('.protocol-runtime-btn');
        expect(runtimeButton.disabled).toBe(false);

        context.document.querySelector('#protocol-service-meta .service-active-toggle').click();
        await flushPromises(12);

        expect(context.document.querySelector('.protocol-items-page').dataset.runtimeState).toBe('0');
        expect(cleanupProject).toHaveBeenCalledWith(1);
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
        expect(tcpGrid.querySelectorAll('.protocol-field')).toHaveLength(4);
        expect(tcpGrid.querySelectorAll('[data-field-name="fields"]').length).toBe(2);
        tcpGrid.querySelector('[data-field-name="fields"]').click();
        expect(context.document.querySelector('.modal-overlay')).toBeNull();
    });

    /**
     * 测试思路：请求侧 Body 内容配置暂时隐藏，响应侧 BodyEditor 保持完整可编辑。
     * 示例：请求 Body 弹窗只保留类型选择；响应 Body 选择 Binary 后仍显示普通字段配置。
     */
    it('协议项请求 Body 字段点击会弹出隐藏内容区的 BodyEditor modal', async () => {
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
        expect(context.document.querySelector('.body-editor-type-label').textContent).toBe('期望Body类型');
        expect(context.document.querySelector('.body-editor').classList.contains('is-content-hidden')).toBe(true);
        expect(context.document.querySelector('.body-editor').classList.contains('is-text-collapsed')).toBe(true);
        expect(context.document.querySelector('.body-editor-format').hidden).toBe(true);
        expect(context.document.querySelector('.body-editor-clear').hidden).toBe(true);
        expect(Array.from(context.document.querySelector('.body-editor-type').options).map(option => option.value)).toEqual([
            'none',
            'empty',
            'json',
            'xml',
            'text',
            'image',
            'binary',
        ]);
        expect(context.document.querySelector('.body-editor-type option[value="binary"]').disabled).toBe(false);
        expect(context.document.querySelector('.body-binary-clear-fields')).toBeTruthy();
        expect(context.document.querySelector('.body-binary-clear-fields').hidden).toBe(true);

        const editorType = context.document.querySelector('.body-editor-type');
        editorType.value = 'none';
        editorType.dispatchEvent(new context.Event('change', { bubbles: true }));
        expect(context.document.querySelector('.body-editor').classList.contains('is-content-hidden')).toBe(true);
        expect(context.document.querySelector('.body-editor').classList.contains('is-text-collapsed')).toBe(true);

        editorType.value = 'image';
        editorType.dispatchEvent(new context.Event('change', { bubbles: true }));
        expect(context.document.querySelector('.body-editor').classList.contains('is-content-hidden')).toBe(true);
        expect(context.document.querySelector('.body-editor').classList.contains('is-text-collapsed')).toBe(true);

        editorType.value = 'binary';
        editorType.dispatchEvent(new context.Event('change', { bubbles: true }));
        expect(context.document.querySelector('.edit-body-modal').classList.contains('is-binary-body-mode')).toBe(false);
        expect(context.document.querySelector('.edit-body-modal').classList.contains('config-pattern-modal')).toBe(false);
        expect(context.document.querySelector('.edit-body-modal').classList.contains('is-item-pattern')).toBe(false);
        expect(context.document.querySelector('.body-editor').classList.contains('is-binary-mode')).toBe(false);
        expect(context.document.querySelector('.body-binary-clear-fields').hidden).toBe(true);
        expect(context.document.querySelector('.body-editor-binary-wrap .pattern-layout-section')).toBeNull();
        expect(context.document.querySelector('.body-editor-binary-wrap .pattern-field-info')).toBeNull();

        context.document.querySelector('.edit-body-modal .close-modal').click();
        await flushPromises(4);

        protocolItem.querySelector('.response-body').click();
        await flushPromises(12);

        expect(context.document.querySelector('.edit-body-modal')).toBeTruthy();
        expect(context.document.querySelector('.body-editor-type-label').textContent).toBe('Body类型');
        expect(context.document.querySelector('.body-editor').classList.contains('is-content-hidden')).toBe(false);
        expect(context.document.querySelector('.body-editor').classList.contains('is-text-collapsed')).toBe(false);
        expect(context.document.querySelector('.body-editor-format').hidden).toBe(false);
        expect(context.document.querySelector('.body-editor-clear').hidden).toBe(false);

        const responseEditorType = context.document.querySelector('.body-editor-type');
        responseEditorType.value = 'binary';
        responseEditorType.dispatchEvent(new context.Event('change', { bubbles: true }));
        expect(context.document.querySelector('.edit-body-modal').classList.contains('is-binary-body-mode')).toBe(true);
        expect(context.document.querySelector('.edit-body-modal').classList.contains('config-pattern-modal')).toBe(true);
        expect(context.document.querySelector('.edit-body-modal').classList.contains('is-item-pattern')).toBe(true);
        expect(context.document.querySelector('.body-editor').classList.contains('is-binary-mode')).toBe(true);
        expect(context.document.querySelector('.body-binary-clear-fields').hidden).toBe(false);
        expect(context.document.querySelector('.body-editor-binary-wrap .pattern-layout-section')).toBeTruthy();
        expect(context.document.querySelector('.body-editor-binary-wrap .pattern-field-info')).toBeTruthy();
        expect(context.document.querySelector('.body-editor-binary-wrap .body-editor-binary-add-field')).toBeNull();
        expect(context.document.querySelector('.body-editor-binary-wrap .pattern-section-title').textContent).toContain('字节布局预览');
        expect(context.document.querySelector('.body-editor-binary-wrap .pattern-field-grid-labels').textContent).toContain('名称');
        expect(context.document.querySelector('.body-editor-binary-wrap .pattern-field-grid-labels').textContent).toContain('类型');
        expect(context.document.querySelector('.body-editor-binary-wrap .pattern-field-grid-labels').textContent).toContain('角色');
        expect(context.document.querySelector('.body-editor-binary-wrap .pattern-wire-hex-input')).toBeTruthy();
        expect(context.document.querySelector('.body-editor-binary-wrap .pattern-value-editor-input')).toBeTruthy();
        expect(context.document.querySelector('.body-editor-binary-wrap .pattern-field-value-display-btn')).toBeTruthy();
        const binaryActionButtons = Array.from(context.document.querySelectorAll('.body-editor-binary-wrap .pattern-cell-actions button'));
        expect(binaryActionButtons).toHaveLength(4);
        expect(binaryActionButtons.map(button => button.textContent.trim())).toEqual(['', '', '', '']);
        expect(binaryActionButtons.every(button => button.querySelector('.pattern-action-icon'))).toBe(true);
        expect(context.document.querySelector('.body-editor-binary-wrap .pattern-cell-role .pattern-fixed-value-btn').hidden).toBe(true);
        expect(context.document.querySelector('.body-editor-binary-wrap .pattern-cell-actions .pattern-fixed-value-btn')).toBeNull();
        expect(Array.from(context.document.querySelector('.body-editor-binary-wrap .pattern-field-role').options).map(option => option.value)).toEqual(['common']);
        expect(context.document.querySelector('.body-editor-binary-wrap .pattern-cell-byte-pos label').textContent).toBe('Byte起始位置');
        expect(context.document.querySelector('.body-editor-binary-wrap .pattern-field-byte-len').disabled).toBe(true);
        expect(context.document.querySelectorAll('.body-editor-binary-wrap .pattern-field-container').length).toBeGreaterThan(0);

        context.document.querySelector('.body-binary-clear-fields').click();
        const resetBodyFields = context.document.querySelectorAll('.body-editor-binary-wrap .pattern-field-container');
        expect(resetBodyFields).toHaveLength(1);
        expect(resetBodyFields[0].querySelector('.pattern-field-name').value).toBe('');
        expect(resetBodyFields[0].querySelector('.pattern-field-byte-pos').value).toBe('0');
        expect(resetBodyFields[0].querySelector('.pattern-field-byte-len').value).toBe('');
        expect(resetBodyFields[0].querySelector('.pattern-field-type').value).toBe('');
        expect(resetBodyFields[0].querySelector('.pattern-field-role').value).toBe('common');
        expect(context.document.querySelector('.body-editor-binary-count').textContent).toBe('1 个普通字段');
        expect(context.document.querySelector('.body-editor-binary-preview').textContent).toContain('字段1');
    });

    /**
     * 测试思路：协议卡片 Body 弹窗和独立协议项表单应使用同一套 BodyEditor 控件尺寸。
     * 示例：BodyEditor textarea 不能被普通弹窗样式覆盖，Body 类型 select 闭合状态要有稳定高度和行高，避免选中文字被裁切。
     */
    it('协议卡片 Body 弹窗字体样式和表单页 BodyEditor 对齐', () => {
        const mainCss = readRepoFile('css/main.css');
        const modalCss = readRepoFile('css/modal_styles.css');

        expect(mainCss).toContain('.protocol-body-section .body-editor-input-wrap');
        expect(mainCss).toContain('.body-editor .body-editor-type');
        expect(mainCss).toContain('min-height: 44px;');
        expect(mainCss).toContain('line-height: 20px;');
        expect(mainCss).toContain('appearance: none;');
        expect(mainCss).toContain('.body-editor.is-text-collapsed .body-editor-input-wrap');
        expect(mainCss).toContain('.protocol-body-section.is-request-body-content-hidden .body-editor-input-wrap');
        expect(mainCss).toContain('.protocol-body-section.is-request-body-content-hidden .body-editor-binary-wrap');
        expect(mainCss).toContain('.body-editor-binary-wrap');
        expect(mainCss).not.toContain('.body-editor-binary-field-info .pattern-field-grid-labels');
        expect(mainCss).not.toContain('.body-editor-binary-field-info .pattern-field {');
        expect(mainCss).toContain('.pattern-wire-hex-input');
        expect(mainCss).toContain('.pattern-value-editor-input');
        expect(readRepoFile('js/tcp_pattern_modal.js')).toContain('function createPatternFieldEditorSectionHTML');
        expect(readRepoFile('js/tcp_pattern_modal.js')).toContain('createPatternFieldEditorSectionHTML({');
        expect(readRepoFile('js/body_editor.js')).toContain('tcpEditor.createPatternFieldEditorSectionHTML');
        expect(modalCss).toContain('.edit-body-modal-overlay');
        expect(modalCss).toContain('padding: 36px 24px;');
        expect(modalCss).toContain('.edit-body-modal .modal-body > .form-group');
        expect(modalCss).toContain('.edit-body-modal.is-binary-body-mode .modal-body > .form-group');
        expect(modalCss).toContain('.edit-body-modal.is-binary-body-mode .body-editor-binary-field-info');
        expect(modalCss).toContain('.edit-body-modal .form-actions');
        expect(modalCss).toContain('overflow: hidden;');
        expect(modalCss).toContain('flex: 0 0 auto;');
        expect(modalCss).toContain('--pattern-actions-col: 160px;');
        expect(modalCss).toContain('--pattern-field-grid: var(--pattern-byte-pos-col) var(--pattern-name-col) var(--pattern-byte-len-col) var(--pattern-type-col) var(--pattern-role-col) var(--pattern-value-col) var(--pattern-actions-col);');
        expect(modalCss).toContain('.config-pattern-modal .pattern-cell-role.has-fixed-value-control .pattern-role-control');
        expect(modalCss).toContain('grid-template-columns: minmax(0, 1fr) 34px;');
        expect(modalCss).not.toContain('width: calc(100% + 40px);');
        expect(modalCss).toContain('mask: url("../assets/icons/plus.svg")');
        expect(modalCss).toContain('mask: url("../assets/icons/arrow-up.svg")');
        expect(modalCss).toContain('mask: url("../assets/icons/arrow-down.svg")');
        expect(modalCss).toContain('mask: url("../assets/icons/trash-2.svg")');
        expect(modalCss).not.toContain('mask: url("../assets/icons/fixed-value.svg")');
        expect(modalCss).toContain('.config-pattern-modal .pattern-field-toolbar button,\n.config-pattern-modal .pattern-field-actions button,\n.config-pattern-modal .pattern-fixed-value-btn');
        expect(modalCss).toContain('.config-pattern-modal .pattern-field-actions button,\n.config-pattern-modal .pattern-field-container .del-field-btn,\n.config-pattern-modal .pattern-fixed-value-btn');
        expect(modalCss).toContain('.config-pattern-modal .pattern-fixed-value-btn.has-fixed-value');
        expect(modalCss).toContain('color: #dc2626;');
        expect(modalCss).toContain('color: #087443;');
        expect(modalCss).toContain('.config-pattern-modal .pattern-field-container.is-fixed-value-popover-open');
        expect(modalCss).toContain('z-index: 40;');
        expect(modalCss).toContain('--pattern-modal-safe-space: 48px;');
        expect(modalCss).toContain('--pattern-scrollbar-width: 8px;');
        expect(modalCss).toContain('width: min(1120px, calc(100vw - var(--pattern-modal-safe-space)));');
        expect(modalCss).toContain('padding: 8px calc(14px + var(--pattern-scrollbar-width)) 8px 14px;');
        expect(modalCss).toContain('overflow-y: scroll;');
        expect(modalCss).toContain('scrollbar-gutter: stable;');
        expect(modalCss).toContain('.config-pattern-modal .pattern-byte-layout');
        expect(modalCss).toContain('height: var(--pattern-scrollbar-width);');
        expect(modalCss).toContain('@media (max-width: 1020px)');
        expect(modalCss).toContain('.edit-body-modal:not(.config-pattern-modal)');
        expect(modalCss).not.toContain('.edit-body-modal.is-binary-body-mode {\n    --pattern-field-grid');
        expect(modalCss).toContain('.edit-body-modal .body-editor-binary-wrap:not(.is-collapsed)');
        expect(modalCss).toContain('height: min(920px, calc(100vh - 72px));');
        expect(modalCss).toContain('.edit-body-modal.is-binary-body-mode .modal-body > .form-group > #body-editor-host');
        expect(modalCss).toContain('scroll-padding-bottom: 12px;');
        expect(modalCss).toContain('.edit-body-modal.is-binary-body-mode .body-editor-binary-preview');
        expect(modalCss).toContain('overflow-x: auto;');
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
     * 测试思路：HTTP 卡片详情只隐藏请求 Headers，响应 Headers 仍复用配置弹窗。
     * 示例：请求行只保留 method/path/请求 Body，响应行包含 status/响应 Headers/响应 Body；点击响应 Headers 保存后只更新 resp_cfg.headers。
     */
    it('协议项 HTTP 卡片隐藏请求 Headers 并保留响应 Headers 配置', async () => {
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
        expect(grid.querySelector('.http-request-config .http-empty-slot')).toBeTruthy();
        expect(grid.querySelector('.http-response-config').querySelectorAll('.protocol-field')).toHaveLength(4);
        expect(grid.querySelector('.http-response-config .http-empty-slot')).toBeTruthy();
        expect(grid.querySelector('.http-request-config').textContent).not.toContain('请求 Headers');
        expect(grid.querySelector('.http-response-config').textContent).toContain('响应 Headers');
        const mainCss = readRepoFile('css/main.css');
        expect(mainCss).toContain('.http-details-row');
        expect(mainCss).toContain('grid-template-columns: repeat(4, minmax(0, 1fr));');
        expect(mainCss).toContain('.details-grid.tcp');
        expect(mainCss).toContain('.details-grid.tcp .protocol-field');
        expect(mainCss).toContain('.details-grid.http .protocol-field.http-empty-slot');
        expect(mainCss).not.toContain('.http-details-row.http-request-config');
        expect(mainCss).not.toContain('.http-details-row-title');
        expect(grid.querySelector('[data-http-headers-side="request"]')).toBeNull();
        expect(grid.querySelector('[data-http-headers-side="response"] .value').textContent).toBe('已设置 1 条');

        grid.querySelector('[data-http-headers-side="response"]').click();
        const modal = context.document.querySelector('.http-headers-modal');
        expect(modal).toBeTruthy();
        expect(modal.querySelector('.modal-header').textContent).toContain('配置响应 Headers');
        modal.querySelector('.http-header-name').value = 'X-Resp-New';
        modal.querySelector('.http-header-value').value = 'new';
        modal.querySelector('.confirm-btn').click();
        await flushPromises(8);

        expect(updateCfg).toHaveBeenCalledWith(1, 1, 2, {
            headers: { 'X-Resp-New': 'new' },
        });
        expect(context.document.querySelector('.http-headers-modal')).toBeNull();
        expect(grid.querySelector('[data-http-headers-side="response"] .value').textContent).toBe('已设置 1 条');
        expect(JSON.parse(grid.querySelector('[data-http-headers-side="response"]').dataset.headers)).toEqual({
            'X-Resp-New': 'new',
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
     * 测试思路：协议项表单页请求侧暂时隐藏 Body 内容配置，响应侧仍保持可编辑。
     * 示例：请求 Body 选 Binary 不展示字段配置但仍提交空内容，切到响应 Tab 后可正常输入文本。
     */
    it('表单页请求 Body Binary 提交空内容且响应侧保持可编辑', async () => {
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

        await context.KitProxy.protocolItemForm.initPage();
        await flushPromises(8);

        context.document.getElementById('protocol-item-name').value = '新增HTTP特殊请求Body';
        context.document.querySelector('input[name="request-method"][value="POST"]').checked = true;
        context.document.getElementById('request-path').value = '/api/special';

        const state = context.KitProxy.protocolItemForm.pageState;
        const editor = state.bodyEditor;
        expect(context.document.querySelector('.protocol-body-section').classList.contains('is-request-body-content-hidden')).toBe(true);
        expect(context.document.querySelector('.body-editor-type-label').textContent).toBe('期望Body类型');
        expect(context.document.querySelector('.body-editor').classList.contains('is-content-hidden')).toBe(true);
        expect(context.document.querySelector('.body-editor').classList.contains('is-text-collapsed')).toBe(true);
        expect(context.document.querySelector('.body-editor-format').hidden).toBe(true);
        expect(context.document.querySelector('.body-editor-clear').hidden).toBe(true);
        expect(Array.from(context.document.querySelector('.body-editor-type').options).map(option => option.value)).toEqual([
            'none',
            'empty',
            'json',
            'xml',
            'text',
            'image',
            'binary',
        ]);

        editor.setType('binary');
        editor.setValue('旧请求内容');
        expect(context.document.querySelector('#protocol-body-editor-host .body-editor-binary-wrap').classList.contains('config-pattern-modal')).toBe(true);
        expect(context.document.querySelector('#protocol-body-editor-host .body-editor-binary-wrap').classList.contains('body-editor-binary-pattern-scope')).toBe(true);
        expect(context.document.querySelector('#protocol-body-editor-host .body-editor').classList.contains('is-binary-mode')).toBe(false);
        expect(context.document.querySelector('#protocol-body-editor-host .body-editor-binary-wrap').classList.contains('is-collapsed')).toBe(true);
        expect(context.document.querySelector('#protocol-body-editor-host .body-editor-binary-wrap .pattern-field-grid-labels')).toBeNull();
        expect(context.document.querySelector('#protocol-body-editor-host .body-editor-binary-wrap .pattern-list')).toBeNull();
        context.KitProxy.protocolItemForm.setActiveBodyTab('response');
        expect(context.document.querySelector('.protocol-body-section').classList.contains('is-request-body-content-hidden')).toBe(false);
        expect(context.document.querySelector('.body-editor-type-label').textContent).toBe('Body类型');
        expect(context.document.querySelector('.body-editor').classList.contains('is-content-hidden')).toBe(false);
        expect(context.document.querySelector('.body-editor').classList.contains('is-text-collapsed')).toBe(false);
        expect(context.document.querySelector('.body-editor-format').hidden).toBe(false);
        expect(context.document.querySelector('.body-editor-clear').hidden).toBe(false);
        expect(Array.from(context.document.querySelector('.body-editor-type').options).map(option => option.value)).toEqual([
            'none',
            'empty',
            'json',
            'xml',
            'text',
            'image',
            'binary',
        ]);
        editor.setType('text');
        editor.setValue('ok');

        const data = context.KitProxy.protocolItemForm.collectFormData();
        const payload = context.KitProxy.protocolItemForm.buildAddPayload(data);

        expect(payload.cfg_header.req_body_type).toBe('binary');
        expect(payload.request_body).toBe('');
        expect(payload.cfg_header.resp_body_type).toBe('text');
        expect(payload.response_body).toBe('ok');
    });

    /**
     * 测试思路：旧新增协议弹窗暂时隐藏请求侧 Body 内容导入入口，响应侧不受影响。
     * 示例：HTTP 弹窗请求 Body 仍可选择 Binary 并提交空内容，响应 Body 仍可导入文本。
     */
    it('旧 HTTP 新增协议弹窗请求 Body 隐藏导入入口并提交空内容', async () => {
        const context = createBrowserContext('?apiMode=mock&projectId=1');
        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/protocol_item.js',
            'js/add_protocol_modal.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));

        let submittedProtocol = null;
        context.addHTTPProtocol = vi.fn((serviceCard, protocol) => {
            submittedProtocol = protocol;
            return Promise.resolve({ id: 99 });
        });

        const serviceCard = context.document.createElement('div');
        serviceCard.id = 'service-card-1';
        context.document.body.appendChild(serviceCard);

        const modal = context.httpProtocolModal.create(serviceCard);
        context.document.body.appendChild(modal);

        expect(modal.querySelector('label[for="request-body-type"]').textContent).toBe('期望Body类型');
        expect(modal.querySelector('.import-btn[data-target="request-body-type"]').hidden).toBe(true);
        expect(modal.querySelector('.import-btn[data-target="request-body-type"]').disabled).toBe(true);
        expect(modal.querySelector('.import-btn[data-target="response-body-type"]').hidden).toBe(false);
        expect(modal.querySelector('.import-btn[data-target="response-body-type"]').disabled).toBe(false);
        expect(Array.from(modal.querySelector('#request-body-type').options).map(option => option.value)).toEqual([
            'none',
            'empty',
            'json',
            'xml',
            'text',
            'image',
            'binary',
        ]);
        expect(modal.querySelector('#request-body-type option[value="binary"]').disabled).toBe(false);
        expect(Array.from(modal.querySelector('#response-body-type').options).map(option => option.value)).toEqual([
            'none',
            'empty',
            'json',
            'xml',
            'text',
            'image',
            'binary',
        ]);
        expect(modal.querySelector('#response-body-type option[value="binary"]').disabled).toBe(false);

        modal.querySelector('#protocol-item-name').value = '旧弹窗HTTP';
        modal.querySelector('input[name="request-method"][value="POST"]').checked = true;
        modal.querySelector('#request-path').value = '/api/legacy';
        modal.querySelector('#response-status-code').value = '200';
        modal.querySelector('#request-body-type').value = 'binary';
        modal.querySelector('#response-body-type').value = 'text';
        modal.querySelector('#response-body-type').dataset.importedContent = 'ok';
        modal.querySelector('#add-protocol-item-form')
            .dispatchEvent(new context.Event('submit', { bubbles: true, cancelable: true }));
        await flushPromises(8);

        expect(context.addHTTPProtocol).toHaveBeenCalledTimes(1);
        expect(submittedProtocol.cfg_header.req_body_type).toBe('binary');
        expect(submittedProtocol.cfg_header.resp_body_type).toBe('text');
        expect(submittedProtocol.request_body).toBe('');
        expect(submittedProtocol.response_body).toBe('ok');
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

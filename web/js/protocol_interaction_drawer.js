(function initProtocolInteractionDrawer(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});
    const Live = KitProxy.protocolInteractionLive;
    if (!Live) return;

    const FILTERS = Object.freeze({
        all: '全部',
        protocol: '协议项',
        notice: '项目 Notice',
        error: '异常',
        image: '图片',
    });
    const TABS = Object.freeze({
        overview: '概览',
        request: 'Request',
        response: 'Response',
        raw: 'Raw/Hex',
        attachments: '附件',
    });
    const SORT_ASC_ICON = '<svg viewBox="0 0 24 24" aria-hidden="true" focusable="false"><path d="M6 18h12"></path><path d="M6 12h8"></path><path d="M6 6h4"></path><path d="M18 18V6"></path><path d="m14 10 4-4 4 4"></path></svg>';
    const SORT_DESC_ICON = '<svg viewBox="0 0 24 24" aria-hidden="true" focusable="false"><path d="M6 6h12"></path><path d="M6 12h8"></path><path d="M6 18h4"></path><path d="M18 6v12"></path><path d="m14 14 4 4 4-4"></path></svg>';
    const CLEAR_ICON = '<svg viewBox="0 0 24 24" aria-hidden="true" focusable="false"><path d="M3 6h18"></path><path d="M8 6V4h8v2"></path><path d="m19 6-1 14H6L5 6"></path><path d="M10 11v5"></path><path d="M14 11v5"></path></svg>';
    const DOWNLOAD_ICON = '<svg viewBox="0 0 24 24" aria-hidden="true" focusable="false"><path d="M12 3v12"></path><path d="m7 10 5 5 5-5"></path><path d="M5 21h14"></path></svg>';
    const COPY_ICON = '<svg viewBox="0 0 24 24" aria-hidden="true" focusable="false"><rect x="8" y="8" width="11" height="11" rx="1.5"></rect><path d="M16 8V5.5A1.5 1.5 0 0 0 14.5 4h-9A1.5 1.5 0 0 0 4 5.5v9A1.5 1.5 0 0 0 5.5 16H8"></path></svg>';
    const RECORD_TRANSITION_MS = Object.freeze({
        slow: 820,
        medium: 650,
        fast: 380,
    });
    const RECORD_TRANSITION_CLEAR_GAP_MS = 40;
    const DRAWER_TRANSITION_MS = 260;
    const clients = new Map();
    const viewStates = new Map();
    let drawer = null;
    let activeEntry = null;

    function viewStateFor(key) {
        let state = viewStates.get(key);
        if (!state) {
            state = {
                followLive: true,
                mobileDetail: false,
                sortDirection: 'asc',
                readRecordKeys: new Set(),
                enteringRecordKeys: new Set(),
                exitingRecords: new Map(),
                transitionBusy: false,
                transitionPaused: false,
                transitionTimer: null,
            };
            viewStates.set(key, state);
        }
        return state;
    }

    function id(value) {
        const number = Number(value);
        return Number.isInteger(number) && number > 0 ? number : 0;
    }

    function escapeText(value) {
        return String(value == null ? '' : value);
    }

    function statusLabel(state) {
        return {
            disconnected: '未连接',
            connecting: '连接中',
            catching_up: '补发中',
            active: '实时',
            pausing: '暂停中',
            paused: '已暂停',
            resuming: '恢复中',
            reconnect_wait: '等待重连',
            disconnecting: '断开中',
            error: '连接错误',
        }[state] || '未连接';
    }

    function resultLabel(result) {
        return {
            matched: '成功',
            request_mismatch: '请求不匹配',
            protocol_not_found: '协议项未找到',
            serialize_error: '序列化失败',
            parse_error: '解析失败',
            route_not_found: '路由未找到',
            method_not_allowed: '方法不允许',
            internal_error: '内部错误',
        }[result] || String(result || '未知');
    }

    function resultTone(result) {
        const value = String(result || '');
        if (value === 'matched') return 'matched';
        if (value.includes('not_found') || value.includes('mismatch')) return 'warn';
        return 'error';
    }

    function isAdminViewer() {
        if (KitProxy.auth && typeof KitProxy.auth.isCurrentUserAdmin === 'function') {
            return KitProxy.auth.isCurrentUserAdmin();
        }
        return Boolean(global.document && global.document.body
            && global.document.body.dataset.userRole === 'admin');
    }

    function protocolTypeLabel(type) {
        return String(type || '').toUpperCase() || '未知协议';
    }

    function recordTitle(record) {
        if (record.scope === 'project') {
            const reason = String(record.error_message || '')
                .replace(/^项目(?:级)?\s*Notice\s*[:：]?\s*/i, '')
                .trim();
            return reason || resultLabel(record.result) || '项目级请求未匹配协议项';
        }
        const requestMeta = record.request && record.request.meta;
        const responseMeta = record.response && record.response.meta;
        const protocolType = String(record.protocol_type || '').toLowerCase();
        if (protocolType === 'http' || (!protocolType && requestMeta)) {
            const method = requestMeta && requestMeta.method ? requestMeta.method : 'HTTP';
            const target = requestMeta && (requestMeta.path || requestMeta.target);
            const status = responseMeta && responseMeta.status_code;
            return `${method} ${target || 'raw_packet'}${status ? ` ${status}` : ''}`;
        }
        const functionCode = (requestMeta && requestMeta.function_code)
            || (responseMeta && responseMeta.function_code);
        return `FUNC ${functionCode || 'unknown'}`;
    }

    function bodySummary(record) {
        const body = record.request && record.request.body;
        if (!body) return 'empty · 0B';
        const size = Number(body.size || 0);
        const formattedSize = size > 1024 * 1024
            ? `${(size / 1024 / 1024).toFixed(1)}MB`
            : size > 1024 ? `${(size / 1024).toFixed(1)}KB` : `${size}B`;
        return `${body.kind || 'empty'} · ${formattedSize}`;
    }

    function recordHasImage(record) {
        return ['request', 'response'].some(side => {
            const value = record && record[side];
            const refs = value && value.body && value.body.attachments;
            return Array.isArray(refs) && refs.some(ref => String(ref.kind || '').toLowerCase() === 'image');
        });
    }

    function isErrorRecord(record) {
        return !record || record.scope === 'project' || record.result !== 'matched';
    }

    function matchesFilter(record, filter) {
        if (filter === 'protocol') return record.scope === 'protocol' && record.result === 'matched';
        if (filter === 'notice') return record.scope === 'project';
        if (filter === 'error') return isErrorRecord(record);
        if (filter === 'image') return recordHasImage(record);
        return true;
    }

    function formatTime(timeMs) {
        const value = Number(timeMs);
        if (!Number.isFinite(value)) return '未知时间';
        try {
            return new Date(value).toLocaleTimeString('zh-CN', { hour12: false });
        } catch (error) {
            return String(value);
        }
    }

    function compareRecordTime(left, right, direction) {
        const leftTime = Number(left && left.time_ms);
        const rightTime = Number(right && right.time_ms);
        const leftValid = Number.isFinite(leftTime);
        const rightValid = Number.isFinite(rightTime);
        let comparison = 0;
        if (leftValid && rightValid) comparison = leftTime - rightTime;
        else if (leftValid) comparison = -1;
        else if (rightValid) comparison = 1;
        if (comparison === 0) comparison = String(left && left._key || '').localeCompare(String(right && right._key || ''));
        // 交互列表的“正序”定义为新事件在前，“倒序”定义为新事件在后。
        return direction === 'asc' ? -comparison : comparison;
    }

    function sortLabel(direction) {
        return direction === 'asc' ? '正序' : '倒序';
    }

    function recordTransitionMs(speed) {
        const normalized = String(speed || '').toLowerCase();
        return RECORD_TRANSITION_MS[normalized] || RECORD_TRANSITION_MS.medium;
    }

    function addEnteringRecord(viewState, recordKey) {
        if (!recordKey) return false;
        if (viewState.transitionPaused) return false;
        const canJoinExitTransition = viewState.transitionBusy
            && viewState.exitingRecords.size > 0
            && viewState.enteringRecordKeys.size === 0;
        if (viewState.transitionBusy && !canJoinExitTransition) return false;
        startRecordTransition(viewState);
        viewState.enteringRecordKeys.add(recordKey);
        return true;
    }

    function addExitingRecord(viewState, record) {
        if (!record || !record._key || viewState.transitionPaused || viewState.transitionBusy) return false;
        startRecordTransition(viewState);
        viewState.exitingRecords.set(record._key, record);
        return true;
    }

    function startRecordTransition(viewState) {
        if (viewState.transitionBusy) return;
        viewState.transitionBusy = true;
        const client = activeClient();
        const state = client && typeof client.getState === 'function' ? client.getState() : null;
        const transitionMs = recordTransitionMs(state && state.mergeSpeed);
        viewState.transitionTimer = global.setTimeout(() => {
            viewState.transitionTimer = null;
            viewState.transitionBusy = false;
            viewState.enteringRecordKeys.clear();
            viewState.exitingRecords.clear();
            if (activeEntry && viewStateFor(activeEntry.key) === viewState) render();
        }, transitionMs + RECORD_TRANSITION_CLEAR_GAP_MS);
    }

    function clearTransientAnimations(viewState) {
        if (viewState.transitionTimer) global.clearTimeout(viewState.transitionTimer);
        viewState.transitionTimer = null;
        viewState.transitionBusy = false;
        viewState.enteringRecordKeys.clear();
        viewState.exitingRecords.clear();
    }

    function pauseRecordTransitions(viewState) {
        if (!viewState) return;
        clearTransientAnimations(viewState);
        viewState.transitionPaused = true;
        if (!drawer) return;
        drawer.panel.querySelectorAll('.interaction-record-item').forEach(item => {
            item.classList.remove(
                'is-entering-front', 'is-entering-end',
                'is-exiting', 'is-exiting-asc', 'is-exiting-desc',
            );
        });
    }

    function resumeRecordTransitions(viewState) {
        if (viewState) viewState.transitionPaused = false;
    }

    function formatJson(value) {
        if (value == null || value === '') return '';
        if (typeof value === 'string') {
            try {
                return JSON.stringify(JSON.parse(value), null, 2);
            } catch (error) {
                return value;
            }
        }
        try {
            return JSON.stringify(value, null, 2);
        } catch (error) {
            return String(value);
        }
    }

    function setText(element, value) {
        if (element) element.textContent = escapeText(value);
    }

    function makeButton(label, className, title) {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = className || '';
        button.textContent = label;
        if (title) {
            button.title = title;
            button.setAttribute('aria-label', title);
        }
        return button;
    }

    function createDrawerDom() {
        if (drawer) return drawer;
        const backdrop = document.createElement('div');
        backdrop.className = 'protocol-interaction-backdrop';
        backdrop.hidden = true;

        const panel = document.createElement('section');
        panel.className = 'protocol-interaction-drawer';
        panel.dataset.testid = 'protocol-interaction-drawer';
        panel.hidden = true;
        panel.setAttribute('role', 'dialog');
        panel.setAttribute('aria-modal', 'true');
        panel.setAttribute('aria-labelledby', 'protocol-interaction-drawer-title');
        panel.innerHTML = `
            <header class="interaction-drawer-header" data-testid="protocol-interaction-header">
                <div class="interaction-drawer-heading">
                    <h2 id="protocol-interaction-drawer-title"></h2>
                    <p class="interaction-drawer-meta">
                        <span class="interaction-type-badge" data-role="protocol-type"></span>
                        <span data-role="protocol-meta"></span>
                    </p>
                </div>
                <div class="interaction-drawer-actions">
                    <button type="button" data-testid="protocol-interaction-fullscreen" data-action="fullscreen" title="全屏/还原" aria-label="全屏/还原">全屏</button>
                    <button type="button" data-testid="protocol-interaction-close" data-action="close" title="收起抽屉" aria-label="收起抽屉">×</button>
                </div>
            </header>
            <div class="interaction-drawer-status" data-testid="protocol-interaction-status">
                <div class="interaction-status-history" data-role="history"></div>
                <div class="interaction-status-main"><span class="interaction-status-dot"></span><strong data-testid="protocol-interaction-state" data-role="status"></strong><span data-role="warning"></span></div>
                <div class="interaction-status-actions">
                    <button type="button" data-testid="protocol-interaction-connect" data-action="connect">连接</button>
                    <button type="button" data-testid="protocol-interaction-disconnect" data-action="disconnect">断开</button>
                    <button type="button" data-testid="protocol-interaction-pause" data-action="pause">暂停</button>
                    <button type="button" data-testid="protocol-interaction-resume" data-action="resume">恢复</button>
                </div>
            </div>
            <div class="interaction-drawer-toolbar" data-testid="protocol-interaction-toolbar">
                <div class="interaction-filter-group" role="group" aria-label="交互筛选"></div>
                <div class="interaction-buffer-info" data-role="buffer"></div>
            </div>
            <div class="interaction-drawer-body">
                <aside class="interaction-record-list" aria-label="交互记录列表">
                    <div class="interaction-list-actions" role="group" aria-label="交互记录操作">
                        <div class="interaction-consumption-speed" role="group" aria-label="队列消费速度">
                            <button type="button" data-action="set-merge-speed" data-speed="slow" title="慢速消费缓冲队列">慢</button>
                            <button type="button" data-action="set-merge-speed" data-speed="medium" title="中速消费缓冲队列">中</button>
                            <button type="button" data-action="set-merge-speed" data-speed="fast" title="快速消费缓冲队列">快</button>
                        </div>
                        <button type="button" class="interaction-list-action" data-action="toggle-sort" title="切换交互记录时间排序" aria-label="切换交互记录时间排序">${SORT_DESC_ICON}</button>
                        <button type="button" class="interaction-list-action" data-action="clear-records" title="清空当前实时记录" aria-label="清空当前实时记录">${CLEAR_ICON}</button>
                        <div class="interaction-clear-confirm" role="dialog" aria-label="确认清空实时记录" hidden>
                            <p>确定清空当前实时记录？</p>
                            <div class="interaction-clear-confirm-actions">
                                <button type="button" data-action="cancel-clear">取消</button>
                                <button type="button" data-action="confirm-clear">清空</button>
                            </div>
                        </div>
                    </div>
                    <div class="interaction-record-items" data-testid="protocol-interaction-record-list" tabindex="0"></div>
                </aside>
            <main class="interaction-record-detail detail-pane" data-testid="protocol-interaction-detail">
                <button type="button" class="interaction-mobile-back" data-action="back-to-list">返回列表</button>
                    <div class="interaction-record-detail-inner" data-testid="protocol-interaction-detail-content"></div>
                </main>
            </div>
        `;
        document.body.appendChild(backdrop);
        document.body.appendChild(panel);
        const imagePreview = document.createElement('div');
        imagePreview.className = 'interaction-image-preview';
        imagePreview.hidden = true;
        imagePreview.setAttribute('role', 'dialog');
        imagePreview.setAttribute('aria-modal', 'true');
        imagePreview.setAttribute('aria-label', '图片预览');
        imagePreview.innerHTML = `
            <button type="button" class="interaction-image-preview-close" data-action="close-image-preview" title="关闭图片预览" aria-label="关闭图片预览">×</button>
            <img class="interaction-image-preview-image" data-role="image-preview" alt="">
        `;
        document.body.appendChild(imagePreview);
        const copyToast = document.createElement('div');
        copyToast.className = 'interaction-copy-toast';
        copyToast.hidden = true;
        copyToast.setAttribute('role', 'status');
        copyToast.setAttribute('aria-live', 'polite');
        document.body.appendChild(copyToast);
        drawer = {
            backdrop,
            panel,
            imagePreview,
            imagePreviewLastFocus: null,
            copyToast,
            copyToastTimer: null,
            lastFocus: null,
            closeTimer: null,
            openFrame: null,
            filter: 'all',
            tab: 'overview',
        };

        Object.entries(FILTERS).forEach(([key, label]) => {
            const button = makeButton(label, 'interaction-filter-btn');
            button.dataset.filter = key;
            if (key === 'all') button.classList.add('is-active');
            panel.querySelector('.interaction-filter-group').appendChild(button);
        });

        panel.addEventListener('click', handleDrawerClick);
        backdrop.addEventListener('click', close);
        imagePreview.addEventListener('click', event => {
            if (event.target === imagePreview || event.target.dataset.action === 'close-image-preview') {
                closeImagePreview();
            }
        });
        global.addEventListener('keydown', event => {
            if (event.key !== 'Escape' || !drawer) return;
            if (!drawer.imagePreview.hidden) {
                closeImagePreview();
                return;
            }
            if (!drawer.panel.hidden) close();
        });
        return drawer;
    }

    function openImagePreview(url, alt, trigger) {
        if (!drawer || !url) return;
        const image = drawer.imagePreview.querySelector('[data-role="image-preview"]');
        const closeButton = drawer.imagePreview.querySelector('[data-action="close-image-preview"]');
        if (!image) return;
        drawer.imagePreviewLastFocus = trigger || global.document.activeElement;
        image.src = url;
        image.alt = alt || '图片预览';
        drawer.imagePreview.hidden = false;
        if (closeButton) closeButton.focus();
    }

    function closeImagePreview() {
        if (!drawer || !drawer.imagePreview || drawer.imagePreview.hidden) return;
        drawer.imagePreview.hidden = true;
        const image = drawer.imagePreview.querySelector('[data-role="image-preview"]');
        if (image) image.removeAttribute('src');
        if (drawer.imagePreviewLastFocus && typeof drawer.imagePreviewLastFocus.focus === 'function') {
            drawer.imagePreviewLastFocus.focus({ preventScroll: true });
        }
        drawer.imagePreviewLastFocus = null;
    }

    function downloadImage(url, fileName) {
        if (!url || !global.document) return;
        const link = global.document.createElement('a');
        link.href = url;
        link.download = fileName || 'interaction-image';
        link.hidden = true;
        global.document.body.appendChild(link);
        link.click();
        link.remove();
    }

    function setClearConfirmationVisible(visible) {
        if (!drawer) return;
        const confirmation = drawer.panel.querySelector('.interaction-clear-confirm');
        if (!confirmation) return;
        confirmation.hidden = !visible;
        if (visible) {
            const confirmButton = confirmation.querySelector('[data-action="confirm-clear"]');
            if (confirmButton) confirmButton.focus();
        }
    }

    function clearActiveRecords() {
        if (!activeEntry) return;
        const viewState = viewStateFor(activeEntry.key);
        viewState.readRecordKeys.clear();
        clearTransientAnimations(viewState);
        if (activeClient() && typeof activeClient().clearRecords === 'function') activeClient().clearRecords();
        persistDrawerUiState(true);
    }

    function focusRecordItem(recordKey) {
        if (!drawer || !recordKey) return;
        const item = drawer.panel.querySelector(`[data-record-key="${recordKey}"]`);
        if (!item || typeof item.focus !== 'function') return;
        try {
            item.focus({ preventScroll: true });
        } catch (error) {
            item.focus();
        }
    }

    function syncBufferLimits(options = {}) {
        if (!drawer || !activeEntry || !activeClient()
            || typeof activeClient().setBufferLimits !== 'function') return;
        const client = activeClient();
        const pauseFlow = options.pauseFlow === true;
        const viewState = activeEntry ? viewStateFor(activeEntry.key) : null;
        if (pauseFlow) {
            drawer.panel.classList.add('is-reflowing');
            pauseRecordTransitions(viewState);
            if (typeof client.pauseMerge === 'function') client.pauseMerge();
        }
        const isFullscreen = drawer.panel.classList.contains('is-fullscreen');
        const limits = {
            maxVisibleRecords: isFullscreen ? 20 : 10,
            maxPendingRecords: 20,
        };
        if (isFullscreen) {
            client.setBufferLimits(limits);
        } else {
            const state = client.getState();
            const visibleRecords = state.visibleRecords.slice();
            if (visibleRecords.length) {
                client.setBufferLimits(limits, {
                    reflowVisibleRecords: visibleRecords,
                });
            } else {
                client.setBufferLimits(limits);
            }
        }
        if (pauseFlow) {
            const resume = () => {
                if (!drawer) return;
                drawer.panel.classList.remove('is-reflowing');
                resumeRecordTransitions(viewState);
                if (typeof client.resumeMerge === 'function') client.resumeMerge();
            };
            if (typeof global.requestAnimationFrame === 'function') {
                global.requestAnimationFrame(resume);
            } else {
                global.setTimeout(resume, 0);
            }
        }
    }

    function activeClient() {
        return activeEntry && activeEntry.client;
    }

    function persistDrawerUiState(immediate = false) {
        if (!activeEntry || !activeClient() || typeof activeClient().updatePersistenceUiState !== 'function') return;
        const viewState = viewStateFor(activeEntry.key);
        activeClient().updatePersistenceUiState({
            filter: drawer ? drawer.filter : 'all',
            detailTab: drawer ? drawer.tab : 'overview',
            fullscreen: Boolean(drawer && drawer.panel.classList.contains('is-fullscreen')),
            mobileDetail: Boolean(viewState.mobileDetail),
            drawerOpen: Boolean(drawer && !drawer.panel.hidden && !drawer.closeTimer),
            followLive: Boolean(viewState.followLive),
            readRecordKeys: Array.from(viewState.readRecordKeys),
        }, immediate);
    }

    function isEligible(entry) {
        return entry && id(entry.projectId) > 0 && id(entry.protocolId) > 0
            && Number(entry.projectRuntimeState) === 1
            && Number(entry.configState) === 1
            && entry.deleted !== true;
    }

    function eligibilityReason(entry) {
        if (!entry) return '请先启动测试服务';
        if (entry.deleted === true) return '协议项已删除';
        if (Number(entry.projectRuntimeState) !== 1) return '请先启动测试服务';
        if (Number(entry.configState) === 2) return '协议项待重配置';
        if (Number(entry.configState) !== 1) return '请先上线协议项';
        return '';
    }

    function clientFor(entry) {
        const key = `${entry.projectId}:${entry.protocolId}`;
        let client = clients.get(key);
        if (client) return client;
        client = Live.createClient({
            projectId: entry.projectId,
            protocolId: entry.protocolId,
            protocolType: entry.protocolType,
            maxVisibleRecords: 10,
            maxPendingRecords: 20,
        });
        const rerender = (restoreSelectedFocus = false, preserveList = false) => {
            if (activeEntry && activeEntry.key === key) render(restoreSelectedFocus, preserveList);
        };
        ['state', 'warning', 'liveReady', 'recordReceived', 'recordVisible', 'recordEvicted', 'recordsChanged', 'recordsReset', 'selectionChanged', 'attachmentAvailable', 'commandAck', 'closed'].forEach(type => client.on(type, payload => {
            let transitionAccepted = true;
            if (type === 'recordVisible' && payload && payload.record && activeEntry && activeEntry.key === key) {
                transitionAccepted = addEnteringRecord(viewStateFor(key), payload.record._key);
            }
            if (type === 'recordEvicted' && payload && payload.reason === 'visible_capacity'
                && payload.record && activeEntry && activeEntry.key === key) {
                transitionAccepted = addExitingRecord(viewStateFor(key), payload.record);
            }
            if (!activeEntry || activeEntry.key !== key) return;
            if (type === 'recordVisible' || type === 'recordsReset'
                || type === 'selectionChanged') {
                if (type === 'recordVisible') {
                    const viewState = viewStateFor(key);
                    rerender(true, !transitionAccepted || viewState.exitingRecords.size > 0);
                } else {
                    rerender(true);
                }
                return;
            }
            if (type === 'recordEvicted') {
                if (payload && payload.reason === 'visible_capacity' && transitionAccepted) rerender(true);
                else updateHeader();
                return;
            }
            if (type === 'recordsChanged' && payload && payload.reason === 'buffer_limits') {
                rerender(true, true);
                return;
            }
            if (type === 'attachmentAvailable') {
                updateHeader();
                renderDetail();
                return;
            }
            updateHeader();
        }));
        clients.set(key, client);
        return client;
    }

    function updateHeader() {
        if (!drawer || !activeEntry) return;
        const client = activeEntry.client;
        const state = client.getState();
        setText(drawer.panel.querySelector('#protocol-interaction-drawer-title'), activeEntry.name || '协议项交互详情');
        const protocolType = protocolTypeLabel(activeEntry.protocolType);
        setText(drawer.panel.querySelector('[data-role="protocol-type"]'), protocolType);
        const protocolTypeBadge = drawer.panel.querySelector('[data-role="protocol-type"]');
        if (protocolTypeBadge) {
            protocolTypeBadge.className = `interaction-type-badge interaction-type-${String(activeEntry.protocolType || '').toLowerCase()}`;
        }
        setText(drawer.panel.querySelector('[data-role="protocol-meta"]'), `protocol_id=${activeEntry.protocolId} · runtime project_id=${activeEntry.projectId}`);
        setText(drawer.panel.querySelector('[data-role="status"]'), statusLabel(state.connectionState));
        const dot = drawer.panel.querySelector('.interaction-status-dot');
        if (dot) dot.dataset.state = state.connectionState;
        const warning = state.lastError || (state.warnings.length ? state.warnings[state.warnings.length - 1].message : '');
        setText(drawer.panel.querySelector('[data-role="warning"]'), warning ? ` · ${warning}` : '');
        setText(drawer.panel.querySelector('[data-role="history"]'), `历史：已显示 ${state.visibleRecords.length} 条`);
        setText(drawer.panel.querySelector('[data-role="buffer"]'), `待显示 ${state.pendingRecords.length} 条 · 已显示 ${state.visibleRecords.length} 条`);
        const viewState = viewStateFor(activeEntry.key);
        const mergeSpeed = state.mergeSpeed || 'medium';
        drawer.panel.style.setProperty(
            '--interaction-record-transition-duration',
            `${recordTransitionMs(mergeSpeed)}ms`,
        );
        drawer.panel.querySelectorAll('[data-action="set-merge-speed"]').forEach(button => {
            const active = button.dataset.speed === mergeSpeed;
            button.classList.toggle('is-active', active);
            button.setAttribute('aria-pressed', active ? 'true' : 'false');
        });
        const sortButton = drawer.panel.querySelector('[data-action="toggle-sort"]');
        if (sortButton) {
            const currentDirection = sortLabel(viewState.sortDirection);
            const nextDirection = sortLabel(viewState.sortDirection === 'asc' ? 'desc' : 'asc');
            const sortHint = `当前${currentDirection}，点击切换为${nextDirection}`;
            sortButton.innerHTML = viewState.sortDirection === 'asc' ? SORT_ASC_ICON : SORT_DESC_ICON;
            sortButton.title = sortHint;
            sortButton.setAttribute('aria-label', sortHint);
            sortButton.dataset.sortDirection = viewState.sortDirection;
        }
        const connectButton = drawer.panel.querySelector('[data-action="connect"]');
        const disconnectButton = drawer.panel.querySelector('[data-action="disconnect"]');
        const pauseButton = drawer.panel.querySelector('[data-action="pause"]');
        const resumeButton = drawer.panel.querySelector('[data-action="resume"]');
        if (connectButton) connectButton.disabled = state.desiredConnected || [Live.STATES.CONNECTING, Live.STATES.CATCHING_UP, Live.STATES.ACTIVE, Live.STATES.PAUSED, Live.STATES.PAUSING, Live.STATES.RESUMING].includes(state.connectionState);
        if (disconnectButton) disconnectButton.disabled = !state.desiredConnected && !state.socket;
        if (pauseButton) pauseButton.disabled = state.connectionState !== Live.STATES.ACTIVE;
        if (resumeButton) resumeButton.disabled = state.connectionState !== Live.STATES.PAUSED;
    }

    function updateRecordItem(item, record, state, viewState, visibleIndex, isExiting, allowAnimation) {
        item.dataset.recordKey = record._key;
        item.dataset.testid = 'protocol-interaction-record-item';
        const isSelected = record._key === state.selectedRecordKey;
        const isUnread = !viewState.readRecordKeys.has(record._key);
        const isEntering = allowAnimation && viewState.enteringRecordKeys.has(record._key);
        const wasEntering = item.classList.contains('is-entering-front')
            || item.classList.contains('is-entering-end');
        applyRecordGridPlacement(item, visibleIndex, isExiting, viewState.sortDirection);
        item.classList.remove(
            'is-selected', 'is-unread',
            'is-exiting', 'is-exiting-asc', 'is-exiting-desc',
        );
        if (!wasEntering) item.classList.remove('is-entering-front', 'is-entering-end');
        if (!isExiting && isSelected) item.classList.add('is-selected');
        if (!isExiting && isUnread) item.classList.add('is-unread');
        if (isEntering && !wasEntering) item.classList.add(viewState.sortDirection === 'asc' ? 'is-entering-front' : 'is-entering-end');
        if (isExiting) {
            item.classList.add('is-exiting');
            item.classList.add(viewState.sortDirection === 'asc' ? 'is-exiting-asc' : 'is-exiting-desc');
        }
        const showSeq = drawer.filter === 'protocol' || drawer.filter === 'notice';
        const deliveryMarkup = isAdminViewer() ? '<span data-role="delivery"></span>' : '';
        const noticeMarkup = record.scope === 'project' ? '<span data-role="scope" class="interaction-badge badge-notice"></span>' : '';
        item.innerHTML = `<div class="interaction-record-item-top"><strong data-role="title"></strong>${noticeMarkup}<span data-role="result"></span></div><div class="interaction-record-item-sub">${deliveryMarkup}<span data-role="seq"></span><span data-role="time"></span><span data-role="body"></span></div>`;
        setText(item.querySelector('[data-role="title"]'), recordTitle(record));
        setText(item.querySelector('[data-role="result"]'), resultLabel(record.result));
        const delivery = item.querySelector('[data-role="delivery"]');
        const scope = item.querySelector('[data-role="scope"]');
        if (delivery) setText(delivery, record._delivery === 'catch_up' ? '补发' : '实时');
        if (scope) setText(scope, 'Notice');
        setText(item.querySelector('[data-role="seq"]'), `seq=${record.seq}`);
        setText(item.querySelector('[data-role="time"]'), formatTime(record.time_ms));
        setText(item.querySelector('[data-role="body"]'), bodySummary(record));
        if (delivery) delivery.className = `interaction-badge ${record._delivery === 'catch_up' ? 'badge-catchup' : 'badge-realtime'}`;
        const seq = item.querySelector('[data-role="seq"]');
        seq.hidden = !showSeq;
        item.querySelector('[data-role="result"]').className = `interaction-result-badge result-${resultTone(record.result)}`;
    }

    function renderList(restoreSelectedFocus = false, preserveExisting = false) {
        if (!drawer || !activeEntry) return;
        const list = drawer.panel.querySelector('.interaction-record-list');
        const items = list.querySelector('.interaction-record-items');
        const client = activeEntry.client;
        const state = client.getState();
        const viewState = viewStateFor(activeEntry.key);
        const activeRecordKeys = new Set(state.visibleRecords.concat(state.pendingRecords)
            .concat(Array.from(viewState.exitingRecords.values()))
            .map(record => record && record._key)
            .filter(Boolean));
        viewState.readRecordKeys.forEach(key => {
            if (!activeRecordKeys.has(key)) viewState.readRecordKeys.delete(key);
        });
        const focusedRecordKey = global.document && global.document.activeElement
            && global.document.activeElement.dataset
            && global.document.activeElement.dataset.recordKey;
        const existingItems = preserveExisting
            ? new Map(Array.from(items.querySelectorAll('[data-record-key]')).map(item => [item.dataset.recordKey, item]))
            : new Map();
        if (preserveExisting) {
            const emptyState = items.querySelector('.interaction-empty-state');
            if (emptyState) emptyState.remove();
        } else {
            items.innerHTML = '';
        }
        const records = state.visibleRecords
            .concat(Array.from(viewState.exitingRecords.values()))
            .filter(record => matchesFilter(record, drawer.filter))
            .sort((left, right) => compareRecordTime(left, right, viewState.sortDirection));
        if (!records.length) {
            const empty = document.createElement('p');
            empty.className = 'interaction-empty-state';
            empty.textContent = '暂无符合条件的交互记录';
            items.appendChild(empty);
            return;
        }
        let visibleIndex = 0;
        const renderedKeys = new Set();
        records.forEach(record => {
            const item = existingItems.get(record._key) || makeButton('', 'interaction-record-item');
            const isExiting = viewState.exitingRecords.has(record._key);
            updateRecordItem(item, record, state, viewState, visibleIndex, isExiting, true);
            if (!isExiting) visibleIndex += 1;
            renderedKeys.add(record._key);
            // preserveExisting 只复用节点，不保留旧 DOM 顺序；否则新记录虽位于 grid 首行，键盘顺序和动画节点仍在旧位置。
            items.appendChild(item);
        });
        if (preserveExisting) {
            existingItems.forEach((item, key) => {
                if (!renderedKeys.has(key)) item.remove();
            });
        }
        if (focusedRecordKey && focusedRecordKey === state.selectedRecordKey) {
            const focusedItem = Array.from(items.querySelectorAll('[data-record-key]'))
                .find(item => item.dataset.recordKey === state.selectedRecordKey);
            if (focusedItem && typeof focusedItem.focus === 'function') {
                try {
                    focusedItem.focus({ preventScroll: true });
                } catch (error) {
                    focusedItem.focus();
                }
            }
        }
    }

    function applyRecordGridPlacement(item, index, isExiting, sortDirection) {
        const isFullscreen = drawer.panel.classList.contains('is-fullscreen');
        const isCompact = Number(global.innerWidth) > 0 && Number(global.innerWidth) <= 900;
        const useTwoColumns = isFullscreen && !isCompact;
        const lastRow = 10;

        if (isExiting) {
            item.style.gridColumn = useTwoColumns
                ? (sortDirection === 'asc' ? '2' : '1')
                : '1';
            item.style.gridRow = sortDirection === 'asc' ? String(lastRow) : '1';
            return;
        }

        if (useTwoColumns) {
            item.style.gridColumn = String(Math.floor(index / lastRow) + 1);
            item.style.gridRow = String((index % lastRow) + 1);
            return;
        }
        item.style.gridColumn = '1';
        item.style.gridRow = String(index + 1);
    }

    function appendKeyValue(parent, label, value) {
        const row = document.createElement('div');
        row.className = 'interaction-kv-row';
        const key = document.createElement('dt');
        const val = document.createElement('dd');
        key.textContent = label;
        val.textContent = escapeText(value);
        row.appendChild(key);
        row.appendChild(val);
        parent.appendChild(row);
    }

    function createCopyButton(text, enabled, label) {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'interaction-code-copy';
        button.dataset.action = 'copy-code';
        button.dataset.copyText = String(text || '');
        button.innerHTML = COPY_ICON;
        button.disabled = !enabled;
        button.title = enabled ? `复制${label || '内容'}` : `${label || '内容'}不可复制`;
        button.setAttribute('aria-label', button.title);
        return button;
    }

    function hideCopyToast() {
        if (!drawer || !drawer.copyToast) return;
        if (drawer.copyToastTimer) global.clearTimeout(drawer.copyToastTimer);
        drawer.copyToastTimer = null;
        drawer.copyToast.classList.remove('is-visible');
        drawer.copyToast.hidden = true;
    }

    function showCopyToast() {
        if (!drawer || !drawer.copyToast) return;
        if (drawer.copyToastTimer) global.clearTimeout(drawer.copyToastTimer);
        drawer.copyToast.textContent = '复制成功';
        drawer.copyToast.hidden = false;
        drawer.copyToast.classList.add('is-visible');
        drawer.copyToastTimer = global.setTimeout(() => {
            hideCopyToast();
        }, 1400);
    }

    async function copyCode(text, button) {
        if (!button || button.disabled || !text) return;
        let copied = false;
        try {
            if (global.navigator && global.navigator.clipboard && typeof global.navigator.clipboard.writeText === 'function') {
                await global.navigator.clipboard.writeText(text);
                copied = true;
            } else {
                const input = document.createElement('textarea');
                input.value = text;
                input.setAttribute('readonly', '');
                input.style.position = 'fixed';
                input.style.opacity = '0';
                document.body.appendChild(input);
                input.select();
                copied = typeof document.execCommand === 'function' && document.execCommand('copy');
                input.remove();
            }
        } catch (error) {
            copied = false;
        }
        if (copied) {
            button.classList.add('is-copied');
            button.title = '已复制';
            showCopyToast();
            global.setTimeout(() => {
                if (!button.isConnected) return;
                button.classList.remove('is-copied');
                button.title = '复制内容';
            }, 1200);
        }
    }

    function appendCode(parent, label, value, copyEnabled = Boolean(value)) {
        const heading = document.createElement('div');
        heading.className = 'interaction-code-heading';
        const headingLabel = document.createElement('h4');
        headingLabel.textContent = label;
        heading.appendChild(headingLabel);
        heading.appendChild(createCopyButton(value, copyEnabled, label));
        const pre = document.createElement('pre');
        pre.textContent = escapeText(value || '');
        parent.appendChild(heading);
        parent.appendChild(pre);
    }

    function appendBodyHeading(parent, value) {
        const heading = document.createElement('div');
        heading.className = 'interaction-code-heading';
        const label = document.createElement('h4');
        label.textContent = 'Body';
        heading.appendChild(label);
        const kind = String(value && value.kind || '').toLowerCase();
        const readable = ['text', 'json', 'xml'].includes(kind) && Boolean(value && value.text);
        heading.appendChild(createCopyButton(value && value.text, readable, 'Body'));
        parent.appendChild(heading);
        return heading;
    }

    function asBytes(value) {
        if (value instanceof Uint8Array) return value;
        if (value instanceof ArrayBuffer) return new Uint8Array(value);
        if (ArrayBuffer.isView(value)) return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
        return null;
    }

    function hasBytesAt(bytes, offset, values) {
        return bytes && bytes.length >= offset + values.length
            && values.every((value, index) => bytes[offset + index] === value);
    }

    function isValidRasterImagePayload(ref, payload) {
        if (!ref || String(ref.kind || '').toLowerCase() !== 'image') return false;
        const bytes = asBytes(payload && payload.bytes);
        if (!bytes || bytes.length < 4) return false;
        const isPng = hasBytesAt(bytes, 0, [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
        const isJpeg = hasBytesAt(bytes, 0, [0xff, 0xd8, 0xff]);
        const isGif = hasBytesAt(bytes, 0, [0x47, 0x49, 0x46, 0x38])
            && (bytes[4] === 0x37 || bytes[4] === 0x39)
            && bytes[5] === 0x61;
        const isWebp = hasBytesAt(bytes, 0, [0x52, 0x49, 0x46, 0x46])
            && hasBytesAt(bytes, 8, [0x57, 0x45, 0x42, 0x50]);
        const isBmp = hasBytesAt(bytes, 0, [0x42, 0x4d]);
        const isTiff = hasBytesAt(bytes, 0, [0x49, 0x49, 0x2a, 0x00])
            || hasBytesAt(bytes, 0, [0x4d, 0x4d, 0x00, 0x2a]);
        const isAvif = hasBytesAt(bytes, 4, [0x66, 0x74, 0x79, 0x70])
            && (hasBytesAt(bytes, 8, [0x61, 0x76, 0x69, 0x66])
                || hasBytesAt(bytes, 8, [0x61, 0x76, 0x69, 0x73]));
        return isPng || isJpeg || isGif || isWebp || isBmp || isTiff || isAvif;
    }

    function isMatchedImageBody(body) {
        const value = body || {};
        const actual = String(value.kind || '').toLowerCase();
        const expected = String(value.expect_kind || '').toLowerCase();
        return actual === 'image' && expected === 'image';
    }

    function findBodyImageAttachment(body) {
        const refs = body && Array.isArray(body.attachments) ? body.attachments : [];
        return refs.find(ref => String(ref.kind || '').toLowerCase() === 'image') || null;
    }

    function appendInlineImage(parent, record, sideName, body) {
        if (sideName !== 'request' || !isMatchedImageBody(body) || !activeEntry
            || !activeEntry.client || typeof activeEntry.client.getAttachment !== 'function') return false;
        const ref = findBodyImageAttachment(body);
        const payload = ref && activeEntry.client.getAttachment(record, ref);
        if (!ref || !payload || !payload.objectUrl) {
            const unavailable = document.createElement('div');
            unavailable.className = 'interaction-inline-image-state';
            unavailable.textContent = ref ? '图片附件正在加载或暂不可用' : '未找到图片附件';
            parent.appendChild(unavailable);
            return { rendered: true, downloadButton: null };
        }
        const tools = document.createElement('div');
        tools.className = 'interaction-inline-image-tools';
        const previewButton = document.createElement('button');
        previewButton.type = 'button';
        previewButton.className = 'interaction-inline-image-preview';
        previewButton.dataset.action = 'preview-image';
        previewButton.dataset.imageUrl = payload.objectUrl;
        previewButton.dataset.imageAlt = `${sideName} Body 图片预览`;
        previewButton.title = '点击放大图片';
        previewButton.setAttribute('aria-label', '点击放大图片');
        const image = document.createElement('img');
        image.className = 'interaction-inline-image';
        image.src = payload.objectUrl;
        image.alt = `${sideName} Body 图片预览`;
        previewButton.appendChild(image);
        tools.appendChild(previewButton);
        parent.appendChild(tools);
        let downloadButton = null;
        if (isValidRasterImagePayload(ref, payload)) {
            downloadButton = document.createElement('button');
            downloadButton.type = 'button';
            downloadButton.className = 'interaction-inline-image-download';
            downloadButton.dataset.action = 'download-image';
            downloadButton.dataset.imageUrl = payload.objectUrl;
            downloadButton.dataset.downloadName = ref.attachment_id || 'request-body-image';
            downloadButton.title = '保存图片';
            downloadButton.setAttribute('aria-label', '保存图片');
            downloadButton.innerHTML = DOWNLOAD_ICON;
        }
        return { rendered: true, downloadButton };
    }

    function attachmentKind(ref) {
        return String(ref && ref.kind || 'binary').toLowerCase();
    }

    function isMultiformPart(ref) {
        return String(ref && ref.flag || '').toLowerCase().includes('.multiform.');
    }

    function multiformPartLabel(ref) {
        const flag = String(ref && ref.flag || '');
        const match = flag.match(/\.multiform\.(\d+)$/i);
        if (match) return `Part ${match[1]}`;
        return flag || String(ref && ref.attachment_id || '未命名 Part');
    }

    function createMultiformPartToggle(partLabel) {
        const toggle = makeButton('−', 'interaction-multiform-part-toggle', `收起 ${partLabel}`);
        toggle.dataset.action = 'toggle-multiform-part';
        toggle.dataset.partLabel = partLabel;
        toggle.setAttribute('aria-expanded', 'true');
        return toggle;
    }

    function appendAttachmentImage(parent, record, sideName, ref, payload, inlinePreview) {
        const image = document.createElement('img');
        image.className = 'interaction-attachment-preview';
        image.src = payload.objectUrl;
        image.alt = `${sideName} ${ref.attachment_id || '图片附件'}`;

        if (!inlinePreview) {
            parent.appendChild(image);
            return;
        }

        const tools = document.createElement('div');
        tools.className = 'interaction-inline-image-tools';
        const previewButton = document.createElement('button');
        previewButton.type = 'button';
        previewButton.className = 'interaction-inline-image-preview';
        previewButton.dataset.action = 'preview-image';
        previewButton.dataset.imageUrl = payload.objectUrl;
        previewButton.dataset.imageAlt = image.alt;
        previewButton.title = '点击放大图片';
        previewButton.setAttribute('aria-label', '点击放大图片');
        previewButton.appendChild(image);
        tools.appendChild(previewButton);

        if (isValidRasterImagePayload(ref, payload)) {
            const downloadButton = document.createElement('button');
            downloadButton.type = 'button';
            downloadButton.className = 'interaction-inline-image-download';
            downloadButton.dataset.action = 'download-image';
            downloadButton.dataset.imageUrl = payload.objectUrl;
            downloadButton.dataset.downloadName = ref.attachment_id || 'interaction-image';
            downloadButton.title = '保存图片';
            downloadButton.setAttribute('aria-label', '保存图片');
            downloadButton.innerHTML = DOWNLOAD_ICON;
            tools.appendChild(downloadButton);
        }
        parent.appendChild(tools);
    }

    function appendAttachmentContent(parent, record, sideName, ref, options = {}) {
        const kind = attachmentKind(ref);
        const textKinds = ['text', 'json', 'xml'];
        if (textKinds.includes(kind)) {
            appendCode(parent, options.textLabel || '内容', ref.text || '', Boolean(ref.text));
            return;
        }

        const payload = activeEntry && activeEntry.client
            && typeof activeEntry.client.getAttachment === 'function'
            ? activeEntry.client.getAttachment(record, ref)
            : null;
        if (payload && payload.objectUrl) {
            if (kind === 'image') {
                appendAttachmentImage(parent, record, sideName, ref, payload, Boolean(options.inlinePreview));
            } else if (kind === 'audio' || kind === 'video') {
                const media = document.createElement(kind);
                media.className = 'interaction-attachment-preview';
                media.src = payload.objectUrl;
                media.controls = true;
                media.preload = 'metadata';
                parent.appendChild(media);
            } else if (kind === 'pdf') {
                const preview = document.createElement('iframe');
                preview.className = 'interaction-attachment-preview';
                preview.src = payload.objectUrl;
                preview.title = `${sideName} ${ref.attachment_id || 'PDF 附件'}`;
                parent.appendChild(preview);
                const link = document.createElement('a');
                link.href = payload.objectUrl;
                link.target = '_blank';
                link.rel = 'noopener';
                link.textContent = '在新窗口打开 PDF';
                parent.appendChild(link);
            } else {
                const link = document.createElement('a');
                link.href = payload.objectUrl;
                link.download = ref.attachment_id || 'interaction-attachment';
                link.textContent = '打开或下载';
                parent.appendChild(link);
            }
            return;
        }

        const unavailable = document.createElement('span');
        unavailable.className = 'interaction-attachment-unavailable';
        unavailable.textContent = ref.binary_available === false || ref.truncated ? '附件不可用' : '等待附件数据';
        parent.appendChild(unavailable);
    }

    function renderMultiformParts(parent, body, record, sideName) {
        const refs = body && Array.isArray(body.attachments) ? body.attachments : [];
        const section = document.createElement('section');
        section.className = 'interaction-multiform-parts';
        const heading = document.createElement('h4');
        heading.textContent = 'Multipart parts';
        section.appendChild(heading);

        if (!refs.length) {
            const empty = document.createElement('p');
            empty.className = 'interaction-empty-state';
            empty.textContent = '当前 Multiform 未解析出字段';
            section.appendChild(empty);
            parent.appendChild(section);
            return;
        }

        refs.forEach(ref => {
            const part = document.createElement('article');
            part.className = 'interaction-multiform-part';
            part.dataset.role = 'multiform-part';
            part.dataset.attachmentId = ref.attachment_id || '';

            const partHeader = document.createElement('div');
            partHeader.className = 'interaction-multiform-part-header';
            const titleGroup = document.createElement('div');
            titleGroup.className = 'interaction-multiform-part-title-group';
            const partLabel = multiformPartLabel(ref);
            titleGroup.appendChild(createMultiformPartToggle(partLabel));
            const title = document.createElement('strong');
            title.textContent = partLabel;
            titleGroup.appendChild(title);
            partHeader.appendChild(titleGroup);
            const flag = document.createElement('span');
            flag.className = 'interaction-multiform-part-flag';
            flag.textContent = ref.flag || ref.attachment_id || '';
            partHeader.appendChild(flag);
            part.appendChild(partHeader);

            const partContent = document.createElement('div');
            partContent.className = 'interaction-multiform-part-content';
            partContent.dataset.role = 'multiform-part-content';
            const info = document.createElement('p');
            info.className = 'interaction-multiform-part-info';
            info.textContent = `${ref.kind || 'binary'} · ${ref.captured_size || 0}/${ref.size || ref.captured_size || 0} bytes · SHA1 ${ref.sha1 || '无'}`;
            partContent.appendChild(info);
            appendAttachmentContent(partContent, record, sideName, ref, {
                inlinePreview: true,
                textLabel: `${partLabel} 内容`,
            });
            part.appendChild(partContent);
            section.appendChild(part);
        });
        parent.appendChild(section);
    }

    function renderBody(parent, body, record, sideName) {
        const value = body || {};

        const bodyHeading = appendBodyHeading(parent, value);
        const imageResult = appendInlineImage(parent, record, sideName, value);
        if (imageResult) {
            if (imageResult.downloadButton) bodyHeading.appendChild(imageResult.downloadButton);
            return;
        }
        if (String(value.kind || '').toLowerCase() === 'multiform') {
            renderMultiformParts(parent, value, record, sideName);
            return;
        }
        const pre = document.createElement('pre');
        pre.textContent = escapeText(value.text || '');
        parent.appendChild(pre);
    }

    function renderSide(parent, side, record, sideName) {
        const value = side || {};

        const body_value = value.body || {};
        const meta = document.createElement('dl');
        meta.className = 'interaction-kv interaction-body-kv';
        appendKeyValue(meta, '实际类型', body_value.kind || 'unknown');
        appendKeyValue(meta, '期望类型', body_value.expect_kind || 'unknown');
        appendKeyValue(meta, '大小', `${body_value.captured_size || 0}/${body_value.size || 0}`);
        appendKeyValue(meta, 'SHA1', body_value.sha1 || '');
        if (body_value.truncated) appendKeyValue(meta, '状态', '内容已截断');
        if (body_value.error_message) appendKeyValue(meta, '解析错误', body_value.error_message);
        parent.appendChild(meta);

        appendCode(parent, 'Header', value.head_text || '', Boolean(value.head_text));

        renderBody(parent, value.body, record, sideName);
    }

    function createDetailSidePanel(title, protocolType) {
        const block = document.createElement('section');
        block.className = 'interaction-detail-block side-panel';
        const heading = document.createElement('h3');
        heading.className = 'interaction-detail-block-heading side-head';
        const titleElement = document.createElement('span');
        titleElement.className = 'side-title';
        titleElement.textContent = title;
        heading.appendChild(titleElement);
        const badge = document.createElement('span');
        badge.className = 'interaction-type-badge';
        badge.textContent = protocolTypeLabel(protocolType);
        heading.appendChild(badge);
        const content = document.createElement('div');
        content.className = 'interaction-detail-block-content side-content';
        block.appendChild(heading);
        block.appendChild(content);
        return block;
    }

    function renderOverview(parent, record) {
        const summary = document.createElement('dl');
        summary.className = 'interaction-kv summary-panel';
        appendKeyValue(summary, '来源', record.scope === 'project' ? '项目 Notice' : '协议项');
        appendKeyValue(summary, '结果', resultLabel(record.result));
        appendKeyValue(summary, '时间', formatTime(record.time_ms));
        appendKeyValue(summary, '对端地址', record.peer_addr || '');
        if (isAdminViewer()) {
            appendKeyValue(summary, '投递', record._delivery === 'catch_up' ? '补发' : '实时');
            appendKeyValue(summary, '缓存游标', `${record.scope}:${record.cache_instance_id}:${record.seq}`);
            if (record.error_message) appendKeyValue(summary, '错误信息', record.error_message);
        }
        parent.appendChild(summary);
        const columns = document.createElement('div');
        columns.className = 'interaction-side-columns detail-grid';
        const request = createDetailSidePanel('收到 request', record.protocol_type);
        const response = createDetailSidePanel('发出 response', record.protocol_type);
        renderSide(request.querySelector('.interaction-detail-block-content'), record.request, record, 'request');
        renderSide(response.querySelector('.interaction-detail-block-content'), record.response, record, 'response');
        columns.appendChild(request);
        columns.appendChild(response);
        parent.appendChild(columns);
    }

    function renderRaw(parent, record) {
        const columns = document.createElement('div');
        columns.className = 'interaction-side-columns detail-grid';
        ['request', 'response'].forEach(side => {
            const value = record[side] || {};
            const block = createDetailSidePanel(
                side === 'request' ? 'Request Raw/Hex' : 'Response Raw/Hex',
                record.protocol_type,
            );
            const content = block.querySelector('.interaction-detail-block-content');
            appendCode(content, 'Header', value.head_text || '', Boolean(value.head_text));
            if (value.raw_packet) {
                appendCode(content, 'Raw Hex', value.raw_packet.raw_hex || '', Boolean(value.raw_packet.raw_hex));
            } else {
                const body = value.body || {};
                const kind = String(body.kind || '').toLowerCase();
                appendCode(content, 'Body Text/Hex', body.text || '', ['text', 'json', 'xml'].includes(kind) && Boolean(body.text));
            }
            columns.appendChild(block);
        });
        parent.appendChild(columns);
    }

    function renderAttachments(parent, record) {
        const refs = [];
        ['request', 'response'].forEach(side => {
            const value = record[side] || {};
            (value.body && value.body.attachments || []).forEach(ref => {
                if (side === 'request' && isMatchedImageBody(value.body)
                    && String(ref.kind || '').toLowerCase() === 'image') return;
                refs.push({ side, ref });
            });
            (value.raw_packet && value.raw_packet.attachments || []).forEach(ref => refs.push({ side, ref }));
        });
        if (!refs.length) {
            const empty = document.createElement('p');
            empty.className = 'interaction-empty-state';
            empty.textContent = '当前记录没有附件';
            parent.appendChild(empty);
            return;
        }
        refs.forEach(({ side, ref }) => {
            const row = document.createElement('article');
            row.className = 'interaction-attachment-row';
            const title = document.createElement('strong');
            title.textContent = `${side} · ${isMultiformPart(ref) ? multiformPartLabel(ref) : (ref.attachment_id || '未命名附件')}`;
            if (isMultiformPart(ref)) {
                row.dataset.role = 'multiform-part';
                row.dataset.attachmentId = ref.attachment_id || '';
                const header = document.createElement('div');
                header.className = 'interaction-attachment-row-header';
                const titleGroup = document.createElement('div');
                titleGroup.className = 'interaction-multiform-part-title-group';
                titleGroup.appendChild(createMultiformPartToggle(multiformPartLabel(ref)));
                titleGroup.appendChild(title);
                header.appendChild(titleGroup);
                row.appendChild(header);

                const content = document.createElement('div');
                content.className = 'interaction-attachment-row-content';
                const info = document.createElement('p');
                info.textContent = `${ref.kind || 'binary'} · ${ref.captured_size || 0}/${ref.size || ref.captured_size || 0} bytes · SHA1 ${ref.sha1 || '无'}`;
                content.appendChild(info);
                appendAttachmentContent(content, record, side, ref);
                row.appendChild(content);
            } else {
                row.appendChild(title);
                const info = document.createElement('p');
                info.textContent = `${ref.kind || 'binary'} · ${ref.captured_size || 0}/${ref.size || ref.captured_size || 0} bytes · SHA1 ${ref.sha1 || '无'}`;
                row.appendChild(info);
                appendAttachmentContent(row, record, side, ref);
            }
            parent.appendChild(row);
        });
    }

    function renderDetail() {
        if (!drawer || !activeEntry) return;
        const detail = drawer.panel.querySelector('.interaction-record-detail');
        const detailInner = detail.querySelector('.interaction-record-detail-inner');
        detailInner.innerHTML = '';
        const state = activeEntry.client.getState();
        const record = activeEntry.client.getRecord(state.selectedRecordKey);
        const tabs = document.createElement('div');
        tabs.className = 'interaction-detail-tabs detail-tabs';
        Object.entries(TABS).forEach(([key, label]) => {
            const button = makeButton(label, 'interaction-detail-tab');
            button.dataset.tab = key;
            if (drawer.tab === key) button.classList.add('is-active');
            tabs.appendChild(button);
        });
        detailInner.appendChild(tabs);
        if (!record) {
            const empty = document.createElement('p');
            empty.className = 'interaction-empty-state';
            empty.textContent = '选择一条交互记录查看详情';
            detailInner.appendChild(empty);
            return;
        }
        const content = document.createElement('div');
        content.className = 'interaction-detail-content';
        if (drawer.tab === 'overview') renderOverview(content, record);
        if (drawer.tab === 'request') {
            const request = createDetailSidePanel('收到 request', record.protocol_type);
            renderSide(request.querySelector('.interaction-detail-block-content'), record.request, record, 'request');
            content.appendChild(request);
        }
        if (drawer.tab === 'response') {
            const response = createDetailSidePanel('发出 response', record.protocol_type);
            renderSide(response.querySelector('.interaction-detail-block-content'), record.response, record, 'response');
            content.appendChild(response);
        }
        if (drawer.tab === 'raw') renderRaw(content, record);
        if (drawer.tab === 'attachments') renderAttachments(content, record);
        detailInner.appendChild(content);
    }

    function render(restoreSelectedFocus = false, preserveExisting = false) {
        if (!drawer || !activeEntry) return;
        drawer.panel.classList.toggle('is-mobile-detail', viewStateFor(activeEntry.key).mobileDetail);
        updateHeader();
        renderList(restoreSelectedFocus, preserveExisting);
        renderDetail();
    }

    function handleDrawerClick(event) {
        const target = event.target.closest('button, [data-record-key]');
        if (!target || !drawer) return;
        if (!target.closest('.interaction-clear-confirm') && target.dataset.action !== 'clear-records') {
            setClearConfirmationVisible(false);
        }
        if (target.dataset.filter) {
            drawer.filter = target.dataset.filter;
            drawer.panel.querySelectorAll('[data-filter]').forEach(button => button.classList.toggle('is-active', button === target));
            persistDrawerUiState();
            render();
            return;
        }
        if (target.dataset.tab) {
            drawer.tab = target.dataset.tab;
            persistDrawerUiState();
            render();
            return;
        }
        if (target.dataset.recordKey) {
            const recordKey = target.dataset.recordKey;
            viewStateFor(activeEntry.key).readRecordKeys.add(recordKey);
            activeClient().selectRecord(recordKey);
            viewStateFor(activeEntry.key).mobileDetail = true;
            persistDrawerUiState();
            render();
            focusRecordItem(recordKey);
            return;
        }
        const action = target.dataset.action;
        if (action === 'preview-image') {
            openImagePreview(target.dataset.imageUrl, target.dataset.imageAlt, target);
            return;
        }
        if (action === 'download-image') {
            downloadImage(target.dataset.imageUrl, target.dataset.downloadName);
            return;
        }
        if (action === 'toggle-multiform-part') {
            const part = target.closest('[data-role="multiform-part"]');
            if (!part) return;
            const collapsed = part.classList.toggle('is-collapsed');
            const partLabel = target.dataset.partLabel || 'Part';
            target.textContent = collapsed ? '+' : '−';
            target.setAttribute('aria-expanded', collapsed ? 'false' : 'true');
            target.title = collapsed ? `展开 ${partLabel}` : `收起 ${partLabel}`;
            target.setAttribute('aria-label', target.title);
            return;
        }
        if (action === 'set-merge-speed' && activeEntry) {
            if (activeClient() && typeof activeClient().setMergeSpeed === 'function') {
                const viewState = viewStateFor(activeEntry.key);
                pauseRecordTransitions(viewState);
                activeClient().setMergeSpeed(target.dataset.speed);
                resumeRecordTransitions(viewState);
            }
            persistDrawerUiState();
            render(true);
            return;
        }
        if (action === 'copy-code') {
            copyCode(target.dataset.copyText || '', target);
            return;
        }
        if (action === 'close-image-preview') {
            closeImagePreview();
            return;
        }
        if (action === 'close') close();
        if (action === 'back-to-list' && activeEntry) {
            viewStateFor(activeEntry.key).mobileDetail = false;
            persistDrawerUiState();
        }
        if (action === 'fullscreen') {
            drawer.panel.classList.toggle('is-fullscreen');
            drawer.backdrop.classList.toggle('is-fullscreen', drawer.panel.classList.contains('is-fullscreen'));
            persistDrawerUiState();
            syncBufferLimits({ pauseFlow: true });
        }
        if (action === 'connect') activeClient().connect(true);
        if (action === 'disconnect') activeClient().disconnect();
        if (action === 'pause') activeClient().pause();
        if (action === 'resume') activeClient().resume();
        if (action === 'toggle-sort' && activeEntry) {
            const viewState = viewStateFor(activeEntry.key);
            viewState.sortDirection = viewState.sortDirection === 'asc' ? 'desc' : 'asc';
            if (activeClient() && typeof activeClient().setRecordOrder === 'function') {
                activeClient().setRecordOrder(viewState.sortDirection);
            }
            persistDrawerUiState();
        }
        if (action === 'clear-records' && activeEntry) {
            setClearConfirmationVisible(true);
            return;
        }
        if (action === 'cancel-clear') {
            setClearConfirmationVisible(false);
            return;
        }
        if (action === 'confirm-clear' && activeEntry) {
            clearActiveRecords();
            setClearConfirmationVisible(false);
        }
            render();
    }

    function open(entry) {
        createDrawerDom();
        if (drawer.closeTimer) {
            global.clearTimeout(drawer.closeTimer);
            drawer.closeTimer = null;
        }
        if (drawer.openFrame && typeof global.cancelAnimationFrame === 'function') {
            global.cancelAnimationFrame(drawer.openFrame);
            drawer.openFrame = null;
        }
        const normalized = Object.assign({}, entry, {
            projectId: id(entry && entry.projectId),
            protocolId: id(entry && entry.protocolId),
        });
        if (!isEligible(normalized)) {
            if (KitProxy.utils && KitProxy.utils.showGlobalError) KitProxy.utils.showGlobalError(eligibilityReason(normalized));
            return false;
        }
        const key = `${normalized.projectId}:${normalized.protocolId}`;
        activeEntry = Object.assign(normalized, { key, client: clientFor(normalized) });
        const viewState = viewStateFor(key);
        viewState.mobileDetail = false;
        viewState.followLive = true;
        viewState.sortDirection = 'asc';
        if (activeEntry.client && typeof activeEntry.client.setRecordOrder === 'function') {
            activeEntry.client.setRecordOrder('asc');
        }
        drawer.lastFocus = entry.triggerButton || document.activeElement;
        drawer.filter = 'all';
        drawer.tab = 'overview';
        drawer.panel.hidden = false;
        drawer.backdrop.hidden = false;
        drawer.panel.classList.remove('is-open');
        drawer.panel.classList.toggle('is-mobile-detail', viewState.mobileDetail);
        drawer.backdrop.classList.remove('is-open');
        syncBufferLimits();
        const hydrate = activeEntry.client && typeof activeEntry.client.hydrateWorkspace === 'function'
            ? activeEntry.client.hydrateWorkspace()
            : Promise.resolve({ status: 'disabled', restored: false });
        hydrate.then(result => {
            if (!activeEntry || activeEntry.key !== key) return;
            if (result && result.restored) {
                const restored = activeEntry.client.getState();
                const uiState = restored.uiState || {};
                drawer.filter = uiState.filter || drawer.filter;
                drawer.tab = uiState.detailTab || drawer.tab;
                viewState.mobileDetail = uiState.mobileDetail === true;
                viewState.followLive = uiState.followLive !== false;
                viewState.sortDirection = uiState.sortDirection === 'desc' ? 'desc' : 'asc';
                drawer.panel.classList.toggle('is-fullscreen', uiState.fullscreen === true);
                drawer.backdrop.classList.toggle('is-fullscreen', uiState.fullscreen === true);
                if (typeof activeEntry.client.setRecordOrder === 'function') {
                    activeEntry.client.setRecordOrder(viewState.sortDirection);
                }
            }
            syncBufferLimits();
            persistDrawerUiState(true);
            render();
        }).catch(error => {
            if (global.console && console.warn) console.warn('恢复实时交互工作区失败:', error);
            persistDrawerUiState(true);
            render();
        });
        render();
        const showDrawer = () => {
            drawer.openFrame = null;
            if (!drawer || !activeEntry || drawer.panel.hidden) return;
            drawer.panel.classList.add('is-open');
            drawer.backdrop.classList.add('is-open');
        };
        if (typeof global.requestAnimationFrame === 'function') {
            drawer.openFrame = global.requestAnimationFrame(showDrawer);
        } else {
            drawer.openFrame = global.setTimeout(showDrawer, 0);
        }
        const closeButton = drawer.panel.querySelector('[data-action="close"]');
        if (closeButton) closeButton.focus();
        return true;
    }

    function restore(entry) {
        return open(Object.assign({}, entry, { restore: true }));
    }

    function close() {
        if (!drawer || drawer.panel.hidden) return;
        if (drawer.closeTimer) return;
        if (drawer.openFrame) {
            if (typeof global.cancelAnimationFrame === 'function') global.cancelAnimationFrame(drawer.openFrame);
            else global.clearTimeout(drawer.openFrame);
            drawer.openFrame = null;
        }
        closeImagePreview();
        hideCopyToast();
        drawer.panel.classList.remove('is-open', 'is-fullscreen');
        drawer.backdrop.classList.remove('is-open', 'is-fullscreen');
        setClearConfirmationVisible(false);
        if (drawer.lastFocus && typeof drawer.lastFocus.focus === 'function') drawer.lastFocus.focus();
        drawer.lastFocus = null;
        drawer.closeTimer = global.setTimeout(() => {
            drawer.closeTimer = null;
            drawer.panel.hidden = true;
            drawer.backdrop.hidden = true;
            activeEntry = null;
        }, DRAWER_TRANSITION_MS);
        persistDrawerUiState(true);
    }

    function destroyClient(projectId, protocolId) {
        const key = `${id(projectId)}:${id(protocolId)}`;
        const client = clients.get(key);
        if (!client) return;
        const viewState = viewStates.get(key);
        if (viewState) clearTransientAnimations(viewState);
        client.destroy();
        clients.delete(key);
        viewStates.delete(key);
        if (activeEntry && activeEntry.key === key) close();
    }

    function cleanupProject(projectId) {
        Array.from(clients.keys()).filter(key => key.startsWith(`${id(projectId)}:`)).forEach(key => {
            const [project, protocol] = key.split(':');
            destroyClient(project, protocol);
        });
    }

    function cleanupProtocol(projectId, protocolId) {
        destroyClient(projectId, protocolId);
    }

    function refreshEntryState(projectId, protocolId, state) {
        const key = `${id(projectId)}:${id(protocolId)}`;
        if (!activeEntry || activeEntry.key !== key) return;
        Object.assign(activeEntry, state || {});
        if (!isEligible(activeEntry)) cleanupProtocol(projectId, protocolId);
    }

    function isOpen() {
        return Boolean(drawer && !drawer.panel.hidden && !drawer.closeTimer);
    }

    KitProxy.protocolInteractionDrawer = {
        open,
        restore,
        close,
        isOpen,
        cleanupProject,
        cleanupProtocol,
        refreshEntryState,
        eligibilityReason,
        isEligible,
    };
})(typeof window !== 'undefined' ? window : globalThis);

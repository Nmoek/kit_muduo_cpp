(function initProtocolItemFormPage(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});
    const utils = KitProxy.utils || {};
    const escapeHTML = utils.escapeHTML || function(value) {
        return String(value == null ? '' : value);
    };

    const REQ_BODY = 1;
    const RESP_BODY = 2;

    const pageState = {
        projectId: -1,
        protocolId: null,
        project: null,
        protocol: null,
        mode: 'create',
        protocolType: null,
        bodyState: {
            request: { content: '', bodyType: 'json' },
            response: { content: '', bodyType: 'json' },
            activeTab: 'request',
        },
        initialBodyState: {
            request: { content: '', bodyType: 'json' },
            response: { content: '', bodyType: 'json' },
        },
        initialReqCfg: {},
        initialRespCfg: {},
        reqPatternFields: null,
        respPatternFields: null,
        bodyEditor: null,
        isSubmitting: false,
    };

    /**
     * 保留当前调试参数并返回协议项列表页。
     * @returns {string}
     */
    function buildProtocolListUrl() {
        const params = new URLSearchParams(global.location.search);
        params.delete('protocolId');
        if (pageState.projectId > 0) {
            params.set('projectId', String(pageState.projectId));
        }
        const query = params.toString();
        return query ? `protocol_items.html?${query}` : 'protocol_items.html';
    }

    /**
     * @returns {{ projectId: number; protocolId: number | null; }}
     */
    function readURLParams() {
        const params = new URLSearchParams(global.location.search);
        const projectId = Number(params.get('projectId'));
        const protocolIdValue = params.get('protocolId');
        const protocolId = Number(protocolIdValue);

        return {
            projectId: Number.isInteger(projectId) && projectId > 0 ? projectId : -1,
            protocolId: Number.isInteger(protocolId) && protocolId > 0 ? protocolId : null,
        };
    }

    /**
     * @param {string} message
     */
    function showPageError(message) {
        const errorBox = document.getElementById('protocol-form-error');
        if (!errorBox) return;

        errorBox.style.display = 'block';
        errorBox.innerHTML = `
            <p>${escapeHTML(message)}</p>
            <a class="back-link" href="${escapeHTML(buildProtocolListUrl())}">返回协议项列表</a>
        `;
    }

    function clearPageError() {
        const errorBox = document.getElementById('protocol-form-error');
        if (!errorBox) return;

        errorBox.style.display = 'none';
        errorBox.textContent = '';
    }

    /**
     * @param {string} message
     */
    function setInlineError(message) {
        const errorBox = document.getElementById('protocol-form-error');
        if (!errorBox) return;

        errorBox.style.display = message ? 'block' : 'none';
        errorBox.textContent = message || '';
    }

    function navigateBack() {
        const targetUrl = buildProtocolListUrl();
        const navigateEvent = new CustomEvent('protocol-item-form:navigate-back', {
            bubbles: true,
            cancelable: true,
            detail: {
                projectId: pageState.projectId,
                url: targetUrl,
            },
        });
        document.dispatchEvent(navigateEvent);
        if (navigateEvent.defaultPrevented) return;
        global.location.href = targetUrl;
    }

    /**
     * @param {Uint8Array | ArrayBuffer | string | null | undefined} bodyData
     * @returns {string}
     */
    function decodeBodyData(bodyData) {
        if (KitProxy.bodySyntax && typeof KitProxy.bodySyntax.decodeBodyData === 'function') {
            return KitProxy.bodySyntax.decodeBodyData(bodyData);
        }

        if (bodyData == null) return '';
        if (typeof bodyData === 'string') return bodyData;
        if (bodyData instanceof ArrayBuffer) {
            return new TextDecoder().decode(new Uint8Array(bodyData));
        }
        if (ArrayBuffer.isView(bodyData)) {
            return new TextDecoder().decode(bodyData);
        }
        return String(bodyData);
    }

    /**
     * @param {any} value
     * @returns {any}
     */
    function clone(value) {
        return JSON.parse(JSON.stringify(value == null ? {} : value));
    }

    /**
     * @param {any} project
     */
    function renderProjectContext(project) {
        const context = document.getElementById('protocol-form-project-context');
        if (!context) return;

        const endpointLabel = project.mode === ProjectMode.SERVER ? '监听端口' : '目标IP/端口';
        const endpointValue = project.mode === ProjectMode.SERVER ? project.listen_port : project.target_ip || '未设置';

        context.innerHTML = `
            <span class="service-meta-item service-detail-chip">
                <span class="meta-label">服务</span>
                <span class="meta-value">${escapeHTML(project.name || '')}</span>
            </span>
            <span class="service-meta-item service-detail-chip">
                <span class="meta-label">协议</span>
                <span class="meta-value">${escapeHTML(ProtocolTypeStr[project.protocol_type] || '未知协议')}</span>
            </span>
            <span class="service-meta-item service-detail-chip">
                <span class="meta-label">模式</span>
                <span class="meta-value">${escapeHTML(ProjectModeStr[project.mode] || '未知模式')}</span>
            </span>
            <span class="service-meta-item service-detail-chip">
                <span class="meta-label">${escapeHTML(endpointLabel)}</span>
                <span class="meta-value">${escapeHTML(endpointValue)}</span>
            </span>
        `;
    }

    function renderPageTitle() {
        const title = document.getElementById('protocol-form-title');
        const subtitle = document.getElementById('protocol-form-subtitle');
        const backLink = document.getElementById('back-protocol-list');
        const saveButton = document.getElementById('save-protocol-form');

        if (title) {
            title.textContent = pageState.mode === 'edit' ? '修改协议项' : '添加协议项';
        }

        if (subtitle) {
            subtitle.textContent = pageState.mode === 'edit'
                ? '修改当前协议项配置、请求 Body 和响应 Body'
                : '新增当前测试服务下的请求校验和响应行为';
        }

        if (backLink) {
            backLink.href = buildProtocolListUrl();
        }

        if (saveButton) {
            saveButton.textContent = pageState.mode === 'edit' ? '保存修改' : '确认添加';
        }
    }

    /**
     * @returns {Array<{ value: string; label: string; enabled?: boolean; reserved?: boolean; }>}
     */
    function getAllowedBodyTypes() {
        if (KitProxy.protocolTypes && typeof KitProxy.protocolTypes.getBodyTypeOptions === 'function') {
            return KitProxy.protocolTypes.getBodyTypeOptions(pageState.project.protocol_type);
        }
        return [
            { value: 'json', label: 'JSON', enabled: true },
            { value: 'xml', label: 'XML', enabled: true },
            { value: 'text', label: 'Text', enabled: true },
            { value: 'binary', label: 'Binary', enabled: false, reserved: true },
        ];
    }

    /**
     * @param {HTMLElement} container
     * @param {any=} protocol
     */
    function renderHTTPFields(container, protocol) {
        const method = protocol && protocol.req_cfg ? protocol.req_cfg.method : 'GET';
        const path = protocol && protocol.req_cfg ? protocol.req_cfg.path : '/api/';
        container.innerHTML = `
            <div class="form-group protocol-http-method-group">
                <label>请求方法</label>
                <div class="method-radios protocol-method-radios">
                    ${['GET', 'POST', 'PUT', 'DELETE'].map(item => `
                        <label><input type="radio" name="request-method" value="${item}" ${method === item ? 'checked' : ''} required> ${item}</label>
                    `).join('')}
                </div>
            </div>
            <div class="form-group protocol-http-path-group">
                <label for="request-path">请求路径</label>
                <input type="text" id="request-path" value="${escapeHTML(path || '/api/')}" placeholder="输入请求路径" required>
                <div class="path-hint">必须以 / 开头，例如：/api/v1/test</div>
            </div>
        `;
    }

    /**
     * @param {Array<any>} fields
     * @returns {string}
     */
    function summarizePatternFields(fields) {
        return Array.isArray(fields) && fields.length > 0 ? `已设置 ${fields.length} 个字段` : '未设置';
    }

    /**
     * @param {HTMLElement} button
     * @param {Array<any>} fields
     */
    function updatePatternButtonState(button, fields) {
        const status = button.closest('.form-group')?.querySelector('.import-status');
        button.dataset.patternInfos = JSON.stringify({
            special_fields: {},
            common_fields: fields || [],
        });
        if (status) {
            status.textContent = summarizePatternFields(fields);
            status.style.display = 'inline';
        }
    }

    /**
     * @param {HTMLElement} container
     * @param {any=} protocol
     */
    function renderTCPFields(container, protocol) {
        const reqCfg = protocol && protocol.req_cfg ? protocol.req_cfg : {};
        const respCfg = protocol && protocol.resp_cfg ? protocol.resp_cfg : {};
        pageState.reqPatternFields = clone(reqCfg.common_fields || []);
        pageState.respPatternFields = clone(respCfg.common_fields || []);

        container.innerHTML = `
            <div class="protocol-config-subsection protocol-config-subsection-request">
                <div class="protocol-config-subsection-title">请求侧配置</div>
                <div class="form-group">
                <label for="req-function-code-value">请求功能码</label>
                <input id="req-function-code-value" class="req-function-code-value" value="${escapeHTML(reqCfg.function_code_filed_value || '')}" placeholder="示例：H010203">
                <div class="path-hint">需要填写时必须以 H 开头</div>
                </div>
                <div class="form-group">
                <div class="pattern-header">
                    <label>校验请求头部</label>
                    <span class="import-status">${escapeHTML(summarizePatternFields(pageState.reqPatternFields))}</span>
                </div>
                <button type="button" class="pattern-config-btn" id="req-pattern-infos">配置请求头部字段</button>
                </div>
            </div>
            <div class="protocol-config-subsection protocol-config-subsection-response">
                <div class="protocol-config-subsection-title">响应侧配置</div>
                <div class="form-group">
                <label for="resp-function-code-value">响应功能码</label>
                <input id="resp-function-code-value" class="resp-function-code-value" value="${escapeHTML(respCfg.function_code_filed_value || '')}" placeholder="示例：H010203">
                <div class="path-hint">需要填写时必须以 H 开头</div>
                </div>
                <div class="form-group">
                <div class="pattern-header">
                    <label>目标响应头部</label>
                    <span class="import-status">${escapeHTML(summarizePatternFields(pageState.respPatternFields))}</span>
                </div>
                <button type="button" class="pattern-config-btn" id="resp-pattern-infos">配置响应头部字段</button>
                </div>
            </div>
        `;

        updatePatternButtonState(container.querySelector('#req-pattern-infos'), pageState.reqPatternFields);
        updatePatternButtonState(container.querySelector('#resp-pattern-infos'), pageState.respPatternFields);
        bindTcpPatternButtons(container);
    }

    /**
     * @param {HTMLElement} container
     */
    function bindTcpPatternButtons(container) {
        container.querySelectorAll('.pattern-config-btn').forEach(button => {
            const isReq = button.id === 'req-pattern-infos';
            button.addEventListener('click', async function(event) {
                event.preventDefault();
                event.stopPropagation();

                const loading = showLoading('正在加载 TCP 格式字段...');
                let specialFields = {};
                try {
                    const patternInfo = await KitProxy.api.getProjectPatternInfo(pageState.projectId);
                    specialFields = patternInfo && patternInfo.special_fields ? patternInfo.special_fields : {};
                } catch (error) {
                    hideLoading(loading);
                    setInlineError('获取 TCP 格式字段失败：' + error.message);
                    return;
                }

                await delay(100);
                hideLoading(loading);

                const currentCommonFields = isReq ? pageState.reqPatternFields : pageState.respPatternFields;
                const patternInfosMap = {
                    special_fields: specialFields,
                    common_fields: currentCommonFields || [],
                };
                const statusElement = button.closest('.form-group')?.querySelector('.import-status');
                createCustomTcpPatternModal(
                    button,
                    isReq ? '请求头部全部字段' : '响应头部全部字段',
                    patternInfosMap,
                    statusElement,
                    false,
                    function(patternFieldInfos) {
                        const fields = clone(patternFieldInfos.common_fields || []);
                        if (isReq) {
                            pageState.reqPatternFields = fields;
                        } else {
                            pageState.respPatternFields = fields;
                        }
                        updatePatternButtonState(button, fields);
                    },
                );
            });
        });
    }

    /**
     * @param {number} protocolType
     * @param {any=} protocol
     */
    function renderTypeSpecificFields(protocolType, protocol) {
        const container = document.getElementById('protocol-type-fields');
        if (!container) return;

        if (Number(protocolType) === ProtocolType.HTTP) {
            renderHTTPFields(container, protocol);
            return;
        }

        if (Number(protocolType) === ProtocolType.CUSTOM_TCP) {
            renderTCPFields(container, protocol);
            return;
        }

        container.innerHTML = '<div class="form-group"><p class="form-note">当前协议类型暂不支持协议项表单。</p></div>';
    }

    function syncActiveBodyFromEditor() {
        if (!pageState.bodyEditor) return;
        const activeTab = pageState.bodyState.activeTab;
        pageState.bodyState[activeTab] = {
            content: pageState.bodyEditor.getValue(),
            bodyType: pageState.bodyEditor.getType(),
        };
    }

    /**
     * @param {'request' | 'response'} tab
     */
    function setActiveBodyTab(tab) {
        if (!pageState.bodyEditor || tab === pageState.bodyState.activeTab) return;

        syncActiveBodyFromEditor();
        pageState.bodyState.activeTab = tab;
        const target = pageState.bodyState[tab];
        pageState.bodyEditor.setType(target.bodyType || 'json');
        pageState.bodyEditor.setValue(target.content || '');

        document.querySelectorAll('.body-switch-btn').forEach(button => {
            button.classList.toggle('is-active', button.dataset.bodyTab === tab);
        });
    }

    function setupBodyToggleButtons() {
        document.querySelectorAll('.body-switch-btn').forEach(button => {
            button.addEventListener('click', function() {
                setActiveBodyTab(button.dataset.bodyTab === 'response' ? 'response' : 'request');
            });
        });
    }

    function setupBodyEditor() {
        const host = document.getElementById('protocol-body-editor-host');
        if (!host) return;

        pageState.bodyEditor = KitProxy.bodyEditor.create(host, {
            idPrefix: 'protocol-form-body',
            value: pageState.bodyState.request.content,
            bodyType: pageState.bodyState.request.bodyType,
            allowedTypes: getAllowedBodyTypes(),
            placeholder: '输入 Body 内容...',
        });
    }

    /**
     * @param {'request' | 'response'} tab
     * @returns {{ valid: boolean; message: string; }}
     */
    function validateBodyTab(tab) {
        const label = tab === 'request' ? '校验请求Body' : '目标响应Body';
        const body = pageState.bodyState[tab];
        const validation = KitProxy.bodySyntax.validate(body.content, body.bodyType);
        if (!validation.valid) {
            return {
                valid: false,
                message: `${label}格式错误：${validation.message}`,
            };
        }
        return { valid: true, message: '' };
    }

    /**
     * @returns {{ name: string; }}
     */
    function collectBaseFields() {
        const name = document.getElementById('protocol-item-name')?.value.trim() || '';
        if (!name) {
            throw new Error('协议项名称不能为空');
        }
        return { name };
    }

    /**
     * @returns {{ req_cfg: any; resp_cfg: any; }}
     */
    function collectHTTPCfg() {
        const method = document.querySelector('input[name="request-method"]:checked')?.value;
        const path = document.getElementById('request-path')?.value.trim() || '';

        if (!method) {
            throw new Error('请选择请求方法');
        }
        if (!utils.validateHttpPath(path)) {
            throw new Error('请求路径必须以 / 开头');
        }

        return {
            req_cfg: { method, path },
            resp_cfg: pageState.mode === 'edit' ? clone(pageState.initialRespCfg) : {},
        };
    }

    /**
     * @returns {{ req_cfg: any; resp_cfg: any; }}
     */
    function collectTCPCfg() {
        const reqFunctionCode = document.getElementById('req-function-code-value')?.value.trim() || '';
        const respFunctionCode = document.getElementById('resp-function-code-value')?.value.trim() || '';

        if (reqFunctionCode && !reqFunctionCode.startsWith('H')) {
            throw new Error('请求功能码必须以 H 开头');
        }
        if (respFunctionCode && !respFunctionCode.startsWith('H')) {
            throw new Error('响应功能码必须以 H 开头');
        }

        return {
            req_cfg: {
                function_code_filed_value: reqFunctionCode,
                common_fields: clone(pageState.reqPatternFields || []),
            },
            resp_cfg: {
                function_code_filed_value: respFunctionCode,
                common_fields: clone(pageState.respPatternFields || []),
            },
        };
    }

    /**
     * @returns {{ name: string; req_cfg: any; resp_cfg: any; request_body: string; response_body: string; req_body_type: string; resp_body_type: string; }}
     */
    function collectFormData() {
        syncActiveBodyFromEditor();

        const requestValidation = validateBodyTab('request');
        if (!requestValidation.valid) {
            throw new Error(requestValidation.message);
        }

        const responseValidation = validateBodyTab('response');
        if (!responseValidation.valid) {
            throw new Error(responseValidation.message);
        }

        const base = collectBaseFields();
        const cfg = Number(pageState.protocolType) === ProtocolType.CUSTOM_TCP
            ? collectTCPCfg()
            : collectHTTPCfg();

        return Object.assign({}, base, cfg, {
            request_body: pageState.bodyState.request.content.trim(),
            response_body: pageState.bodyState.response.content.trim(),
            req_body_type: pageState.bodyState.request.bodyType,
            resp_body_type: pageState.bodyState.response.bodyType,
        });
    }

    /**
     * @param {any} data
     * @returns {any}
     */
    function buildAddPayload(data) {
        const isTcp = Number(pageState.protocolType) === ProtocolType.CUSTOM_TCP;
        return {
            cfg_header: {
                name: data.name,
                type: isTcp ? 'TCP' : 'HTTP',
                project_id: pageState.projectId,
                req_body_type: data.req_body_type,
                resp_body_type: data.resp_body_type,
                ...(isTcp ? { is_endian: 1 } : {}),
            },
            req_cfg: data.req_cfg,
            resp_cfg: data.resp_cfg,
            request_body: data.request_body,
            response_body: data.response_body,
        };
    }

    /**
     * @param {any} left
     * @param {any} right
     * @returns {boolean}
     */
    function isSameJSON(left, right) {
        return JSON.stringify(left || {}) === JSON.stringify(right || {});
    }

    /**
     * @param {any} data
     */
    async function handleAdd(data) {
        await KitProxy.api.addProtocol(buildAddPayload(data));
    }

    /**
     * @param {any} data
     */
    async function handleEdit(data) {
        if (!pageState.protocol) {
            throw new Error('缺少协议项数据，无法保存修改');
        }

        if (data.name !== (pageState.protocol.name || '')) {
            await KitProxy.api.updateProtocolName(pageState.protocolId, data.name);
        }

        if (!isSameJSON(data.req_cfg, pageState.initialReqCfg)) {
            await KitProxy.api.updateProtocolCfg(pageState.protocolId, pageState.projectId, REQ_BODY, data.req_cfg);
        }

        if (!isSameJSON(data.resp_cfg, pageState.initialRespCfg)) {
            await KitProxy.api.updateProtocolCfg(pageState.protocolId, pageState.projectId, RESP_BODY, data.resp_cfg);
        }

        if (
            data.req_body_type !== pageState.initialBodyState.request.bodyType ||
            data.request_body !== pageState.initialBodyState.request.content
        ) {
            await KitProxy.api.updateProtocolBody(pageState.protocolId, pageState.projectId, REQ_BODY, pageState.protocol.type, data.req_body_type, data.request_body);
        }

        if (
            data.resp_body_type !== pageState.initialBodyState.response.bodyType ||
            data.response_body !== pageState.initialBodyState.response.content
        ) {
            await KitProxy.api.updateProtocolBody(pageState.protocolId, pageState.projectId, RESP_BODY, pageState.protocol.type, data.resp_body_type, data.response_body);
        }
    }

    /**
     * @param {Event} event
     */
    async function handleSubmit(event) {
        event.preventDefault();
        if (pageState.isSubmitting) return;

        const saveButton = document.getElementById('save-protocol-form');
        const loading = showLoading(pageState.mode === 'edit' ? '正在保存协议项...' : '正在添加协议项...');
        pageState.isSubmitting = true;
        if (saveButton) saveButton.disabled = true;
        clearPageError();

        try {
            const data = collectFormData();
            if (pageState.mode === 'edit') {
                await handleEdit(data);
            } else {
                await handleAdd(data);
            }
            await delay(300);
            navigateBack();
        } catch (error) {
            setInlineError((pageState.mode === 'edit' ? '修改协议项失败：' : '添加协议项失败：') + error.message);
        } finally {
            hideLoading(loading);
            pageState.isSubmitting = false;
            if (saveButton) saveButton.disabled = false;
        }
    }

    /**
     * @param {any} protocol
     */
    function applyProtocolToForm(protocol) {
        const nameInput = document.getElementById('protocol-item-name');
        if (nameInput) {
            nameInput.value = protocol.name || '';
        }
        renderTypeSpecificFields(pageState.protocolType, protocol);
    }

    async function loadEditProtocol() {
        if (!pageState.protocolId) return;

        const protocols = await KitProxy.api.getProtocol(pageState.protocolId);
        if (!Array.isArray(protocols) || protocols.length <= 0) {
            throw new Error('获取协议项详情失败');
        }

        pageState.protocol = protocols[0];
        pageState.initialReqCfg = clone(pageState.protocol.req_cfg || {});
        pageState.initialRespCfg = clone(pageState.protocol.resp_cfg || {});

        const [requestBodyInfo, responseBodyInfo] = await Promise.all([
            KitProxy.api.getProtocolBody(pageState.protocolId, REQ_BODY),
            KitProxy.api.getProtocolBody(pageState.protocolId, RESP_BODY),
        ]);

        pageState.bodyState.request = {
            bodyType: requestBodyInfo[0] || pageState.protocol.req_body_type || 'json',
            content: decodeBodyData(requestBodyInfo[1]),
        };
        pageState.bodyState.response = {
            bodyType: responseBodyInfo[0] || pageState.protocol.resp_body_type || 'json',
            content: decodeBodyData(responseBodyInfo[1]),
        };
        pageState.initialBodyState = {
            request: clone(pageState.bodyState.request),
            response: clone(pageState.bodyState.response),
        };
    }

    function bindFormActions() {
        document.getElementById('protocol-item-form')?.addEventListener('submit', handleSubmit);
        document.getElementById('cancel-protocol-form')?.addEventListener('click', function() {
            navigateBack();
        });
    }

    async function initPage() {
        const params = readURLParams();
        pageState.projectId = params.projectId;
        pageState.protocolId = params.protocolId;
        pageState.mode = pageState.protocolId ? 'edit' : 'create';

        renderPageTitle();

        if (pageState.projectId < 0) {
            showPageError('缺少或非法的测试服务 ID，无法加载协议项表单。');
            return;
        }

        const loading = showLoading('正在加载协议项表单...');
        try {
            const projects = await KitProxy.api.getProject(pageState.projectId);
            if (!Array.isArray(projects) || projects.length <= 0) {
                throw new Error('获取测试服务信息失败');
            }

            pageState.project = projects[0];
            pageState.protocolType = pageState.project.protocol_type;

            if (pageState.mode === 'edit') {
                await loadEditProtocol();
            }

            renderPageTitle();
            renderProjectContext(pageState.project);
            const nameInput = document.getElementById('protocol-item-name');
            if (nameInput && pageState.mode === 'create') {
                nameInput.value = '';
            }
            if (pageState.protocol) {
                applyProtocolToForm(pageState.protocol);
            } else {
                renderTypeSpecificFields(pageState.protocolType, null);
            }
            setupBodyEditor();
            setupBodyToggleButtons();
            bindFormActions();
        } catch (error) {
            showPageError(error.message);
        } finally {
            hideLoading(loading);
        }
    }

    KitProxy.protocolItemForm = {
        pageState,
        buildProtocolListUrl,
        readURLParams,
        collectFormData,
        buildAddPayload,
        setActiveBodyTab,
        navigateBack,
        initPage,
    };

    if (!KitProxy.__disableAutoInitProtocolItemForm) {
        document.addEventListener('DOMContentLoaded', initPage);
    }
})(typeof window !== 'undefined' ? window : globalThis);

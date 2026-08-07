(function initProtocolTypeRegistry(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});
    const registry = {};
    const protocolItemTypeIndex = {};
    const REQUEST_BODY_TYPE_OPTIONS = Object.freeze([
        { value: 'none', label: 'None', enabled: true },
        { value: 'empty', label: 'Empty', enabled: true },
        { value: 'json', label: 'JSON', enabled: true },
        { value: 'xml', label: 'XML', enabled: true },
        { value: 'text', label: 'Text', enabled: true },
        { value: 'image', label: 'Image', enabled: true },
        { value: 'binary', label: 'Binary', enabled: true },
    ]);
    const RESPONSE_BODY_TYPE_OPTIONS = REQUEST_BODY_TYPE_OPTIONS;

    /**
     * 注册一种测试服务协议类型的页面能力。
     * @param {number} projectProtocolType
     * @param {{
     *   protocolItemType?: string;
     *   addProtocolModal?: any;
     *   protocolItemGrid?: any;
     *   serviceExtraFieldsHTML?: Function;
     *   bindServiceExtraActions?: Function;
     *   renderAddServiceExtraControl?: Function;
     *   collectAddServiceExtraPayload?: Function;
     *   bodyTypeOptions?: Array<{ value: string; label: string; enabled?: boolean; reserved?: boolean; }>;
     *   requestBodyTypeOptions?: Array<{ value: string; label: string; enabled?: boolean; reserved?: boolean; }>;
     *   responseBodyTypeOptions?: Array<{ value: string; label: string; enabled?: boolean; reserved?: boolean; }>;
     * }} options
     */
    function register(projectProtocolType, options) {
        registry[Number(projectProtocolType)] = options;

        if (options.protocolItemType) {
            protocolItemTypeIndex[String(options.protocolItemType).toUpperCase()] = options;
        }
    }

    /**
     * @param {number | string} projectProtocolType
     * @returns {any}
     */
    function getByProjectProtocolType(projectProtocolType) {
        return registry[Number(projectProtocolType)] || null;
    }

    /**
     * @param {string} protocolItemType
     * @returns {any}
     */
    function getByProtocolItemType(protocolItemType) {
        return protocolItemTypeIndex[String(protocolItemType || '').toUpperCase()] || null;
    }

    /**
     * @param {number | string} projectProtocolType
     * @returns {any}
     */
    function getAddProtocolModal(projectProtocolType) {
        const entry = getByProjectProtocolType(projectProtocolType);
        return entry ? entry.addProtocolModal : null;
    }

    /**
     * @param {any} protocol
     * @returns {HTMLDivElement}
     */
    function createProtocolItemGrid(protocol) {
        const entry = getByProtocolItemType(protocol.type);

        if (entry && entry.protocolItemGrid) {
            return createProtocolItemGrids(entry.protocolItemGrid, protocol);
        }

        const grids = document.createElement('div');
        grids.className = 'details-grid';
        grids.innerHTML = '<div class="protocol-field"><label>协议类型</label><div class="value">暂不支持</div></div>';
        return grids;
    }

    /**
     * @param {any} project
     * @returns {string}
     */
    function serviceExtraFieldsHTML(project) {
        const entry = getByProjectProtocolType(project.protocol_type);
        if (!entry || typeof entry.serviceExtraFieldsHTML !== 'function') return '';
        return entry.serviceExtraFieldsHTML(project);
    }

    /**
     * @param {HTMLElement} serviceCard
     * @param {any} project
     */
    function bindServiceExtraActions(serviceCard, project) {
        const entry = getByProjectProtocolType(project.protocol_type);
        if (entry && typeof entry.bindServiceExtraActions === 'function') {
            entry.bindServiceExtraActions(serviceCard, project);
        }
    }

    /**
     * @param {HTMLElement} formContainer
     * @param {number} projectProtocolType
     */
    function renderAddServiceExtraControl(formContainer, projectProtocolType) {
        formContainer.querySelectorAll('.protocol-extra-control').forEach(node => node.remove());

        const entry = getByProjectProtocolType(projectProtocolType);
        if (entry && typeof entry.renderAddServiceExtraControl === 'function') {
            entry.renderAddServiceExtraControl(formContainer);
        }
    }

    /**
     * @param {HTMLElement} modal
     * @param {number} projectProtocolType
     * @returns {{ pattern_info?: any; }}
     */
    function collectAddServiceExtraPayload(modal, projectProtocolType) {
        const entry = getByProjectProtocolType(projectProtocolType);
        if (entry && typeof entry.collectAddServiceExtraPayload === 'function') {
            return entry.collectAddServiceExtraPayload(modal);
        }

        return {
            pattern_info: {},
        };
    }

    /**
     * @param {number | string} projectProtocolType
     * @param {Array<{ value: string; label: string; enabled?: boolean; reserved?: boolean; }>} options
     * @returns {Array<{ value: string; label: string; enabled?: boolean; reserved?: boolean; }>}
     */
    function cloneBodyTypeOptions(options) {
        return options
            .filter(option => String(option && option.value || '').toLowerCase() !== 'multiform')
            .map(option => Object.assign({}, option));
    }

    /**
     * @param {number | string} projectProtocolType
     * @param {'request' | 'response'=} side
     * @returns {Array<{ value: string; label: string; enabled?: boolean; reserved?: boolean; }>}
     */
    function getBodyTypeOptions(projectProtocolType, side) {
        if (side === 'request') return getRequestBodyTypeOptions(projectProtocolType);
        return getResponseBodyTypeOptions(projectProtocolType);
    }

    /**
     * @param {number | string} projectProtocolType
     * @returns {Array<{ value: string; label: string; enabled?: boolean; reserved?: boolean; }>}
     */
    function getRequestBodyTypeOptions(projectProtocolType) {
        const entry = getByProjectProtocolType(projectProtocolType) || getByProtocolItemType(projectProtocolType);
        const options = entry && Array.isArray(entry.requestBodyTypeOptions)
            ? entry.requestBodyTypeOptions
            : REQUEST_BODY_TYPE_OPTIONS;
        return cloneBodyTypeOptions(options);
    }

    /**
     * @param {number | string} projectProtocolType
     * @returns {Array<{ value: string; label: string; enabled?: boolean; reserved?: boolean; }>}
     */
    function getResponseBodyTypeOptions(projectProtocolType) {
        const entry = getByProjectProtocolType(projectProtocolType) || getByProtocolItemType(projectProtocolType);
        const options = entry && Array.isArray(entry.responseBodyTypeOptions)
            ? entry.responseBodyTypeOptions
            : (entry && Array.isArray(entry.bodyTypeOptions) ? entry.bodyTypeOptions : RESPONSE_BODY_TYPE_OPTIONS);
        return cloneBodyTypeOptions(options);
    }

    register(ProtocolType.HTTP, {
        protocolItemType: 'HTTP',
        addProtocolModal: global.httpProtocolModal,
        protocolItemGrid: global.httpProtocolItemGrids,
        requestBodyTypeOptions: cloneBodyTypeOptions(REQUEST_BODY_TYPE_OPTIONS),
        responseBodyTypeOptions: cloneBodyTypeOptions(RESPONSE_BODY_TYPE_OPTIONS),
    });

    register(ProtocolType.CUSTOM_TCP, {
        protocolItemType: 'TCP',
        addProtocolModal: global.customTcpProtocolModal,
        protocolItemGrid: global.customTcpProtocolItemGrids,
        requestBodyTypeOptions: cloneBodyTypeOptions(REQUEST_BODY_TYPE_OPTIONS),
        responseBodyTypeOptions: cloneBodyTypeOptions(RESPONSE_BODY_TYPE_OPTIONS),
        serviceExtraFieldsHTML: function(project) {
            const escape = KitProxy.utils && KitProxy.utils.escapeHTML
                ? KitProxy.utils.escapeHTML
                : function(value) { return String(value == null ? '' : value); };
            const patternText = project.length_policy && global.tcpLengthPolicyText
                ? global.tcpLengthPolicyText(project.length_policy)
                : 'TCP格式';
            return `
                <button type="button" class="service-field project-pattern editable-service-field" id="pattern-info-${escape(project.id)}" title="点击编辑 TCP 格式">
                    <span class="field-label">TCP格式</span>
                    <span class="field-value" data-target="pattern-info-${escape(project.id)}">${escape(patternText)}</span>
                </button>
            `;
        },
        bindServiceExtraActions: function(serviceCard, project) {
            const patternField = serviceCard.querySelector('.project-pattern');
            if (!patternField) return;

            patternField.addEventListener('click', async function(e) {
                e.preventDefault();
                e.stopPropagation();

                const loading = showLoading('正在加载格式信息...');
                let patternInfo = {};

                try {
                    patternInfo = await getTcpPatternInfoReq(project.id);
                } catch(error) {
                    hideLoading(loading);
                    console.error('获取格式信息失败!', error.message);
                    KitProxy.utils.showGlobalError('获取格式信息失败!');
                    return;
                }

                await delay(200);
                hideLoading(loading);

                createCustomTcpPatternModal(
                    patternField,
                    '项目格式字段',
                    patternInfo,
                    null,
                    true,
                    async function(nextPatternInfo) {
                    if(!confirm('重新配置格式会使所有协议项失效，是否继续')) {
                        return;
                    }

                    const ok = await updateTcpPatternInfoReq(project.id, nextPatternInfo);
                    if(!ok) {
                        KitProxy.utils.showGlobalError('TCP格式修改失败!');
                        return false;
                    }

                    const textNode = patternField.querySelector('.meta-value');
                    if (textNode) {
                        textNode.textContent = tcpLengthPolicyText(nextPatternInfo.length_policy);
                    }
                    if (KitProxy.protocolItemsPage && typeof KitProxy.protocolItemsPage.clearProtocolRuntimeStateOverrides === 'function') {
                        KitProxy.protocolItemsPage.clearProtocolRuntimeStateOverrides();
                    }
                    if (KitProxy.protocolItemsPage && typeof KitProxy.protocolItemsPage.loadProtocolItems === 'function') {
                        await KitProxy.protocolItemsPage.loadProtocolItems();
                    }
                    return true;
                    },
                );
            });
        },
        renderAddServiceExtraControl: function(formContainer) {
            const patternControl = document.createElement('div');
            patternControl.className = 'form-group pattern-control protocol-extra-control';
            patternControl.innerHTML = tcpPatternControlHTML();

            formContainer.appendChild(patternControl);

            bindTcpPatternTypeControls(
                patternControl.querySelector('#pattern-type'),
                patternControl.querySelector('#pattern-infos'),
                patternControl.querySelector('#first-pattern-import-status'),
            );
        },
        collectAddServiceExtraPayload: function(modal) {
            const cachedPatternInfos = modal.querySelector('#pattern-infos').dataset.patternInfos;

            if (!cachedPatternInfos) {
                throw new Error('格式类型具体内容未配置，请检查!');
            }

            const parsedPatternInfo = JSON.parse(cachedPatternInfos);
            return {
                pattern_info: buildV2TcpPatternInfoFromEditor(parsedPatternInfo, parsedPatternInfo.length_policy),
            };
        },
    });

    KitProxy.protocolTypes = {
        register,
        getByProjectProtocolType,
        getByProtocolItemType,
        getAddProtocolModal,
        createProtocolItemGrid,
        serviceExtraFieldsHTML,
        bindServiceExtraActions,
        renderAddServiceExtraControl,
        collectAddServiceExtraPayload,
        getBodyTypeOptions,
        getRequestBodyTypeOptions,
        getResponseBodyTypeOptions,
    };

    global.ProtocolTypeRegistry = KitProxy.protocolTypes;
})(typeof window !== 'undefined' ? window : globalThis);

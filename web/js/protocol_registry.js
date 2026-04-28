(function initProtocolTypeRegistry(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});
    const registry = {};
    const protocolItemTypeIndex = {};

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
     * @returns {{ pattern_type?: number; pattern_info?: any; }}
     */
    function collectAddServiceExtraPayload(modal, projectProtocolType) {
        const entry = getByProjectProtocolType(projectProtocolType);
        if (entry && typeof entry.collectAddServiceExtraPayload === 'function') {
            return entry.collectAddServiceExtraPayload(modal);
        }

        return {
            pattern_type: PatternType.STANDARD,
            pattern_info: {},
        };
    }

    register(ProtocolType.HTTP, {
        protocolItemType: 'HTTP',
        addProtocolModal: global.httpProtocolModal,
        protocolItemGrid: global.httpProtocolItemGrids,
    });

    register(ProtocolType.CUSTOM_TCP, {
        protocolItemType: 'TCP',
        addProtocolModal: global.customTcpProtocolModal,
        protocolItemGrid: global.customTcpProtocolItemGrids,
        serviceExtraFieldsHTML: function(project) {
            return `
                <div class="service-field project-pattern" id="pattern-info-${project.id}" title="该信息点击可编辑">
                    <span class="field-label">格式信息</span>
                    <span class="field-value" data-target="pattern-info-${project.id}">${PatternTypeStr[project.pattern_type]}</span>
                </div>
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
                    alert('获取格式信息失败!');
                    return;
                }

                await delay(200);
                hideLoading(loading);

                const modal = document.createElement('div');
                modal.className = 'modal-overlay';
                modal.innerHTML = `
                    <div class="tcp-pattern-change-modal">
                        <div class="modal-header">
                            <h3>TCP格式修改</h3>
                            <button class="close-modal">&times;</button>
                        </div>
                        <div class="modal-body">
                            <div class="form-group pattern-control">
                                ${tcpPatternControlHTML()}
                            </div>
                            <div class="form-actions">
                                <button type="button" class="cancel-btn">取消</button>
                                <button type="button" class="confirm-btn">确定修改</button>
                            </div>
                        </div>
                    </div>
                `;

                document.body.appendChild(modal);

                const patternConfigButton = modal.querySelector('#pattern-infos');
                const patternTypeSelect = modal.querySelector('#pattern-type');
                const patternStatusElement = modal.querySelector('#first-pattern-import-status');
                const currentPatternType = Number(project.pattern_type || PatternType.STANDARD);

                patternConfigButton.dataset.patternInfos = JSON.stringify(patternInfo);
                patternTypeSelect.value = String(currentPatternType);
                patternStatusElement.style.display = 'inline';
                bindTcpPatternTypeControls(patternTypeSelect, patternConfigButton, patternStatusElement, currentPatternType);

                modal.querySelector('.confirm-btn').addEventListener('click', async function(confirmEvent) {
                    confirmEvent.preventDefault();
                    confirmEvent.stopPropagation();

                    const patternType = Number(modal.querySelector('#pattern-type').value || 0);
                    const patternCache = modal.querySelector('#pattern-infos').dataset.patternInfos;

                    if(!confirm('重新配置格式会使所有协议项失效，是否继续')) {
                        return;
                    }

                    const ok = await updateTcpPatternInfoReq(patternType, patternCache);
                    if(!ok) {
                        alert('TCP格式修改失败!');
                        return;
                    }

                    KitProxy.utils.removeDomNode(modal);
                });

                KitProxy.utils.bindModalCloseActions(modal);
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
            const patternType = Number(modal.querySelector('#pattern-type').value);
            const cachedPatternInfos = modal.querySelector('#pattern-infos').dataset.patternInfos;

            if (!cachedPatternInfos) {
                throw new Error('格式类型具体内容未配置，请检查!');
            }

            const parsedPatternInfo = JSON.parse(cachedPatternInfos);
            return {
                pattern_type: patternType,
                pattern_info: {
                    least_byte_len: parsedPatternInfo.least_byte_len,
                    special_fields: parsedPatternInfo.special_fields,
                },
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
    };

    global.ProtocolTypeRegistry = KitProxy.protocolTypes;
})(typeof window !== 'undefined' ? window : globalThis);

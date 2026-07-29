const servicePageState = KitProxy.pagination.createState(10);
const serviceFilterState = KitProxy.serviceFilters
    ? KitProxy.serviceFilters.createState()
    : {
        filters: {
            startDate: '',
            endDate: '',
            startTime: '',
            endTime: '',
            status: 'all',
            protocolType: 'all',
            ownerKeyword: '',
            ownerUserId: null,
            ownerNote: '',
        },
        active: false,
    };
let currentPageProjects = [];

function isProjectActive(project) {
    return Number(project && project.runtime_state) === 1;
}

function isProjectDeleted(project) {
    return Number(project && project.status) === 0;
}

function showErrorPopup(message, options) {
    if (KitProxy.utils && typeof KitProxy.utils.showGlobalError === 'function') {
        KitProxy.utils.showGlobalError(message, options);
    }
}

function getProjectRuntimeStatusText(project) {
    if (isProjectDeleted(project)) return '已删除';
    return isProjectActive(project) ? '开启' : '未开启';
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

function getProjectOwnerNote(project) {
    return String((project && project.user_note) || '');
}

const readDatasetProjectId = typeof globalThis.readDatasetProjectId === 'function'
    ? globalThis.readDatasetProjectId
    : function(value) {
        const directId = Number(value);
        if (Number.isInteger(directId) && directId > 0) return directId;
        return ExtractId(value);
    };
globalThis.readDatasetProjectId = readDatasetProjectId;

function isProtocolInactive(protocol) {
    const status = protocol && protocol.status;
    const value = String(status == null ? '' : status).toLowerCase();
    return status === 2 || value === '2' || value === 'inactive' || value === 'disabled';
}

function getProtocolStatusText(protocol) {
    return isProtocolInactive(protocol) ? '已删除' : '正常';
}

function getProtocolConfigState(protocol) {
    const state = Number(protocol && protocol.config_state);
    return [0, 1, 2].includes(state) ? state : 0;
}

function isProtocolOnline(protocol) {
    return getProtocolConfigState(protocol) === 1;
}

function isProtocolNeedReconfig(protocol) {
    return getProtocolConfigState(protocol) === 2;
}

function getProtocolConfigStateText(protocol) {
    const configState = getProtocolConfigState(protocol);
    if (configState === 1) return '已上线';
    if (configState === 2) return '待重配置';
    return '未上线';
}

/**
 * 同步协议项实时交互入口的可用状态。
 * @param {HTMLElement} protocolItem
 */
function refreshProtocolInteractionEntry(protocolItem) {
    const button = protocolItem && protocolItem.querySelector('.protocol-interaction-btn');
    if (!button) return;

    const projectRuntimeState = Number(protocolItem.dataset.projectRuntimeState) === 1 ? 1 : 0;
    const configState = Number(protocolItem.dataset.configState);
    const deleted = protocolItem.dataset.status === 'inactive';
    const entry = {
        projectRuntimeState,
        configState,
        deleted,
    };
    const eligible = !deleted && projectRuntimeState === 1 && configState === 1;
    let reason = '';
    if (KitProxy.protocolInteractionDrawer && typeof KitProxy.protocolInteractionDrawer.eligibilityReason === 'function') {
        reason = KitProxy.protocolInteractionDrawer.eligibilityReason(entry);
    } else if (deleted) {
        reason = '协议项已删除';
    } else if (projectRuntimeState !== 1) {
        reason = '请先启动测试服务';
    } else if (configState === 2) {
        reason = '协议项待重配置';
    } else if (configState !== 1) {
        reason = '请先上线协议项';
    }
    button.disabled = !eligible;
    const label = eligible ? '查看协议项实时交互详情' : reason;
    button.title = label;
    button.setAttribute('aria-label', label);
}

function bindProtocolInteractionAction(protocolItem, protocol) {
    const button = protocolItem.querySelector('.protocol-interaction-btn');
    if (!button) return;
    button.addEventListener('click', function(event) {
        event.preventDefault();
        event.stopPropagation();
        if (button.disabled || !KitProxy.protocolInteractionDrawer) return;
        KitProxy.protocolInteractionDrawer.open({
            projectId: protocolItem.dataset.projectId,
            protocolId: protocolItem.dataset.protocolId,
            protocolType: protocolItem.dataset.protocolType || protocol.type,
            name: protocol.name,
            projectRuntimeState: protocolItem.dataset.projectRuntimeState,
            configState: protocolItem.dataset.configState,
            deleted: protocolItem.dataset.status === 'inactive',
            triggerButton: button,
        });
    });
    refreshProtocolInteractionEntry(protocolItem);
}

function bindProtocolDeleteAction(protocolItem) {
    const deleteBtn = protocolItem.querySelector('.delete-protocol-btn');
    if (!deleteBtn) return;

    deleteBtn.addEventListener('click', async function(e) {
        e.stopPropagation();
        if (confirm('确定要删除这个协议吗？')) {
            
            const ok = await delProtocol(protocolItem.id);
            if(true === ok) {
                if (KitProxy.protocolItemsPage && typeof KitProxy.protocolItemsPage.handleProtocolDeleted === 'function') {
                    await KitProxy.protocolItemsPage.handleProtocolDeleted(protocolItem);
                } else {
                    protocolItem.remove();
                }

            } else {
                showErrorPopup('删除协议项失败!');
            }
        }
    });
}

async function restoreProtocolItem(id_str) {
    const protocolItemId = ExtractId(id_str);

    return KitProxy.utils.runMutationOnce(
        `restore-protocol-${protocolItemId}`,
        async function() {
        if(!Number.isInteger(protocolItemId) || protocolItemId <= 0) {
            throw new Error("无效的协议项ID");
        }

        await KitProxy.api.restoreProtocol(protocolItemId);
        return true;
        },
        {
            message: '正在恢复协议项...',
            successMessage: '协议项恢复成功',
        },
    ).catch(function(error) {
        console.error('恢复协议项失败:', error);
        return false;
    });
}

function bindProtocolRestoreAction(protocolItem) {
    const restoreBtn = protocolItem.querySelector('.restore-protocol-btn');
    if (!restoreBtn) return;

    restoreBtn.addEventListener('click', async function(e) {
        e.stopPropagation();
        if (!confirm('确定要恢复这个协议项吗？')) return;

        const ok = await restoreProtocolItem(protocolItem.id);
        if (!ok) {
            showErrorPopup('恢复协议项失败!');
            return;
        }

        if (KitProxy.protocolItemsPage && typeof KitProxy.protocolItemsPage.loadProtocolItems === 'function') {
            await KitProxy.protocolItemsPage.loadProtocolItems();
        } else {
            protocolItem.remove();
        }
    });
}

/**
 * 生成协议项表单页 URL，并保留 mock/后端调试参数。
 * @param {number | string} projectId
 * @param {number | string=} protocolId
 * @returns {string}
 */
function buildProtocolItemFormUrl(projectId, protocolId) {
    const params = new URLSearchParams(window.location.search);
    params.set('projectId', String(projectId));
    if (protocolId != null && protocolId !== '') {
        params.set('protocolId', String(protocolId));
    } else {
        params.delete('protocolId');
    }
    return `protocol_item_form.html?${params.toString()}`;
}

/**
 * 绑定协议项详情展开/收起。
 * @param {HTMLElement} protocolItem
 */
function bindProtocolToggle(protocolItem) {
    const toggleBtn = protocolItem.querySelector('.protocol-toggle-btn');
    const details = protocolItem.querySelector('.protocol-details');
    if (!toggleBtn || !details) return;

    toggleBtn.addEventListener('click', function(event) {
        event.preventDefault();
        event.stopPropagation();

        const isExpanded = details.classList.toggle('is-expanded');
        protocolItem.classList.toggle('is-expanded', isExpanded);
        toggleBtn.setAttribute('aria-label', isExpanded ? '收起协议项详情' : '展开协议项详情');
        toggleBtn.setAttribute('aria-expanded', String(isExpanded));
    });
}

/**
 * 生成协议项重配置 URL，并保留 mock/后端调试参数。
 * @param {number | string} projectId
 * @param {number | string} protocolId
 * @returns {string}
 */
function buildProtocolReconfigUrl(projectId, protocolId) {
    const params = new URLSearchParams(window.location.search);
    params.set('projectId', String(projectId));
    params.set('protocolId', String(protocolId));
    params.set('mode', 'reconfig');
    return `protocol_item_form.html?${params.toString()}`;
}

function refreshProtocolRuntimeControl(protocolItem, configState) {
    const state = [0, 1, 2].includes(Number(configState)) ? Number(configState) : 0;
    const button = protocolItem.querySelector('.protocol-runtime-btn');
    if (!button) return;

    protocolItem.dataset.configState = String(state);
    button.dataset.configState = String(state);
    button.textContent = state === 1 ? '已上线' : (state === 2 ? '待重配置' : '未上线');
    button.classList.toggle('is-online', state === 1);
    button.classList.toggle('is-offline', state === 0);
    button.classList.toggle('is-reconfig', state === 2);

    const projectRunning = Number(protocolItem.dataset.projectRuntimeState) === 1;
    const disabled = !projectRunning && state !== 2;
    button.disabled = disabled;
    button.title = disabled
        ? '项目未运行，不能上线或下线'
        : (state === 2 ? '进入重配置页面' : (state === 1 ? '点击下线协议项' : '点击上线协议项'));
    refreshProtocolInteractionEntry(protocolItem);
}

function bindProtocolRuntimeAction(protocolItem) {
    const runtimeButton = protocolItem.querySelector('.protocol-runtime-btn');
    if (!runtimeButton) return;

    runtimeButton.addEventListener('click', async function(event) {
        event.preventDefault();
        event.stopPropagation();

        const protocolId = ExtractId(protocolItem.id);
        const projectId = Number(protocolItem.dataset.projectId || 0);
        const configState = Number(protocolItem.dataset.configState || 0);
        const projectRunning = Number(protocolItem.dataset.projectRuntimeState) === 1;

        if (configState === 2) {
            const targetUrl = buildProtocolReconfigUrl(projectId, protocolId);
            protocolItem.dataset.protocolItemFormUrl = targetUrl;
            const navigateEvent = new CustomEvent('protocol-item:navigate-reconfig', {
                bubbles: true,
                cancelable: true,
                detail: {
                    projectId,
                    protocolId,
                    url: targetUrl,
                },
            });
            protocolItem.dispatchEvent(navigateEvent);
            if (navigateEvent.defaultPrevented) return;
            window.location.href = targetUrl;
            return;
        }

        if (!projectRunning) {
            showErrorPopup('项目未运行，不能上线或下线协议项');
            return;
        }

        const enable = configState === 0;
        runtimeButton.disabled = true;
        runtimeButton.classList.add('is-busy');

        try {
            const result = await KitProxy.api.setProtocolRuntime(protocolId, enable);
            const nextConfigState = result && result.config_state != null ? Number(result.config_state) : (enable ? 1 : 0);
            refreshProtocolRuntimeControl(protocolItem, nextConfigState);
            if (nextConfigState !== 1 && KitProxy.protocolInteractionDrawer
                && typeof KitProxy.protocolInteractionDrawer.cleanupProtocol === 'function') {
                KitProxy.protocolInteractionDrawer.cleanupProtocol(projectId, protocolId);
            }
            if (KitProxy.protocolItemsPage && typeof KitProxy.protocolItemsPage.rememberProtocolRuntimeState === 'function') {
                KitProxy.protocolItemsPage.rememberProtocolRuntimeState(protocolId, nextConfigState);
            }
            if (KitProxy.protocolItemsPage && typeof KitProxy.protocolItemsPage.loadProtocolItems === 'function') {
                await KitProxy.protocolItemsPage.loadProtocolItems();
            }
        } catch (error) {
            showErrorPopup(`${enable ? '上线' : '下线'}协议项失败：${error.message}`);
            refreshProtocolRuntimeControl(protocolItem, configState);
        } finally {
            runtimeButton.classList.remove('is-busy');
        }
    });
}

// 页面展示添加协议项
function addProtocolItem(serviceCard, protocol, pos = -1) {

    const protocolList = serviceCard.querySelector('.protocol-list');
    if (!protocolList) return null;

    const protocolItem = document.createElement('div');
    const protocolInactive = isProtocolInactive(protocol);
    const configState = getProtocolConfigState(protocol);
    const projectId = ExtractId(serviceCard.id);
    const projectRuntimeState = Number(serviceCard.dataset.runtimeState != null ? serviceCard.dataset.runtimeState : serviceCard.dataset.active) === 1 ? 1 : 0;
    protocolItem.className = `protocol-item ${protocol.type.toLowerCase()}${protocolInactive ? ' is-inactive' : ''}`;
    protocolItem.id = `protocol-item-${protocol.id}`;
    protocolItem.dataset.projectId = String(projectId);
    protocolItem.dataset.protocolId = String(protocol.id);
    protocolItem.dataset.configState = String(configState);
    protocolItem.dataset.projectRuntimeState = String(projectRuntimeState);
    protocolItem.dataset.protocolType = protocol.type;
    protocolItem.dataset.status = protocolInactive ? 'inactive' : 'active';

    // 注意: 这里只需改变卡片内部子项的呈现，不需要更改整个布局
    const escape = KitProxy.utils.escapeHTML;
    const isAdmin = Boolean(KitProxy.auth && KitProxy.auth.isCurrentUserAdmin());
    const protocolIdHTML = isAdmin
        ? `<span class="service-sub-pill protocol-id">
                <span class="meta-label">协议ID</span>
                <span class="meta-value">${escape(protocol.id)}</span>
            </span>`
        : '';
    const createTime = KitProxy.utils.formatUtcTime(protocol.ctime);
    const interactionButtonHTML = serviceCard.classList.contains('protocol-items-page')
        ? `<button type="button" class="protocol-interaction-btn" data-testid="protocol-interaction-open" title="查看协议项实时交互详情" aria-label="查看协议项实时交互详情">
                    <span class="protocol-interaction-icon" aria-hidden="true"></span>
                </button>`
        : '';
    const actionHTML = protocolInactive
        ? `<button type="button" class="restore-protocol-btn">恢复协议项</button>${interactionButtonHTML}`
        : `<button type="button" class="protocol-runtime-btn is-${configState === 1 ? 'online' : (configState === 2 ? 'reconfig' : 'offline')}" data-config-state="${escape(configState)}">${escape(getProtocolConfigStateText(protocol))}</button>
                ${interactionButtonHTML}
                <button type="button" class="protocol-toggle-btn" aria-label="展开协议项详情" aria-expanded="false">
                    <span class="protocol-toggle-icon" aria-hidden="true"></span>
                </button>
                <button type="button" class="delete-protocol-btn">删除协议</button>
                `;
    protocolItem.innerHTML =`
        <div class="protocol-header">
            <div class="protocol-primary-row">
                <span class="protocol-tag ${escape(protocol.type.toLowerCase())}">${escape(protocol.type)}</span>
                <span class="protocol-name">${escape(protocol.name)}</span>
                ${protocolInactive ? `<span class="protocol-status-badge is-inactive">${escape(getProtocolStatusText(protocol))}</span>` : ''}
                <div class="protocol-time">
                    ${protocolIdHTML}
                    <span class="create-time">创建: ${escape(createTime)}</span>
                </div>
            </div>
            <div class="protocol-header-actions">
                ${actionHTML}
            </div>
        </div>
        <div class="protocol-details">
            <div class="details-grid"> </div>
        </div>
`;

    // 根据协议类型注册表生成不同协议项的详情网格。
    const oldDetailsGrid = protocolItem.querySelector('.details-grid');
    oldDetailsGrid.replaceWith(ProtocolTypeRegistry.createProtocolItemGrid(protocol));

    bindProtocolToggle(protocolItem);
    if (protocolInactive) {
        bindProtocolRestoreAction(protocolItem);
    } else {
        bindProtocolDeleteAction(protocolItem);
        bindProtocolRuntimeAction(protocolItem);
        refreshProtocolRuntimeControl(protocolItem, configState);
    }
    bindProtocolInteractionAction(protocolItem, protocol);

    // 将协议项卡片插入到列表中
    if(-1 === pos) {
        protocolList.appendChild(protocolItem);
    } else {
        protocolList.insertBefore(protocolItem, protocolList.children[pos] || null);
    }

    // (暂不实现)更新协议数量统计
    // const protocolCount = protocolList.querySelectorAll('.protocol-item').length;
    // serviceCard.querySelector('.project-protocol-cnt').textContent = protocolCount;

    return protocolItem;

}


// 获取协议项列表
async function getProtocolList(project_id, offset = 0, limit = 10, options = {}) {

    try {
        const protocols = await KitProxy.api.getProtocolList(project_id, offset, limit, options);
        console.log('获取协议项列表请求成功:', protocols);
        return protocols;
    } catch (error) {
        console.error('获取协议项列表请求出错:', error);
        throw error;
    }
}


// 获取单个协议项请求
async function getProtocolReq(protocol_id) {
    try {
        const protocols = await KitProxy.api.getProtocol(protocol_id);

        if(!Array.isArray(protocols) || protocols.length <= 0) {
            throw new Error('获取单个协议项请求为null');
        }
 
        console.log('获取单个协议项请求成功:', protocols);

        return protocols;
    } catch (error) {
        console.error('获取单个协议项请求失败:', error);
        throw error;
    }
}

// 表单body数据辅助组装
function formBodyDataHeler(formData, body, body_type, body_data) {

    if(body_data.length <= 0)
        return;

    const body_key = body === 1 ? 'protocol_req_body' : 'protocol_resp_body';
    let root;
    let body_value = '';
    
    console.info('body_data: ', body_data);

    if(body_type.includes('json') && body_data[0] === '{') {
        root = JSON.parse(body_data || '');
        body_value = JSON.stringify(root);
    } else if(body_type.includes('xml')) {
        body_value = new XMLSerializer(body_data);
    } else if(body_type.includes('binary')) {
        body_value = new Blob([body_data])
    }

    console.info('bady_value: ', body_value);

    formData.append(body_key, body_value);
}


async function  addHTTPProtocolReq(protocol) {

    try {
        const addResult = await KitProxy.api.addProtocol(protocol);
        console.log('协议项添加请求成功:', addResult);
        return addResult;
    } catch(error) {
        console.error('添加协议项请求失败:', error);

        throw error;
    }

}


// 添加HTTP协议项
async function  addHTTPProtocol(serviceCard, submit_protocol) {

    const projectId = submit_protocol && submit_protocol.cfg_header
        ? submit_protocol.cfg_header.project_id
        : ExtractId(serviceCard.id);

    return KitProxy.utils.runMutationOnce(
        `add-protocol-${projectId}`,
        async function() {
        let protocols;

        // 1. 先添加服务
        const addResult = await addHTTPProtocolReq(submit_protocol);
        console.info('添加完成的单个协议项id:', addResult.protocol_id);
        
        // 2. 再获取单个项
        protocols = await getProtocolReq(addResult.protocol_id);
        console.info('获取的单个协议项:', protocols);

        // 3. 添加到页面显示；协议项独立页需要回到第一页并刷新当前列表。
        if (KitProxy.protocolItemsPage && typeof KitProxy.protocolItemsPage.handleProtocolAdded === 'function') {
            await KitProxy.protocolItemsPage.handleProtocolAdded(protocols[0]);
        } else {
            addProtocolItem(serviceCard, protocols[0], 1);
        }

        return protocols[0];
        },
        {
            message: '正在添加协议项...',
            successMessage: '协议项添加成功',
        },
    ).catch(function(error) {
        console.error('添加测试协议项失败: ', error);
        showErrorPopup('添加测试协议项失败!');
        return null;
    });
}

function normalizeLengthPolicy(value, fallback = LengthPolicy.BODY_LENGTH) {
    if (value === LengthPolicy.BODY_LENGTH || value === LengthPolicy.TOTAL_LENGTH || value === LengthPolicy.NO_LENGTH) {
        return value;
    }

    const legacyType = Number(value);
    if (legacyType === PatternType.BODY_LENGTH_DEP) return LengthPolicy.BODY_LENGTH;
    if (legacyType === PatternType.TOTAL_LENGTH_DEP) return LengthPolicy.TOTAL_LENGTH;
    if (legacyType === PatternType.NO_LENGTH_DEP) return LengthPolicy.NO_LENGTH;
    return fallback;
}

function tcpLengthPolicyText(policy) {
    return LengthPolicyStr[normalizeLengthPolicy(policy, '')] || '未知格式';
}

function getDefaultTcpPatternInfo(lengthPolicy = LengthPolicy.BODY_LENGTH) {
    const policy = normalizeLengthPolicy(lengthPolicy);

    if (policy === LengthPolicy.NO_LENGTH) {
        return {
            version: 2,
            header_bytes: 24,
            byte_order: 'raw',
            length_policy: policy,
            fields: [
                { name: '起始字符', byte_pos: 0, byte_len: 2, type: 'STR', role: 'start_magic', match: 'H023A' },
                { name: '功能码', byte_pos: 2, byte_len: 2, type: 'STR', role: 'function_code' },
                { name: '分隔符', byte_pos: 4, byte_len: 1, type: 'STR', role: 'common' },
                { name: '设备类型', byte_pos: 5, byte_len: 2, type: 'STR', role: 'common' },
                { name: '分隔符', byte_pos: 7, byte_len: 1, type: 'STR', role: 'common' },
                { name: '站号', byte_pos: 8, byte_len: 2, type: 'STR', role: 'common' },
                { name: '分隔符', byte_pos: 10, byte_len: 1, type: 'STR', role: 'common' },
                { name: '序号', byte_pos: 11, byte_len: 10, type: 'STR', role: 'common' },
                { name: '分隔符', byte_pos: 21, byte_len: 1, type: 'STR', role: 'common' },
                { name: '结束符', byte_pos: 22, byte_len: 2, type: 'STR', role: 'common' },
            ],
        };
    }

    const lengthFieldKey = policy === LengthPolicy.TOTAL_LENGTH ? 'total_length_field' : 'body_length_field';
    const lengthFieldName = policy === LengthPolicy.TOTAL_LENGTH ? '报文总长度' : '报文体长度';
    const lengthFieldPos = policy === LengthPolicy.TOTAL_LENGTH ? 4 : 14;

    // 新建 TCP 格式时的兜底字段，使用 V2 length_policy 作为唯一格式来源。
    return {
        version: 2,
        header_bytes: 26,
        byte_order: 'big',
        length_policy: policy,
        fields: policy === LengthPolicy.TOTAL_LENGTH
            ? [
                { name: '起始标识', byte_pos: 0, byte_len: 4, type: 'UINT32', role: 'start_magic', match: 'H23232323' },
                { name: lengthFieldName, byte_pos: lengthFieldPos, byte_len: 4, type: 'UINT32', role: 'total_length' },
                { name: '消息序列号', byte_pos: 8, byte_len: 4, type: 'UINT32', role: 'common' },
                { name: '功能码', byte_pos: 12, byte_len: 2, type: 'UINT16', role: 'function_code' },
                { name: '报文体长度', byte_pos: 14, byte_len: 4, type: 'UINT32', role: 'common' },
                { name: '消息时间戳', byte_pos: 18, byte_len: 8, type: 'UINT64', role: 'common' },
            ]
            : [
                { name: '起始标识', byte_pos: 0, byte_len: 4, type: 'UINT32', role: 'start_magic', match: 'H23232323' },
                { name: '消息总长度', byte_pos: 4, byte_len: 4, type: 'UINT32', role: 'common' },
                { name: '消息序列号', byte_pos: 8, byte_len: 4, type: 'UINT32', role: 'common' },
                { name: '功能码', byte_pos: 12, byte_len: 2, type: 'UINT16', role: 'function_code' },
                { name: lengthFieldName, byte_pos: lengthFieldPos, byte_len: 4, type: 'UINT32', role: 'body_length' },
                { name: '消息时间戳', byte_pos: 18, byte_len: 8, type: 'UINT64', role: 'common' },
            ],
    };
}

function fieldRoleFromLegacySpecialKey(key) {
    if (key === 'start_magic_num_field' || key === 'start_magic_field') return 'start_magic';
    if (key === 'function_code_field') return 'function_code';
    if (key === 'body_length_field') return 'body_length';
    if (key === 'total_length_field') return 'total_length';
    return 'common';
}

function legacySpecialKeyFromFieldRole(role) {
    if (role === 'start_magic') return 'start_magic_num_field';
    if (role === 'function_code') return 'function_code_field';
    if (role === 'body_length') return 'body_length_field';
    if (role === 'total_length') return 'total_length_field';
    return '';
}

function fieldEnd(field) {
    return Number(field.byte_pos || 0) + Number(field.byte_len || 0);
}

function normalizeEditorField(field, fallback = {}) {
    return {
        name: String(field && field.name != null ? field.name : fallback.name || ''),
        idx: Number.isFinite(Number(field && field.idx)) ? Number(field.idx) : fallback.idx,
        byte_pos: Number(field && field.byte_pos),
        byte_len: Number(field && field.byte_len),
        type: String(field && field.type != null ? field.type : fallback.type || 'STR'),
        value: String(field && field.value != null ? field.value : fallback.value || ''),
    };
}

function patternInfoV2ToEditorInfo(patternInfo) {
    if (KitProxy.tcpPatternEditor && typeof KitProxy.tcpPatternEditor.normalizePatternInfo === 'function') {
        return KitProxy.tcpPatternEditor.normalizePatternInfo(patternInfo);
    }

    const editorInfo = {
        length_policy: normalizeLengthPolicy(patternInfo.length_policy),
        default_order: patternInfo.default_order || 'big',
        least_byte_len: Number(patternInfo.header_bytes || 0),
        special_fields: {},
        common_fields: [],
    };

    (patternInfo.fields || []).forEach((field, index) => {
        const role = field.role || 'common';
        const editorField = normalizeEditorField({
            name: field.name,
            idx: index,
            byte_pos: field.byte_pos,
            byte_len: field.byte_len,
            type: field.type,
            value: field.match || '',
        }, { idx: index });
        const specialKey = legacySpecialKeyFromFieldRole(role);

        if (specialKey) {
            editorInfo.special_fields[specialKey] = editorField;
        } else {
            editorInfo.common_fields.push(editorField);
        }
    });

    return editorInfo;
}

function patternInfoToEditorInfo(patternInfo) {
    if (KitProxy.tcpPatternEditor && typeof KitProxy.tcpPatternEditor.normalizePatternInfo === 'function') {
        return KitProxy.tcpPatternEditor.normalizePatternInfo(patternInfo || getDefaultTcpPatternInfo());
    }

    if (patternInfo && Number(patternInfo.version) === 2 && Array.isArray(patternInfo.fields)) {
        return patternInfoV2ToEditorInfo(patternInfo);
    }
    return patternInfo || getDefaultTcpPatternInfo();
}

function addGapFields(fields, headerBytes) {
    const sorted = fields
        .filter(field => Number.isFinite(field.byte_pos) && Number.isFinite(field.byte_len) && field.byte_len > 0)
        .sort((left, right) => left.byte_pos - right.byte_pos);
    const result = [];
    let cursor = 0;

    sorted.forEach(field => {
        if (field.byte_pos > cursor) {
            result.push({
                name: `保留字段${cursor}`,
                byte_pos: cursor,
                byte_len: field.byte_pos - cursor,
                type: 'STR',
                role: 'common',
            });
        }
        result.push(field);
        cursor = Math.max(cursor, fieldEnd(field));
    });

    if (headerBytes > cursor) {
        result.push({
            name: `保留字段${cursor}`,
            byte_pos: cursor,
            byte_len: headerBytes - cursor,
            type: 'STR',
            role: 'common',
        });
    }

    return result;
}

function buildV2TcpPatternInfoFromEditor(editorInfo, lengthPolicy) {
    if (KitProxy.tcpPatternEditor && typeof KitProxy.tcpPatternEditor.toPatternInfoV2 === 'function') {
        const source = Object.assign({}, editorInfo || {});
        if (lengthPolicy) {
            source.length_policy = normalizeLengthPolicy(lengthPolicy);
        }
        return KitProxy.tcpPatternEditor.toPatternInfoV2(source);
    }

    const policy = normalizeLengthPolicy(lengthPolicy || editorInfo.length_policy);
    const fields = [];
    const specialFields = editorInfo.special_fields || {};
    const defaultOrder = editorInfo.default_order || (policy === LengthPolicy.NO_LENGTH ? 'raw' : 'big');

    Object.keys(specialFields).forEach(key => {
        const role = fieldRoleFromLegacySpecialKey(key);
        if ((role === 'body_length' && policy !== LengthPolicy.BODY_LENGTH)
            || (role === 'total_length' && policy !== LengthPolicy.TOTAL_LENGTH)) {
            return;
        }

        const source = normalizeEditorField(specialFields[key]);
        const field = {
            name: source.name,
            byte_pos: source.byte_pos,
            byte_len: source.byte_len,
            type: source.type,
            role,
        };
        if (role === 'start_magic') {
            field.match = source.value;
        }
        fields.push(field);
    });

    (editorInfo.common_fields || []).forEach(fieldInfo => {
        const source = normalizeEditorField(fieldInfo);
        fields.push({
            name: source.name,
            byte_pos: source.byte_pos,
            byte_len: source.byte_len,
            type: source.type,
            role: 'common',
        });
    });

    const maxFieldEnd = fields.reduce((max, field) => Math.max(max, fieldEnd(field)), 0);
    const headerBytes = Math.max(Number(editorInfo.least_byte_len || 0), maxFieldEnd);

    return {
        version: 2,
        header_bytes: headerBytes,
        default_order: defaultOrder,
        length_policy: policy,
        fields: addGapFields(fields, headerBytes),
    };
}

function buildTcpPatternModalInput(patternInfoText) {
    const patternInfo = patternInfoText ? patternInfoToEditorInfo(JSON.parse(patternInfoText)) : getDefaultTcpPatternInfo();
    return patternInfo;
}

function openTcpPatternConfig(targetField, statusElement) {
    const lengthPolicy = normalizeLengthPolicy(targetField.dataset.lengthPolicy);
    const patternInfoText = targetField.dataset.patternInfos || JSON.stringify(getDefaultTcpPatternInfo(lengthPolicy));
    const inputPatternInfosMap = buildTcpPatternModalInput(patternInfoText);

    createCustomTcpPatternModal(targetField, '头部特殊字段', inputPatternInfosMap, statusElement, true);
}

function bindTcpPatternTypeControls(selectElement, configButton, statusElement, initialPolicy = LengthPolicy.BODY_LENGTH) {
    // V2 长度策略在 config-pattern-modal 内维护；这个函数保留给旧调用点绑定“配置”按钮。
    let previousLengthPolicy = normalizeLengthPolicy(initialPolicy);
    if (selectElement) {
        selectElement.value = previousLengthPolicy;
    }
    configButton.dataset.lengthPolicy = previousLengthPolicy;

    selectElement?.addEventListener('change', function() {
        const policy = normalizeLengthPolicy(this.value, '');
        const targetField = configButton;

        if (previousLengthPolicy !== policy && targetField.dataset.patternInfos) {
            if(!confirm('格式已配置, 切换会清空，是否继续?')) {
                this.value = previousLengthPolicy;
                return;
            }

            delete targetField.dataset.patternInfos;
            if (statusElement) {
                statusElement.style.display = 'none';
            }
        }

        previousLengthPolicy = policy;
        configButton.dataset.lengthPolicy = policy;
        configButton.disabled = !policy;
    });

    configButton.disabled = false;
    configButton.addEventListener('click', function() {
        openTcpPatternConfig(configButton, statusElement);
    });
}

function tcpPatternControlHTML() {
    return `
        <div class="pattern-header">
            <label for="pattern-infos">TCP格式</label>
            <scan class="import-status" id="first-pattern-import-status" style="display: none">格式已设置</scan>
        </div>
        <div class="pattern-container">
            <button type="button" class="pattern-config-btn" id="pattern-infos">配置</button>
        </div>
    `;
}

// 添加测试服务模态框
function addNewServiceCardModalHTML() {
    return`<div class="add-service-modal">
            <div class="modal-header">
                <h3>添加新测试服务</h3>
                <button class="close-modal">&times;</button>
            </div>
            <div class="modal-body">
                <form id="add-service-form">
                    <div class="form-columns-container">
                        <div class="form-group">
                            <label for="service-name">服务名称</label>
                            <input type="text" id="service-name" placeholder="输入服务名称" required>
                        </div>

                        <div class="form-group">
                            <label for="protocol-type">协议类型</label>
                            <select id="protocol-type" required>
                                <option value="">请选择协议类型</option>
                                <option value=1>HTTP</option>
                                <option value=2>TCP</option>
                                <!-- HTTPS 测试服务暂未支持，添加入口先隐藏。 -->
                                <!-- <option value=3>HTTPS</option> -->
                            </select>
                        </div>

                        <div class="form-group">
                            <label for="service-mode">测试模式</label>
                            <select id="service-mode" required>
                                <option value="">请选择测试模式</option>
                                <option value=1>服务器模式</option>
                                <!-- <option value=2>客户端模式</option> -->
                            </select>
                        </div>
                    </div>

                    <div class="form-actions">
                        <button type="button" class="cancel-btn">取消</button>
                        <button type="submit" class="confirm-btn">确认添加</button>
                    </div>
                </form>
            </div>
        </div>
    `;
}

function renderServiceEndpointControl(formContainer, mode) {
    const existingPortControl = formContainer.querySelector('.port-control');
    if (existingPortControl) {
        formContainer.removeChild(existingPortControl);
    }

    if (mode === ProjectMode.SERVER) {
        const portControl = document.createElement('div');
        portControl.className = 'form-group port-control';
        portControl.innerHTML = `
            <label for="service-port">监听端口</label>
            <input type="number" id="service-port" value="0" disabled>
            <div class="path-hint">新增后默认未开启，启动时由后端自动分配监听端口</div>
        `;
        formContainer.appendChild(portControl);
    } else if (mode === ProjectMode.CLIENT) {
        const portControl = document.createElement('div');
        portControl.className = 'form-group port-control';
        portControl.innerHTML = `
            <label for="target-address">目标IP/端口</label>
            <input type="text" id="target-address" 
                placeholder="IP:端口" 
                pattern="^((25[0-5]|2[0-4]\\d|[01]?\\d\\d?)\\.){3}(25[0-5]|2[0-4]\\d|[01]?\\d\\d?):([1-9]\\d{0,3}|[1-5]\\d{4}|6[0-4]\\d{3}|65[0-4]\\d{2}|655[0-2]\\d|6553[0-5])$"
                title="请输入有效的IP地址和端口号，格式如：192.168.1.1:8080"
                required>
            <div class="error-message" style="color:red;font-size:12px;display:none"></div>
        `;
        formContainer.appendChild(portControl);

        const addressInput = portControl.querySelector('#target-address');
        const errorMessage = portControl.querySelector('.error-message');
        
        addressInput.addEventListener('input', function() {
            if (!KitProxy.utils.validateIpPort(this.value)) {
                errorMessage.textContent = '请输入有效的IP:端口格式，如：192.168.1.1:8080';
                errorMessage.style.display = 'block';
                this.setCustomValidity('IP地址或端口号格式不正确');
            } else {
                errorMessage.style.display = 'none';
                this.setCustomValidity('');
            }
        });
    }
}

function renderProjectPatternControl(formContainer, protocolType) {
    ProtocolTypeRegistry.renderAddServiceExtraControl(formContainer, protocolType);
}

function bindAddServiceDynamicControls(modal) {
    const formContainer = modal.querySelector('.form-columns-container');
    const serviceModeSelect = modal.querySelector('#service-mode');
    const protocolTypeSelect = modal.querySelector('#protocol-type');

    if (serviceModeSelect && formContainer) {
        serviceModeSelect.addEventListener('change', function() {
            renderServiceEndpointControl(formContainer, Number(this.value));
        });
    }

    if (protocolTypeSelect && formContainer) {
        protocolTypeSelect.addEventListener('change', function() {
            const type = Number(this.value);
            renderProjectPatternControl(formContainer, type);
        });
    }
}

function collectAddServicePayload(modal) {
    const serviceName = modal.querySelector('#service-name').value;
    const protocolType = Number(modal.querySelector('#protocol-type').value);
    const serviceMode = Number(modal.querySelector('#service-mode').value);
    let servicePort = 0;
    let targetIp = '';

    if(!serviceName.trim()) {
        throw new Error('服务名称不能为空');
    }

    if(!protocolType) {
        throw new Error('请选择协议类型');
    }

    if(!serviceMode) {
        throw new Error('请选择测试模式');
    }

    if (serviceMode === ProjectMode.SERVER) {
        servicePort = 0;
    } else if (serviceMode === ProjectMode.CLIENT) {
        const addressInput = modal.querySelector('#target-address');

        if (!addressInput || !addressInput.reportValidity()) {
            throw new Error('请输入有效的IP:端口格式，如：192.168.1.1:8080');
        }
        if(!KitProxy.utils.validateIpPort(addressInput.value)) {
            throw new Error('请输入有效的IP:端口格式，如：192.168.1.1:8080');
        }
        targetIp = addressInput.value;
    }

    const extraPayload = ProtocolTypeRegistry.collectAddServiceExtraPayload(modal, protocolType);

    return {
        name: serviceName,
        mode: serviceMode,
        protocol_type: protocolType,
        listen_port: servicePort,
        target_ip: targetIp,
        pattern_info: extraPayload.pattern_info,
    };
}



async function updateTcpPatternInfoReq(projectId, patternInfo) {
    try {
        await KitProxy.api.updateProjectPatternInfo(projectId, patternInfo);
        return true;
    } catch (error) {
        console.error('修改TCP格式信息请求失败:', error.message);
        return false;
    }
}

async function getTcpPatternInfoReq(project_id) {
    
    try {
        const patternInfo = await KitProxy.api.getProjectPatternInfo(project_id);
        return patternInfo;

    }catch(error) {
        console.error('获取TCP格式信息请求失败:', error);
        throw error;
    }

}

async function updateProjectNameReq(project_id, name) {

    try {
        await KitProxy.api.updateProjectName(project_id, name);

    } catch (error) {
        console.error('修改测试服务标题请求失败:', error.message);
        throw error;
    }

}

async function updateProjectName(id_str, tilte_name) {
    const serviceCardId = ExtractId(id_str)

    const serviceCard = document.getElementById(id_str);

    return KitProxy.utils.runMutationOnce(
        `update-project-name-${serviceCardId}`,
        async function() {
        let projects;

        if(!Number.isInteger(serviceCardId) || serviceCardId <= 0) {
            throw new Error("无效的测试服务ID");
        }

        await updateProjectNameReq(serviceCardId, tilte_name);

        projects = await getProjectReq(serviceCardId);

        // 动态更新页面数据
        updateServiceCard(id_str, projects[0]) 

        return true;
        },
        {
            message: '正在保存测试服务名称...',
            successMessage: '测试服务名称保存成功',
        },
    ).catch(function(error) {
        console.error('修改测试服务标题失败:', error);
        return false;
    });
}

async function setProjectActiveReq(projectId, active) {
    try {
        return await KitProxy.api.setProjectRuntimeState(projectId, active);
    } catch (error) {
        console.error(active ? '启动测试服务请求失败:' : '停止测试服务请求失败:', error.message);
        throw error;
    }
}

function mergeProjectRuntimeState(projectId, runtimeData, active) {
    const project = currentPageProjects.find(item => Number(item.id) === Number(projectId));
    if (!project) return null;

    project.runtime_state = runtimeData && runtimeData.runtime_state != null
        ? Number(runtimeData.runtime_state)
        : (active ? 1 : 0);
    project.active = project.runtime_state;
    if (runtimeData && Object.prototype.hasOwnProperty.call(runtimeData, 'listen_port')) {
        project.listen_port = runtimeData.listen_port;
    } else if (!active && Number(project.mode) === ProjectMode.SERVER) {
        project.listen_port = 0;
    }

    return project;
}

async function setProjectActive(projectId, active) {
    return KitProxy.utils.runMutationOnce(
        `set-project-active-${projectId}`,
        async function() {
            const runtimeData = await setProjectActiveReq(projectId, active);
            return mergeProjectRuntimeState(projectId, runtimeData || {}, active) || Object.assign({}, runtimeData || {}, {
                id: projectId,
                runtime_state: active ? 1 : 0,
                active: active ? 1 : 0,
            });
        },
        {
            message: active ? '正在启动测试服务...' : '正在停止测试服务...',
            successMessage: active ? '测试服务启动成功' : '测试服务停止成功',
        },
    );
}

// 添加测试服务 http请求
async function addProjectReq(project) {
    
    try {

        const addResult = await KitProxy.api.addProject(project);
        return addResult;

    } catch (error) {
        console.error('添加服务请求出错:', error.message);
        throw error;
    }
}

// 获取项目列表；Project 列表筛选由后端执行，页面只透传已校验的查询字段。
async function getProjectList(offset, limit, options = {}) {
    try {
        const projects = await KitProxy.api.getProjectList(offset, limit, options);
        return projects;
    } catch (error) {
        console.error('获取项目列表出错:', error);
        throw error;
    }
}

// 获取单个测试服务 http请求
async function getProjectReq(project_id) {

    try {
        const projects = await KitProxy.api.getProject(project_id);

        if(!Array.isArray(projects) || projects.length <= 0) {
            throw new Error('获取单个测试服务请求为null');
        }
        return projects;
    } catch (error) {
        console.error('获取单个测试服务请求失败:', error);
        throw error;
    }
}

// 删除单个测试服务 http请求
async function delProtocolReq(protocol_id, project_id) {

    try {
        await KitProxy.api.deleteProtocol(protocol_id, project_id);
        console.log('删除协议项成功');

    } catch (error) {
        console.error('删除协议项请求出错:', error);
        throw error;
    }
}

async function delProtocol(id_str) {
    // 获取当前测试服务卡片ID
    const protocolItemId = ExtractId(id_str);
    const protocolItem = document.getElementById(id_str);
    const serviceCardId = readDatasetProjectId(protocolItem.dataset.projectId);

    return KitProxy.utils.runMutationOnce(
        `delete-protocol-${protocolItemId}`,
        async function() {

        if(!Number.isInteger(protocolItemId) || protocolItemId <= 0) {
            throw new Error("无效的协议项ID");
        }

        await delProtocolReq(protocolItemId, serviceCardId);

        return true;
        },
        {
            message: '正在删除协议项...',
            successMessage: '协议项删除成功',
        },
    ).catch(function(error) {

        console.error('删除协议项失败:', error);

        return false;
    });
}



// 删除单个测试服务 http请求
async function delProjectReq(project_id) {

    try {
        await KitProxy.api.deleteProject(project_id);

        return true;
    } catch (error) {
        console.error('删除测试服务请求出错:', error);
        throw error;
    }
}

async function delProject(id_str) {
    // 获取当前测试服务卡片ID
    const serviceCardId = ExtractId(id_str);

    const serviceCard = document.getElementById(id_str);

    return KitProxy.utils.runMutationOnce(
        `delete-project-${serviceCardId}`,
        async function() {
        if(!Number.isInteger(serviceCardId) || serviceCardId <= 0) {
            throw new Error("无效的测试服务ID");
        }
        
        await delProjectReq(serviceCardId);

        return true;
        },
        {
            message: '正在删除测试服务...',
            successMessage: '测试服务删除成功',
        },
    ).catch(function(error) {
        console.error('删除测试服务失败:', error);
        return false;
    });
}

async function restoreProject(id_str) {
    const serviceCardId = ExtractId(id_str);

    return KitProxy.utils.runMutationOnce(
        `restore-project-${serviceCardId}`,
        async function() {
        if(!Number.isInteger(serviceCardId) || serviceCardId <= 0) {
            throw new Error("无效的测试服务ID");
        }

        await KitProxy.api.restoreProject(serviceCardId);
        return true;
        },
        {
            message: '正在恢复测试服务...',
            successMessage: '测试服务恢复成功',
        },
    ).catch(function(error) {
        console.error('恢复测试服务失败:', error);
        return false;
    });
}

async function addProject(project) {

    return KitProxy.utils.runMutationOnce(
        'add-project',
        async function() {
        // 1. 先添加服务
        const addResult = await addProjectReq(project);

        // 2. 再获取单个项
        project = await getProjectReq(addResult.project_id);

        await loadAllProjects(1);
        return project[0];
        },
        {
            message: '正在添加测试服务...',
            successMessage: '测试服务添加成功',
        },
    ).catch(function(error) {
        console.error('添加测试服务失败: ', error);
        showErrorPopup('添加测试服务失败!');
        return null;
    });
    
}


// 组装卡片页面
/**
 * @param {any} project
 * @returns {string}
 */
function serviceCardHTML(project) {
    project = project || {};
    const id = project.id;
    const name = project.name;
    const protocol = project.protocol_type;
    const mode = Number(project.mode);
    const active = isProjectActive(project);
    const escape = KitProxy.utils.escapeHTML;
    const displayName = name || `默认测试服务${id}`;
    const endpointLabel = mode === ProjectMode.SERVER ? '监听端口' : '目标IP/端口';
    const endpointValue = getProjectEndpointDisplay(project);
    const protocolText = ProtocolTypeStr[protocol] || '未知协议';
    const modeText = ProjectModeStr[mode] || '未知模式';
    const statusText = getProjectRuntimeStatusText(project);
    const createTime = KitProxy.utils.formatUtcTime(project.ctime);
    const isAdmin = Boolean(KitProxy.auth && KitProxy.auth.isCurrentUserAdmin());
    const projectIdHTML = isAdmin
        ? `<span class="service-sub-pill project-id">
                <span class="meta-label">服务ID</span>
                <span class="meta-value">${escape(id)}</span>
            </span>`
        : '';
    const ownerNote = getProjectOwnerNote(project);
    const ownerHTML = ownerNote && isAdmin
        ? `<span class="service-sub-pill project-owner-note">
                <span class="meta-label">所有者</span>
                <span class="meta-value field-value">${escape(ownerNote)}</span>
            </span>`
        : '';
    const deleted = isProjectDeleted(project);
    const statusControlHTML = deleted
        ? `<span class="service-meta-item service-detail-chip project-status project-deleted" aria-label="测试服务已删除">
                <span class="meta-label">状态</span>
                <span class="meta-value field-value status status-deleted">${escape(statusText)}</span>
            </span>`
        : `<button type="button" class="service-meta-item service-detail-chip project-status service-active-toggle" data-next-active="${active ? '0' : '1'}" aria-label="${active ? '停止测试服务' : '启动测试服务'}">
                <span class="meta-label">状态</span>
                <span class="meta-value field-value status ${active ? 'status-active' : 'status-inactive'}">${escape(statusText)}</span>
            </button>`;
    const actionsHTML = deleted
        ? '<button type="button" class="restore-service-btn">恢复</button>'
        : '<button type="button" class="view-protocols-btn">查看协议项</button><button type="button" class="delete-service-btn">删除</button>';

    return `
        <div class="service-list-row">
            <div class="service-main-cell">
                <div class="service-primary-row">
                    <h3 class="service-title ${deleted ? '' : 'editable'}" data-default="Undef默认测试服务">${escape(displayName)}</h3>
                    <div class="service-sub-meta">
                        ${projectIdHTML}
                        <span class="service-sub-pill project-create-time">
                            <span class="meta-label">创建时间</span>
                            <span class="meta-value field-value">${escape(createTime)}</span>
                        </span>
                        ${ownerHTML}
                    </div>
                </div>
                <div class="service-meta-strip service-info-strip">
                    <span class="service-meta-item service-detail-chip project-protocol-type" data-protocol_type="${escape(protocol)}">
                        <span class="meta-label">协议</span>
                        <span class="meta-value field-value">${escape(protocolText)}</span>
                    </span>
                    <span class="service-meta-item service-detail-chip project-mode">
                        <span class="meta-label">模式</span>
                        <span class="meta-value field-value">${escape(modeText)}</span>
                    </span>
                    <span class="service-meta-item service-detail-chip project-${mode === ProjectMode.SERVER ? 'listen-port' : 'target-ip'}">
                        <span class="meta-label">${escape(endpointLabel)}</span>
                        <span class="meta-value field-value">${escape(endpointValue)}</span>
                    </span>
                    ${statusControlHTML}
                    ${deleted ? '' : ProtocolTypeRegistry.serviceExtraFieldsHTML(project)}
                </div>
            </div>
            <div class="service-action-cell service-actions-container">
                ${actionsHTML}
            </div>
        </div>
    `;
}



// 动态更新页面数据
function updateServiceCard(id_str, project) {
    const serviceCard = document.getElementById(id_str);
    if (!serviceCard) return;

    const title = serviceCard.querySelector(".service-title");
    if (title) {
        title.textContent = project.name || '';
        title.dataset.titleValue = project.name || '';
    }

    // TODO: 待考虑 是否能修改
    // serviceCard.querySelector(".project-protocol-type .field-value").textContent = protocol_str;
    // serviceCard.querySelector(".project-mode .field-value").textContent = mode_str;


    // if(1 === project.mode) {
    //     serviceCard.querySelector(".project-listen-port .field-value").textContent = project.listen_port || '未设置';
    // } else if(2 === project.mode) {
    //     serviceCard.querySelector(".project-target-ip .field-value").textContent = project.target_ip || '未设置';
    // }
    // serviceCard.querySelector(".project-protocol-cnt .field-value").textContent = project.protocol_cnt || '0';
    // TODO: 待考虑 是否能修改

    serviceCard.dataset.runtimeState = String(isProjectActive(project) ? 1 : 0);
    serviceCard.dataset.active = String(isProjectActive(project) ? 1 : 0);
    serviceCard.dataset.status = String(project.status == null ? 1 : project.status);
    serviceCard.classList.toggle('is-deleted', isProjectDeleted(project));

    const endpointValue = serviceCard.querySelector(project.mode === ProjectMode.SERVER
        ? '.project-listen-port .field-value'
        : '.project-target-ip .field-value');
    if (endpointValue) {
        endpointValue.textContent = getProjectEndpointDisplay(project);
    }

    const statusButton = serviceCard.querySelector('.project-status.service-active-toggle');
    if (statusButton) {
        statusButton.dataset.nextActive = isProjectActive(project) ? '0' : '1';
        statusButton.setAttribute('aria-label', isProjectActive(project) ? '停止测试服务' : '启动测试服务');
        statusButton.disabled = false;
        statusButton.classList.remove('is-busy');
    }

    const statusValue = serviceCard.querySelector(".project-status .field-value");
    if (statusValue) {
        statusValue.textContent = getProjectRuntimeStatusText(project);
        statusValue.className = `meta-value field-value status ${isProjectDeleted(project) ? 'status-deleted' : (isProjectActive(project) ? 'status-active' : 'status-inactive')}`;
    }

    serviceCard.querySelectorAll('.protocol-item').forEach(protocolItem => {
        protocolItem.dataset.projectRuntimeState = serviceCard.dataset.runtimeState;
        refreshProtocolRuntimeControl(protocolItem, protocolItem.dataset.configState);
    });
    
}

/**
 * @param {HTMLDivElement} serviceCard
 */
function bindServiceCardToggle(serviceCard) {
    const toggleIcon = serviceCard.querySelector('.toggle-icon');
    if (!toggleIcon) return;

    toggleIcon.addEventListener('click', function(e) {
        e.stopPropagation();
        serviceCard.classList.toggle('expanded');
        toggleIcon.textContent = serviceCard.classList.contains('expanded') ? '▲' : '▼';
    });
}

/**
 * 生成协议项页 URL，并保留 mock/后端调试参数。
 * @param {number | string} projectId
 * @returns {string}
 */
function buildProtocolItemsUrl(projectId) {
    const params = new URLSearchParams(window.location.search);
    params.set('projectId', String(projectId));
    return `protocol_items.html?${params.toString()}`;
}

if (typeof window !== 'undefined') {
    window.buildProtocolItemFormUrl = buildProtocolItemFormUrl;
}

function bindOpenProtocolItemsAction(serviceCard, project) {
    const viewBtn = serviceCard.querySelector('.view-protocols-btn');
    if (viewBtn) {
        viewBtn.addEventListener('click', function(e) {
            e.preventDefault();
            e.stopPropagation();
            const targetUrl = buildProtocolItemsUrl(project.id);
            serviceCard.dataset.protocolItemsUrl = targetUrl;
            const navigateEvent = new CustomEvent('service-card:navigate-protocol-items', {
                bubbles: true,
                cancelable: true,
                detail: {
                    projectId: project.id,
                    url: targetUrl,
                },
            });
            serviceCard.dispatchEvent(navigateEvent);
            if (navigateEvent.defaultPrevented) return;
            window.location.href = targetUrl;
        });
    }
}

function bindServiceActiveToggle(serviceCard, project) {
    const toggleButton = serviceCard.querySelector('.service-active-toggle');
    if (!toggleButton) return;

    toggleButton.addEventListener('click', async function(event) {
        event.preventDefault();
        event.stopPropagation();

        const projectId = Number(project.id);
        const nextActive = toggleButton.dataset.nextActive === '1';
        const actionText = nextActive ? '启动' : '停止';
        toggleButton.disabled = true;
        toggleButton.classList.add('is-busy');

        try {
            const nextProject = await setProjectActive(projectId, nextActive);
            updateServiceCard(serviceCard.id, Object.assign({}, project, nextProject));
        } catch (error) {
            showErrorPopup(`${actionText}测试服务失败：${error.message}`);
            toggleButton.disabled = false;
            toggleButton.classList.remove('is-busy');
        }
    });
}

function bindServiceDeleteAction(serviceCard) {
    const deleteBtn = serviceCard.querySelector('.delete-service-btn');
    if (!deleteBtn) return;

    deleteBtn.addEventListener('click', async function(e) {
        e.stopPropagation();
        if (confirm('确定要删除这个测试服务吗？')) {

            const ok = await delProject(serviceCard.id);
            if(true === ok) {
                const visibleCountBeforeDelete = document.querySelectorAll('.service-cards .service-card').length;
                const nextPage = KitProxy.pagination.nextPageAfterDelete(servicePageState, visibleCountBeforeDelete);
                await loadAllProjects(nextPage);
            } else {
                showErrorPopup('删除测试服务失败!');
            }
        }
    });
}

function bindServiceRestoreAction(serviceCard) {
    const restoreBtn = serviceCard.querySelector('.restore-service-btn');
    if (!restoreBtn) return;

    restoreBtn.addEventListener('click', async function(e) {
        e.stopPropagation();
        if (!confirm('确定要恢复这个测试服务吗？')) return;

        const ok = await restoreProject(serviceCard.id);
        if (ok) {
            await loadAllProjects(servicePageState.currentPage);
        } else {
            showErrorPopup('恢复测试服务失败!');
        }
    });
}

function bindAddProtocolAction(serviceCard, project) {
    const addProtocolBtn = serviceCard.querySelector('.add-protocol-btn');
    if (!addProtocolBtn) return;

    addProtocolBtn.addEventListener('click', function(e) {
        e.stopPropagation();

        // 根据不同协议类型生成不同的框
        const modal = createAddProtocolModal(ProtocolTypeRegistry.getAddProtocolModal(project.protocol_type), serviceCard);
        if(modal) {
            document.body.appendChild(modal);
        }
    });
}

function insertServiceCard(serviceCard, pos = -1) {
    const serviceCards = document.querySelector('.service-cards');
    if(!serviceCards) return;

    if(-1 === pos) {
        serviceCards.appendChild(serviceCard);
    } else if(pos >= 1) {
        serviceCards.insertBefore(serviceCard, serviceCards.childNodes[pos]);
    }
}


/** 态生成测试服务卡片页面 + 事件监听
 * @param {{ id: string; name: any; protocol_type: number; listen_port: any; mode: any; status: boolean | undefined; }} project
 */
function addServiceCard(project, pos = -1) {

    const serviceCard = document.createElement('div');
    serviceCard.className = 'service-card';
    serviceCard.id = String("service-card-" + project.id);
    serviceCard.dataset.runtimeState = String(isProjectActive(project) ? 1 : 0);
    serviceCard.dataset.active = String(isProjectActive(project) ? 1 : 0);
    serviceCard.dataset.status = String(project.status == null ? 1 : project.status);
    serviceCard.classList.toggle('is-deleted', isProjectDeleted(project));

    serviceCard.innerHTML = serviceCardHTML(project);

    const titleElement = serviceCard.querySelector('.service-title');
    if (!isProjectDeleted(project) && titleElement && KitProxy.utils.bindInlineTitleEditor) {
        KitProxy.utils.bindInlineTitleEditor({
            titleElement,
            onSave: function(newTitle) {
                return updateProjectName(serviceCard.id, newTitle);
            },
            emptyMessage: '服务名称不能为空',
        });
    }

    if (!isProjectDeleted(project)) {
        ProtocolTypeRegistry.bindServiceExtraActions(serviceCard, project);
        bindServiceActiveToggle(serviceCard, project);
        bindOpenProtocolItemsAction(serviceCard, project);
        bindServiceDeleteAction(serviceCard);
    } else {
        bindServiceRestoreAction(serviceCard);
    }
    insertServiceCard(serviceCard, pos);

    return serviceCard;
}

// 刷新所有页面状态
function refreshServiceCards(projects) {
    projects.forEach(project => {
        addServiceCard(project);
    });
}

function renderServiceEmptyState(message) {
    const serviceCards = document.querySelector('.service-cards');
    if (!serviceCards) return;
    const escape = KitProxy.utils.escapeHTML;

    const emptyState = document.createElement('div');
    emptyState.className = 'empty-state';
    emptyState.innerHTML = `
        <div class="empty-message">
            <p>${escape(message)}</p>
        </div>
    `;
    serviceCards.appendChild(emptyState);
}

function updateServiceFilterSummary(totalCount) {
    const summary = document.getElementById('service-filter-summary');
    if (!summary || !KitProxy.serviceFilters) return;

    const description = KitProxy.serviceFilters.describe(serviceFilterState.filters);
    summary.textContent = `${description}，共 ${totalCount} 条`;
}

function setServiceFilterError(message) {
    const errorBox = document.getElementById('service-filter-error');
    if (!errorBox) return;

    errorBox.textContent = message || '';
    errorBox.style.display = message ? 'block' : 'none';
}

function renderCurrentPageServices() {
    const serviceCards = document.querySelector('.service-cards');
    if (!serviceCards) return;

    serviceCards.innerHTML = '';
    refreshServiceCards(currentPageProjects);

    if (currentPageProjects.length === 0) {
        renderServiceEmptyState(servicePageState.total === 0 && serviceFilterState.active
            ? '没有符合筛选条件的测试服务'
            : '暂无测试服务，点击按钮添加');
    }

    updateServiceFilterSummary(servicePageState.total);
}

async function applyServiceFiltersFromDOM() {
    if (!KitProxy.serviceFilters) return;

    const filters = KitProxy.serviceFilters.readFromDOM(document);
    const validation = KitProxy.serviceFilters.validate(filters);

    if (!validation.valid) {
        setServiceFilterError(validation.message);
        return;
    }

    setServiceFilterError('');
    serviceFilterState.filters = filters;
    serviceFilterState.active = KitProxy.serviceFilters.hasActiveFilters(filters);
    await loadAllProjects(1);
}

let ownerCandidateTimer = null;
let ownerCandidateRequestId = 0;
let ownerNoteComposing = false;
const OWNER_NOTE_SEARCH_DEBOUNCE_MS = 600;
const OWNER_NOTE_SEARCH_TIMEOUT_MS = 5000;

function clearOwnerNoteSelection(input) {
    if (!input) return;
    delete input.dataset.selectedUserId;
    delete input.dataset.selectedNote;
}

function hideOwnerNoteCandidates() {
    const list = document.getElementById('filter-owner-candidates');
    if (!list) return;
    list.hidden = true;
    list.innerHTML = '';
}

function setOwnerNoteSearchState(searching) {
    const input = document.getElementById('filter-owner-note');
    if (!input) return;

    input.disabled = Boolean(searching);
    input.setAttribute('aria-busy', searching ? 'true' : 'false');
}

function focusOwnerNoteInput() {
    const input = document.getElementById('filter-owner-note');
    if (!input || input.disabled) return;
    input.focus({ preventScroll: true });
}

function renderOwnerNoteStatus(message) {
    const list = document.getElementById('filter-owner-candidates');
    if (!list) return;

    const escape = KitProxy.utils.escapeHTML;
    list.innerHTML = `<div class="filter-candidate-status" role="status">${escape(message)}</div>`;
    list.hidden = false;
}

function renderOwnerNoteCandidates(candidates) {
    const list = document.getElementById('filter-owner-candidates');
    if (!list) return;

    const escape = KitProxy.utils.escapeHTML;
    const validCandidates = (Array.isArray(candidates) ? candidates : []).filter(candidate => {
        const userId = Number(candidate && (candidate.user_id != null ? candidate.user_id : candidate.id));
        const note = String(candidate && candidate.note_name || '');
        return Number.isInteger(userId) && userId > 0 && Boolean(note);
    });

    list.innerHTML = '';
    if (validCandidates.length === 0) {
        renderOwnerNoteStatus('未搜索到匹配项');
        return;
    }

    validCandidates.forEach(candidate => {
        const userId = Number(candidate && (candidate.user_id != null ? candidate.user_id : candidate.id));
        const note = String(candidate && candidate.note_name || '');

        const status = String(candidate.status || '').toLowerCase() === 'active' ? '正常' : '已停用';
        const option = document.createElement('button');
        option.type = 'button';
        option.className = 'filter-candidate-option';
        option.setAttribute('role', 'option');
        option.dataset.userId = String(userId);
        option.dataset.note = note;
        option.innerHTML = `<span>${escape(note)}</span><small>${escape(status)}</small>`;
        option.addEventListener('click', function() {
            const input = document.getElementById('filter-owner-note');
            if (!input) return;
            input.value = note;
            input.dataset.selectedUserId = String(userId);
            input.dataset.selectedNote = note;
            hideOwnerNoteCandidates();
            setServiceFilterError('');
        });
        list.appendChild(option);
    });

    list.hidden = list.children.length === 0;
}

async function loadOwnerNoteCandidates(keyword) {
    const requestId = ++ownerCandidateRequestId;
    const normalizedKeyword = String(keyword || '').trim();
    if (!normalizedKeyword) {
        setOwnerNoteSearchState(false);
        hideOwnerNoteCandidates();
        return;
    }

    setOwnerNoteSearchState(true);
    renderOwnerNoteStatus('正在搜索');
    setServiceFilterError('');

    let timeoutId = null;
    try {
        const request = Promise.resolve().then(() => {
            return KitProxy.api.getProjectNoteCandidates(normalizedKeyword, 10);
        });
        const timeout = new Promise((resolve, reject) => {
            timeoutId = window.setTimeout(() => {
                const error = new Error('note 候选搜索超时');
                error.code = 'OWNER_NOTE_SEARCH_TIMEOUT';
                reject(error);
            }, OWNER_NOTE_SEARCH_TIMEOUT_MS);
        });
        const candidates = await Promise.race([request, timeout]);
        if (requestId !== ownerCandidateRequestId) return;
        renderOwnerNoteCandidates(candidates);
    } catch (error) {
        if (requestId !== ownerCandidateRequestId) return;
        const timedOut = error && error.code === 'OWNER_NOTE_SEARCH_TIMEOUT';
        renderOwnerNoteStatus(timedOut ? '搜索超时，请重试' : '搜索失败');
        setServiceFilterError(timedOut
            ? 'note 候选搜索超时，请重试'
            : (error && error.message ? error.message : '获取所有者候选失败'));
    } finally {
        if (timeoutId !== null) {
            window.clearTimeout(timeoutId);
        }
        if (requestId === ownerCandidateRequestId) {
            setOwnerNoteSearchState(false);
            focusOwnerNoteInput();
        }
    }
}

function bindOwnerNoteCandidates() {
    const input = document.getElementById('filter-owner-note');
    if (!input || input.dataset.candidatesBound === '1') return;
    input.dataset.candidatesBound = '1';

    const scheduleSearch = function(ownerInput) {
        if (!ownerInput || ownerInput.disabled || ownerNoteComposing) return;

        ++ownerCandidateRequestId;
        window.clearTimeout(ownerCandidateTimer);
        hideOwnerNoteCandidates();
        const keyword = ownerInput.value.trim();
        if (!keyword) {
            setOwnerNoteSearchState(false);
            hideOwnerNoteCandidates();
            return;
        }
        ownerCandidateTimer = window.setTimeout(
            () => loadOwnerNoteCandidates(keyword),
            OWNER_NOTE_SEARCH_DEBOUNCE_MS,
        );
    };

    input.addEventListener('compositionstart', function() {
        if (this.disabled) return;
        ownerNoteComposing = true;
        window.clearTimeout(ownerCandidateTimer);
        hideOwnerNoteCandidates();
    });

    input.addEventListener('compositionend', function() {
        ownerNoteComposing = false;
        if (!this.disabled) {
            this.dispatchEvent(new Event('input', { bubbles: true }));
        }
    });

    input.addEventListener('input', function(event) {
        if (this.disabled || ownerNoteComposing || event.isComposing) return;

        if (this.value.trim() !== String(this.dataset.selectedNote || '')) {
            clearOwnerNoteSelection(this);
        }

        scheduleSearch(this);
    });

}

/**
 * 管理员才需要查看软删数据和跨用户搜索；普通用户筛选栏保持只筛自己的服务。
 */
function syncAdminOnlyServiceFilters() {
    const isAdmin = Boolean(KitProxy.auth && KitProxy.auth.isCurrentUserAdmin());
    const deletedStatusOption = document.querySelector('#filter-status option[value="deleted"]');
    const ownerNoteField = document.querySelector('[data-admin-only] #filter-owner-note')?.closest('[data-admin-only]');

    if (deletedStatusOption) {
        deletedStatusOption.hidden = !isAdmin;
        deletedStatusOption.disabled = !isAdmin;
    }

    if (ownerNoteField) {
        ownerNoteField.hidden = !isAdmin;
        if (!isAdmin) {
            const ownerInput = ownerNoteField.querySelector('#filter-owner-note');
            if (ownerInput) {
                ownerInput.value = '';
                clearOwnerNoteSelection(ownerInput);
            }
            setOwnerNoteSearchState(false);
            hideOwnerNoteCandidates();
        }
    }
}

function resetServiceFilters() {
    const statusSelect = document.getElementById('filter-status');
    const protocolTypeSelect = document.getElementById('filter-protocol-type');
    const ownerNoteInput = document.getElementById('filter-owner-note');

    const timeRange = KitProxy.timeRangeFilter
        ? KitProxy.timeRangeFilter.bind(document, {
            prefix: 'filter-create',
            inputMask: 'imask',
            iconTrigger: true,
        })
        : null;
    if (timeRange && typeof timeRange.reset === 'function') timeRange.reset();
    if (statusSelect) statusSelect.value = 'all';
    if (protocolTypeSelect) protocolTypeSelect.value = 'all';
    if (ownerNoteInput) {
        ownerNoteInput.value = '';
        clearOwnerNoteSelection(ownerNoteInput);
    }
    ownerNoteComposing = false;
    ++ownerCandidateRequestId;
    setOwnerNoteSearchState(false);
    hideOwnerNoteCandidates();

    serviceFilterState.filters = {
        startDate: '',
        endDate: '',
        startTime: '',
        endTime: '',
        status: 'all',
        protocolType: 'all',
        ownerKeyword: '',
        ownerUserId: null,
        ownerNote: '',
    };
    serviceFilterState.active = false;
    setServiceFilterError('');
    loadAllProjects(1);
}

function bindServiceFilterActions() {
    const applyBtn = document.getElementById('apply-service-filter');
    const resetBtn = document.getElementById('reset-service-filter');

    if (KitProxy.timeRangeFilter) {
        KitProxy.timeRangeFilter.bind(document, {
            prefix: 'filter-create',
            inputMask: 'imask',
            iconTrigger: true,
        });
    }

    if (applyBtn) {
        applyBtn.addEventListener('click', applyServiceFiltersFromDOM);
    }

    if (resetBtn) {
        resetBtn.addEventListener('click', resetServiceFilters);
    }

    bindOwnerNoteCandidates();
}

/**
 * 渲染服务列表分页条。
 */
function renderServicePagination() {
    KitProxy.pagination.render(document.getElementById('service-pagination'), servicePageState, {
        pageSizeOptions: KitProxy.pagination.DEFAULT_PAGE_SIZE_OPTIONS,
        onPageSizeChange: function(pageSize) {
            servicePageState.pageSize = pageSize;
            servicePageState.currentPage = 1;
            loadAllProjects(1);
        },
        onPrev: function() {
            loadAllProjects(servicePageState.currentPage - 1);
        },
        onNext: function() {
            loadAllProjects(servicePageState.currentPage + 1);
        },
    });
}

// 加载当前页测试服务数据
async function loadAllProjects(page = servicePageState.currentPage) {
    const serviceCards = document.querySelector('.service-cards');
    if (!serviceCards) return;

    const loading = showLoading("正在加载测试服务列表...");

    try {
        servicePageState.currentPage = Math.max(1, Number(page) || 1);

        const requestPage = async () => getProjectList(
            KitProxy.pagination.getOffset(servicePageState),
            servicePageState.pageSize,
            KitProxy.serviceFilters.toRequest(serviceFilterState.filters),
        );

        let pageResult = await requestPage();
        if (!pageResult || !Array.isArray(pageResult.items)) {
            throw new Error('数据格式错误');
        }

        KitProxy.pagination.setPageResult(pageResult, servicePageState);
        const lastPage = KitProxy.pagination.getLastPage(servicePageState);
        if (servicePageState.currentPage > lastPage) {
            servicePageState.currentPage = lastPage;
            pageResult = await requestPage();
            if (!pageResult || !Array.isArray(pageResult.items)) {
                throw new Error('数据格式错误');
            }
            KitProxy.pagination.setPageResult(pageResult, servicePageState);
        }

        currentPageProjects = pageResult.items;

        // 后端已完成筛选和分页，当前页面只渲染响应中的 items。
        renderCurrentPageServices();
        renderServicePagination();

    } catch(error) {
        console.error('加载服务列表出错:', error);
        showErrorPopup('加载服务列表出错： ' + error.message);

    }finally{
        await delay(1000);
        hideLoading(loading);
    }

}


// 页面加载入口
document.addEventListener('DOMContentLoaded', async function() {
    const serviceCards = document.querySelector('.service-cards');
    if (!serviceCards) return;
    if (window.KitProxy && window.KitProxy.__disableAutoInitMain) return;

    try {
        await KitProxy.auth.requireCurrentUser();
    } catch (error) {
        if (Number(error && error.status) !== 401) {
            showErrorPopup(error && error.message ? error.message : '登录态校验失败');
        }
        return;
    }

    syncAdminOnlyServiceFilters();
    bindServiceFilterActions();

    // 监听添加服务按钮点击
    document.getElementById('add-service')?.addEventListener('click',  async function() {

        const modal = document.createElement('div');
        modal.className = 'modal-overlay';
        modal.innerHTML = addNewServiceCardModalHTML();
        document.body.appendChild(modal);

        bindAddServiceDynamicControls(modal);

        // 处理表单提交
        modal.querySelector('#add-service-form').addEventListener('submit', async function(e) {
            e.preventDefault();

            let project;
            try {
                project = collectAddServicePayload(modal);
            } catch(error) {
                showErrorPopup(error.message);
                return;
            }

            const addedProject = await addProject(project);
            if (!addedProject) return;

            KitProxy.utils.removeDomNode(modal);

        });

        KitProxy.utils.bindModalCloseActions(modal);
    });

    loadAllProjects();

});

/**
 * 生成不同协议项配置子类网格
 * @param {派生子类} item 
 * @param {协议项信息} protocol 
 * @returns 
 */
function createProtocolItemGrids(item, protocol) {
    return item.create(protocol);
}


var httpProtocolItemGrids = {
    create: function(protocol) {
        const grids = document.createElement('div');
        grids.className = 'details-grid http';
        const escape = KitProxy.utils && KitProxy.utils.escapeHTML
            ? KitProxy.utils.escapeHTML
            : function(value) { return String(value == null ? '' : value); };
        grids.innerHTML = `
            <div class="protocol-field req-cfg" data-field-name="method">
                <label>期望请求方法</label>
                <div class="value">${escape(protocol.req_cfg.method || '')}</div>
            </div>
            <div class="protocol-field req-cfg" data-field-name="path">
                <label>请求路径</label>
                <div class="value">${escape(protocol.req_cfg.path || '')}</div>
            </div>
            <div class="protocol-field request-body">
                <label><span class="body-indicator ${protocol.req_body_status === 1 ? 'has' : 'no'}"></span>校验请求Body</label>
                <div class="value">${protocol.req_body_status === 1 ? '已设置' : '未设置'}</div>
            </div>
            <div class="protocol-field response-body">
                <label><span class="body-indicator ${protocol.resp_body_status === 1 ? 'has' : 'no'}"></span>目标响应Body</label>
                <div class="value">${protocol.resp_body_status === 1 ? '已设置' : '未设置'}</div>
            </div>
        `;
        return grids;
    }
};

async function getAllPatternFieldsReq(protocolId, req_or_resp) {
    
    const protocolItem = document.getElementById(`protocol-item-${protocolId}`);

    const projectId = ExtractId(protocolItem.dataset.projectId);


    try{
        return await KitProxy.api.getAllPatternFields(projectId, protocolId, req_or_resp);

    } catch(error) {
        console.error(error.message);
        throw error;
    }


}

async function getPatternInfoReq(projectId) {

    try{
        return await KitProxy.api.getProjectPatternInfo(projectId);

    } catch(error) {
        console.error("获取字段信息失败!");
        throw error;
    }


}

async function getSpecialPatternFields(projectId) {
    try {

        const pattern_info = await getPatternInfoReq(projectId);
        
        console.log('pattern_info: ', pattern_info);
        
        return pattern_info.special_fields || [];
    } catch (error) {
        console.error('获取特殊字段信息失败!');
        throw error;
    }
}

async function getPatternFields(protocolId, req_or_resp) {

    try {

        const [pattern_info, common_fields] = await getAllPatternFieldsReq(protocolId, req_or_resp)

        return {
            "special_fields": pattern_info.special_fields || [],
            "common_fields": common_fields || [],
        }

    } catch (error) {
        console.error('获取所有字段信息失败!');
        throw error;
    }

}

var customTcpProtocolItemGrids = {
    create: function(protocol) {

        const grids = document.createElement('div');
        grids.className = 'details-grid tcp';
        const escape = KitProxy.utils && KitProxy.utils.escapeHTML
            ? KitProxy.utils.escapeHTML
            : function(value) { return String(value == null ? '' : value); };
        const reqCfg = protocol.req_cfg || {};
        const respCfg = protocol.resp_cfg || {};
        const reqCommonFields = Array.isArray(reqCfg.common_fields) ? reqCfg.common_fields : [];
        const respCommonFields = Array.isArray(respCfg.common_fields) ? respCfg.common_fields : [];
        grids.innerHTML = `
            <div class="protocol-field req-cfg" data-field-name="function_code_filed_value">
                <label>请求功能码</label>
                <div class="value">${escape(reqCfg.function_code_filed_value || '')}</div>
            </div>
            <div class="protocol-field req-cfg" data-field-name="common_fields">
                <label><span class="header-fields-indicator ${reqCommonFields.length ? 'has' : 'no'}"></span>请求头部字段</label>
                <div class="value" id="${escape(protocol.id)}-header-fields">${reqCommonFields.length ? '已设置' : '未设置'}</div>
            </div>

            <div class="protocol-field request-body">
                <label><span class="body-indicator ${protocol.req_body_status === 1 ? 'has' : 'no'}"></span>校验请求Body</label>
                <div class="value">${protocol.req_body_status === 1 ? '已设置' : '未设置'}</div>
            </div>

            <div class="protocol-field resp-cfg" data-field-name="function_code_filed_value">
                <label>响应功能码</label>
                <div class="value">${escape(respCfg.function_code_filed_value || '')}</div>
            </div>
            <div class="protocol-field resp-cfg" data-field-name="common_fields">
                <label><span class="header-fields-indicator ${respCommonFields.length ? 'has' : 'no'}"></span>响应头部字段</label>
                <div class="value">${respCommonFields.length ? '已设置' : '未设置'}</div>
            </div>
            <div class="protocol-field response-body">
                <label><span class="body-indicator ${protocol.resp_body_status === 1 ? 'has' : 'no'}"></span>目标响应Body</label>
                <div class="value">${protocol.resp_body_status === 1 ? '已设置' : '未设置'}</div>
            </div>
        `;
        return grids;
    }
};

/**
 * 生成body编辑框
 * @param {*} body_type 
 * @param {*} body_data 
 */
function createProtocolItemBodyModal(body_type, body_data, handleCb, options = {}) {

    let currentBody = KitProxy.bodySyntax
        ? KitProxy.bodySyntax.decodeBodyData(body_data)
        : new TextDecoder().decode(body_data || new Uint8Array());

    try {
        if(currentBody && KitProxy.bodySyntax) {
            currentBody = KitProxy.bodySyntax.format(currentBody, body_type);
        }
    } catch(error) {
        console.error('请求体数据解析出错! ', error.message);
    }
    
    const modal = document.createElement('div');
    modal.className = 'modal-overlay';
    modal.innerHTML = `
        <div class="edit-body-modal">
            <div class="modal-header">
                <h3>编辑Body</h3>
                <button class="close-modal">&times;</button>
            </div>
            <div class="modal-body">
                <div class="form-group">
                    <label>Body内容</label>
                    <div id="body-editor-host"></div>
                </div>
                <div class="form-actions">
                    <button type="button" class="cancel-btn">取消</button>
                    <button type="button" class="confirm-btn">确定修改</button>
                </div>
            </div>
        </div>
    `;
    
    document.body.appendChild(modal);

    const bodyEditor = KitProxy.bodyEditor.create(modal.querySelector('#body-editor-host'), {
        idPrefix: 'protocol-body',
        value: currentBody,
        bodyType: body_type,
        allowedTypes: KitProxy.protocolTypes && typeof KitProxy.protocolTypes.getBodyTypeOptions === 'function'
            ? KitProxy.protocolTypes.getBodyTypeOptions(options.protocolType || 'HTTP')
            : undefined,
        placeholder: options.placeholder || '输入 Body 内容...',
    });

    
    // 处理确定按钮
    modal.querySelector('.confirm-btn').addEventListener('click', async function(e) {
        e.stopPropagation();
        e.preventDefault();

        const validation = bodyEditor.validate();
        if(!validation.valid) {
            alert(validation.message);
            return;
        }

        const newBodyType = bodyEditor.getType();
        const newBody = bodyEditor.getValue().trim();

        await handleCb(newBodyType, newBody);

        KitProxy.utils.removeDomNode(modal);
    });

    KitProxy.utils.bindModalCloseActions(modal);
}

const DEBUG_MODE = 0;

// 服务模式
const ProjectMode = Object.freeze({
    SERVER: 1,
    CLIENT: 2,
});

const ProjectModeStr = {
    1 : "服务器模式",
    2 : "客户端模式",
};

// 协议类型
const ProtocolType = Object.freeze({
    HTTP: 1,
    CUSTOM_TCP: 2,
    HTTPS: 3
});

const ProtocolTypeStr = {
    1 : "HTTP",
    2 : "自定义TCP",
    3 : "HTTPS"
};

const ProtocolTypeFunc = {
    1 : httpProtocolModal,
    2 : customTcpProtocolModal,
};

// 格式类型(默认为标准格式)
const PatternType = Object.freeze({
    STANDARD          : 0, // 标准格式
    BODY_LENGTH_DEP   : 1, // Body长度依赖
    TOTAL_LENGTH_DEP  : 2, // 总长度依赖
    NO_LENGTH_DEP     : 3, // 无长度长度依赖(需要用户配置)
})

const PatternTypeStr = {
    0 : "标准格式",
    1 : "Body长度依赖",
    2 : "总长度依赖",
    3 : "无长度依赖"
};

// 字段类型
const PatternFieldType = Object.freeze({
    INT8              : 1,
    UINT8             : 2,
    INT16             : 3,
    UINT16            : 4,
    INT32             : 5,
    UINT32            : 6,
    INT64             : 7,
    UINT64            : 8,
    FLOAT             : 9,
    DOUBLE            : 10,
    STR               : 11,
})

const PatternFiledTypeStr = {
    1  : "INT8",
    2  : "UINT8",
    3  : "INT16",
    4  : "UINT16",
    5  : "INT32",
    6  : "UINT32",
    7  : "INT64",
    8  : "UINT64",
    9  : "FLOAT",
    10 : "DOUBLE",
    11 : "STR",
};

// 创建延时函数
function delay(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

// 显示等待加载界面
function showLoading(message = '处理中，请稍候...') {
    const loadingOverlay = document.createElement('div');
    loadingOverlay.className = 'loading-overlay';
    loadingOverlay.style.cssText = `
        position: fixed;
        top: 0;
        left: 0;
        right: 0;
        bottom: 0;
        background-color: rgba(0,0,0,0.7);
        display: flex;
        justify-content: center;
        align-items: center;
        z-index: 9999;
    `;
    loadingOverlay.innerHTML = `
        <div class="loading-content" style="
            background: white;
            padding: 30px;
            border-radius: 8px;
            text-align: center;
            box-shadow: 0 4px 12px rgba(0,0,0,0.15);
            min-width: 300px;
        ">
            <div class="loading-spinner" style="
                width: 50px;
                height: 50px;
                border: 5px solid #f3f3f3;
                border-top: 5px solid #3498db;
                border-radius: 50%;
                margin: 0 auto 20px;
                animation: spin 1s linear infinite;
            "></div>
            <p class="loading-message" style="
                margin: 0;
                font-size: 16px;
                color: #333;
            ">${message}</p>
            <style>
                @keyframes spin {
                    0% { transform: rotate(0deg); }
                    100% { transform: rotate(360deg); }
                }
            </style>
        </div>
    `;
    document.body.appendChild(loadingOverlay);
    return loadingOverlay;
}

// 隐藏等待加载界面
function hideLoading(loadingOverlay) {

    if (loadingOverlay && document.body.contains(loadingOverlay)) {
        document.body.removeChild(loadingOverlay);
    }
}

function ExtractId(IdStr) {
    // 获取当前测试服务卡片ID
    // 找到最后一个-的位置
    let i = 0;
    const str = new String(IdStr);
    const pos = str.lastIndexOf('-');
    if(-1 === i)
        return -1;
    return Number(str.slice(pos + 1));
}

// 检查空状态并显示提示
function checkEmptyState()  {
    const serviceCards = document.querySelector('.service-cards');

    if (!serviceCards) return;
    
    if (serviceCards.children.length === 0) {
        // 创建空状态提示
        const emptyState = document.createElement('div');
        emptyState.className = 'empty-state';
        /*暂时不添加按钮*/
        // emptyState.innerHTML = `
        //     <div class="empty-message">
        //         <p>暂无测试服务，点击下方按钮添加</p>
        //         <button id="add-first-service" class="add-service-btn">+ 添加测试服务</button>
        //     </div>
        // `;
        emptyState.innerHTML = `
            <div class="empty-message">
                <p>暂无测试服务，点击按钮添加</p>
            </div>
        `;
        serviceCards.appendChild(emptyState);

        // 添加点击事件
        // emptyState.querySelector('#add-first-service').addEventListener('click', function() {
        //     document.getElementById('add-service')?.click();
        // });
    } else {
        // 移除现有的空状态提示
        const existingEmptyState = document.querySelector('.empty-state');
        if (existingEmptyState) {
            existingEmptyState.remove();
        }
    }
};


// 加载模态框模板（暂时无用）
async function loadModal(path) { 
    try { 
        const doc = await fetch(path); 
        if (!response.ok) 
            throw new Error('模板加载失败'); 
        return await doc.text(); 
    } catch (error) { 
        console.error('加载模板失败:', error); return null; 
    }
    
}

// 更新协议项显示
function updateProtocolItem(id_str, protocol) {

    console.info('updateProtocolItem: ', protocol);

    const protocolItem = document.getElementById(id_str);

    protocolItem.querySelector(".protocol-name").textContent = protocol.name;

    // 方法名称
    protocolItem.querySelector('.protocol-field[data-field="method"]').querySelector('.value').textContent = protocol.req_cfg.method;
    // 请求路径
    protocolItem.querySelector('.protocol-field[data-field="path"]').querySelector(".value").textContent = protocol.req_cfg.path;

    // 请求Body
    const request_body_div =  protocolItem.querySelector('.protocol-field[data-field="request-body"]');
    request_body_div.querySelector('.body-indicator').className = `body-indicator ${protocol.req_body_status === 1 ? "has-body" : "no-body"}`;
    request_body_div.querySelector('.value').textContent = `${protocol.req_body_status === 1 ? '已设置' : '未设置'}`;
    // 响应Body
    const response_body_div =  protocolItem.querySelector('.protocol-field[data-field="response-body"]');
    response_body_div.querySelector('.body-indicator').className = `body-indicator ${protocol.resp_body_status === 1 ? "has-body" : "no-body"}`;
    response_body_div.querySelector('.value').textContent = `${protocol.resp_body_status === 1 ? '已设置' : '未设置'}`;


}


// 协议请求体配置
async function updateProtocolBodyReq(protocolId, projectId, req_or_resp, protocolType, bodyType, body) {
                
    try {
        const formData = new FormData();
        formData.append("detail_header", JSON.stringify({
            "id": protocolId,
            "project_id": projectId,
            "req_or_resp": req_or_resp,
            "type": protocolType,
            "body_type": bodyType,
        }));

        if(body.length > 0) {
            // 转换为二进制数据
            const binaryBlob = new Blob([body], { type: "application/octet-stream" });
            formData.append("detail_cfg_data", binaryBlob, req_or_resp === 1 ? 'req_body.bin' : 'resp_body.bin');
        }
        for (const [key, value] of formData.entries()) {
            console.info(key, value);
        }

        if(!DEBUG_MODE) {

        
        const response = await fetch('/protocols/details/body', {
            method: 'POST',
            body: formData
        });

        const result = await response.json();

        if (!response.ok || result.code !== 0) {
            throw new Error('code: '+ result.code  + ",msg:" + result.message);
        }
        
        console.log('协议项请求配置修改请求成功:', {
            code: result.code,
            message: result.message,
            data: result.data
        });

        }

        return true;

    } catch (error) {
        console.error('协议项请求配置修改请求失败! ', error.message);
        return false;
    }
}



// 协议请求项配置
async function updateProtocolCfgReq(protocolId, projectId, protocolType, req_cfg_json) {
                
    try {
        const formData = new FormData();
        formData.append("detail_header", JSON.stringify({
            "id": protocolId,
            "project_id": projectId,
            "req_or_resp": 1,
            "type": protocolType,
        }));
        formData.append("detail_cfg_data", JSON.stringify(req_cfg_json));
        
        for (const [key, value] of formData.entries()) {
            console.info(key, value);
        }

        if(!DEBUG_MODE) {

        
        const response = await fetch('/protocols/details/cfg', {
            method: 'POST',
            body: formData
        });

        const result = await response.json();

        if (!response.ok || result.code !== 0) {
            throw new Error('code: '+ result.code  + ",msg:" + result.message);
        }
        
        console.log('协议项请求配置修改请求成功:', {
            code: result.code,
            message: result.message,
            data: result.data
        });

        }

        return true;

    } catch (error) {
        console.error('协议项请求配置修改请求失败! ', error.message);
        return false;
    }
}


async function getProtocolBodyTypeReq(protocolId) {
    try {


    } catch (error) {
        
    }
}

async function getProtocolItemBody(idStr, req_or_resp) {

    const protocolItem = document.getElementById(idStr);
    const protocolItemId = ExtractId(idStr);

    try {
        if(!DEBUG_MODE) {
        const response1 = await fetch("/protocols/details/body_type", {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                "id": protocolItemId,
                "req_or_resp": req_or_resp,
            })
        });

        const response2 = await fetch("/protocols/details/body_data", {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                'id': protocolItemId,
                'req_or_resp': req_or_resp,
            })
        });

        const res1 = await response1.json();
        const res2 = await response2.bytes();

        if(!response1.ok || !response2.ok) {
            throw new Error('请求体类型/数据请求失败!');
        }

        if(!Number.isInteger(res1.code) || res1.code != 0) {
            throw new Error('获取请求体类型出错! ' + res1.message);
        }

        let body_data = '';
        if(res2.length > 0 && 200 === response2.status) {
            body_data = new TextDecoder().decode(res2);
        }
        
        return [res1.data.body_type, body_data];
        }

        return ["json", `{
            "test1": "111111",
            "test2": "222222"
        }`];

    } catch(error) {
        console.error("获取协议请求");
        throw error;
    }
}

function getCurProtocolItemReqCfg(idStr) {
    const protocolItem = document.getElementById(idStr);
   
    // 需要根据不同组件获取内
    const methodStr = protocolItem.querySelector('.protocol-field[data-field="method"]').querySelector('.value').textContent;
    const pathStr = protocolItem.querySelector('.protocol-field[data-field="path"]').querySelector('.value').textContent;
    const root = {
        "method": methodStr,
        "path": pathStr,
    }

    return root;
}

async function updateProtocolBody(idStr, req_or_resp, protocolType, newBodyType, newBody) {
    const protocolItemId = ExtractId(idStr);
    const protocolItem = document.getElementById(idStr);
    const serviceCardId = ExtractId(protocolItem.projectId);

    try {
        if(!Number.isInteger(protocolItemId) || protocolItemId <= 0) {
            throw new Error("无效的协议项ID");
        }

        const ok = await updateProtocolBodyReq(protocolItemId, serviceCardId, req_or_resp, protocolType, newBodyType, newBody);
        if(!ok) {
            throw new Error("修改协议项请求体失败!");
        }

    } catch(error) {
        console.error('修改协议项请求体失败!');
        return false;
    }

    return true;
}

async function updateProtocolHttpUrl(idStr, newPath) {
    const protocolItemId = ExtractId(idStr);
    const protocolItem = document.getElementById(idStr);
    const serviceCardId = ExtractId(protocolItem.projectId);

    // 获取出来所有req_cfg  返回一个JSON结构
    // 不同协议获取的JSON结构不同
    let req_cfg_json = getCurProtocolItemReqCfg(idStr);
    req_cfg_json["path"] = newPath;
    console.info('req_cfg_json: ', req_cfg_json);

    try {
        if(!Number.isInteger(protocolItemId) || protocolItemId <= 0) {
            throw new Error("无效的协议项ID");
        }

        const ok = await updateProtocolCfgReq(protocolItemId, serviceCardId, 'HTTP', req_cfg_json);
        if(!ok) {
            throw new Error("http修改路径失败");
        }

    } catch {
        console.error('http修改路径失败!');
        return false;
    }

    return true;
};


async function updateProtocolHttpMethod(idStr, newMethod) {
    const protocolItemId = ExtractId(idStr);
    const protocolItem = document.getElementById(idStr);
    const serviceCardId = ExtractId(protocolItem.projectId);

    console.info(idStr + ' :: ' + protocolItemId, protocolItem.projectId + '-' + serviceCardId);
    // 获取出来所有req_cfg  返回一个JSON结构
    // 不同协议获取的JSON结构不同
    let req_cfg_json = getCurProtocolItemReqCfg(idStr);

    req_cfg_json["method"] = newMethod;
    console.info('req_cfg_json: ', req_cfg_json);

    try {
        if(!Number.isInteger(protocolItemId) || protocolItemId <= 0) {
            throw new Error("无效的协议项ID");
        }

        const ok = await updateProtocolCfgReq(protocolItemId, serviceCardId, 'HTTP', req_cfg_json);
        if(!ok) {
            throw new Error("http修改请求方法失败");
        }

    } catch {
        console.error('http修改请求方法失败!');
        return false;
    }

    return true;
};

// 发起修改协议名称请求
async function updateProtocolNameReq(protocol_id, name) {

    try {

        const response = await fetch('/protocols/' + protocol_id + '/name', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                name: name,
            })

        });

        const result = await response.json();
        
        if (!response.ok || result.code !== 0) {
            throw new Error('code: '+ result.code  + ",msg:" + result.message);
        }
        
        console.log('修改协议项标题请求成功:', {
            code: result.code,
            message: result.message,
            data: result.data
        });

        return;

    } catch (error) {
        console.error('修改协议项标题请求失败:', error.String());
        throw error;
    }

}



async function updateProtocolName(id_str, tilte_name) {
    const protocolItemId = ExtractId(id_str)

    const protocolItem = document.getElementById(id_str);
    let protocols;

    const loading = showLoading();

    try {

        if(!Number.isInteger(protocolItemId) || protocolItemId <= 0) {
            throw new Error("无效的协议项ID");
        }

        if(!DEBUG_MODE) {
        await updateProtocolNameReq(protocolItemId, tilte_name);

        
        protocols = await getProtocolReq(protocolItemId);
        

        } else {
            protocols = JSON.parse(`[{
                "ctime": "2025-09-09 10:19:00",
                "id": 1,
                "method": "POST",
                "name": "test1",
                "path": "/api/test_update",
                "project_id": 1,
                "req_body_status": 1,
                "req_body_type": "json",
                "resp_body_status": 1,
                "resp_body_type": "json",
                "status_code": 0,
                "type": "HTTP",
                "utime": "2025-09-09 10:22:54"
            }]`);
        }
        // 动态更新页面数据
        updateProtocolItem(id_str, protocols[0]) 

    } catch(error) {
        console.error('修改测试服务标题失败:', error);
        hideLoading(loading);
        return false;
    } 


    await delay(500);
    hideLoading(loading);
    return true;
}


// 更新协议项卡片
function protocolItemHTML(protocol){
    console.info('protocolItemHTML: ', protocol);

    let length_type_str = '';
    let message_type_value_str = '';
    if('TCP' === protocol.type) {

        if(1 ===  protocol.length_type) {
            length_type_str = '固定长度'
        } else if(2 ===  protocol.length_type) {
            length_type_str = '变长'
        } else {
            length_type_str = '未知'
        }

        message_type_value_str = 'H'+ protocol.message_type_value.toString(16).toUpperCase();
    }


    return `
            <div class="protocol-header">
                <div>
                    <span class="protocol-tag">${protocol.type}</span>
                    <span class="protocol-name editable" data-default="${protocol.name}">${protocol.name}</span>
                    <div class="protocol-meta">
                        <span class="last-update-time">最后一次修改时间: ${protocol.utime}</span>
                        <span class="create-time">创建时间: ${protocol.ctime}</span>
                    </div>
                </div>
            </div>
            <div class="protocol-details">
                <div class="details-grid">
                    ${protocol.type === 'HTTP' || protocol.type === 'HTTPS' ? `
                    <div class="protocol-field clickable-field" data-field="method">
                        <label>期望请求方法</label>
                        <div class="value">${protocol.req_cfg.method}</div>
                    </div>
                    <div class="protocol-field clickable-field" data-field="path">
                        <label>请求路径</label>
                        <div class="value">${protocol.req_cfg.path}</div>
                    </div>
                    <div class="protocol-field clickable-field" data-field="request-body">
                        <label><span class="body-indicator ${protocol.req_body_status === 1 ? 'has-body' : 'no-body'}"></span>校验请求Body</label>
                        <div class="value">${protocol.resp_body_status === 1 ? '已设置' : '未设置'}</div>
                    </div>
                    <div class="protocol-field clickable-field" data-field="response-body">
                        <label><span class="body-indicator ${protocol.resp_body_status === 1 ? 'has-body' : 'no-body'}"></span>目标响应Body</label>
                        <div class="value">${protocol.resp_body_status === 1 ? '已设置' : '未设置'}</div>
                    </div>
                    ` : ''}
                    ${protocol.type === 'TCP' ? `
                    <div class="protocol-field clickable-field" data-field="length_type">
                        <label>长度类型: ${length_type_str}</label>
                        <div class="value">头部长度: ${protocol.head_length}</div> 
                        ${protocol.length_type == 2 ? `<div class="value">数据长度字段位置: ${protocol.data_length_pos} | 字段长度:  ${protocol.data_length_size}</div>` : ''}
                    </div>
                    <div class="protocol-field clickable-field" data-field="message_type">
                        <label>消息类型</label>
                        <div class="value">值: ${message_type_value_str}</div>
                        <div class="value">字段位置: ${protocol.message_type_pos} | 字段长度: ${protocol.message_type_size}</div>
                    </div>

                    <div class="protocol-field clickable-field" data-field="request-body">
                        <label><span class="body-indicator ${protocol.req_body_status === 1 ? 'has-body' : 'no-body'}"></span>校验请求Body</label>
                        <div class="value">${protocol.resp_body_status === 1 ? '已设置' : '未设置'}</div>
                    </div>
                    <div class="protocol-field clickable-field" data-field="response-body">
                        <label><span class="body-indicator ${protocol.resp_body_status === 1 ? 'has-body' : 'no-body'}"></span>目标响应Body</label>
                        <div class="value">${protocol.resp_body_status === 1 ? '已设置' : '未设置'}</div>
                    </div>
                    ` : ''}
                    </div>
                <div class="protocol-actions-container">
                    <button class="delete-protocol-btn">删除协议</button>
                </div>
            </div>
        `;
}

// 页面展示添加协议项
function addProtocolItem(serviceCard, protocol, pos = -1) {

    // 存储请求Body内容
    requestBody = protocol.requestBody || '';
    const protocolList = serviceCard.querySelector('.protocol-list');
    const protocolItem = document.createElement('div');
    protocolItem.className = 'protocol-item';
    protocolItem.id = `protocol-item-${protocol.id}`;
    protocolItem.projectId = serviceCard.id;
    protocolItem.className = `protocol-item ${protocol.type.toLowerCase()}`;
    protocolItem.innerHTML = protocolItemHTML(protocol);

    // 添加协议删除功能
    const deleteBtn = protocolItem.querySelector('.delete-protocol-btn');
    if (deleteBtn) {
        deleteBtn.addEventListener('click', async function(e) {
            e.stopPropagation();
            if (confirm('确定要删除这个协议吗？')) {
                
                const ok = await delProtocol(protocolItem.id);
                if(true === ok) {
                            
                    protocolItem.remove();
                    alert('删除协议项成功!');

                } else {
                    alert('删除协议项失败!');
                }

                // 更新协议数量
                // const protocolCount = protocolList.querySelectorAll('.protocol-item').length;
                // serviceCard.querySelector('.project-protocol-cnt').textContent = protocolCount;
            }
        });
    }

    if(-1 === pos) {
        protocolList.appendChild(protocolItem);
    } else {
        protocolList.insertBefore(protocolItem, protocolList.children[pos]);
    }

    // 更新协议数量统计
    const protocolCount = protocolList.querySelectorAll('.protocol-item').length;
    serviceCard.querySelector('.project-protocol-cnt').textContent = protocolCount;

    // 修复请求方法编辑功能 - 最终修复版
    const methodField = protocolItem.querySelector('.protocol-field[data-field="method"]');
    if (methodField) {
        methodField.style.cursor = 'pointer';
        
        // 移除旧的事件监听器避免重复绑定
        methodField.replaceWith(methodField.cloneNode(true));
        const newMethodField = protocolItem.querySelector('.protocol-field[data-field="method"]');
        
        newMethodField.addEventListener('click', function(e) {
            if (e.target.closest('.delete-protocol-btn')) return;
            
            e.stopPropagation();
            e.preventDefault();
            
            const valueElement = this.querySelector('.value');
            if (!valueElement) {
                console.error('未能找到value元素');
                return;
            }
            
            const currentMethod = valueElement.textContent.trim() || 'GET';
            const modal = document.createElement('div');
            modal.className = 'modal-overlay';
            modal.innerHTML = `
                <div class="edit-method-modal">
                    <div class="modal-header">
                        <h3>修改请求方法</h3>
                        <button class="close-modal">&times;</button>
                    </div>
                    <div class="modal-body">
                        <div class="method-radios">
                            <label><input type="radio" name="edit-method" value="GET" ${currentMethod.includes('GET') ? 'checked' : ''}> GET</label>
                            <label><input type="radio" name="edit-method" value="POST" ${currentMethod.includes('POST') ? 'checked' : ''}> POST</label>
                            <label><input type="radio" name="edit-method" value="PUT" ${currentMethod.includes('PUT') ? 'checked' : ''}> PUT</label>
                            <label><input type="radio" name="edit-method" value="DELETE" ${currentMethod.includes('DELETE') ? 'checked' : ''}> DELETE</label>
                        </div>
                        <div class="form-actions">
                            <button type="button" class="cancel-btn">取消</button>
                            <button type="button" class="confirm-btn">确定修改</button>
                        </div>
                    </div>
                </div>
            `;
            document.body.appendChild(modal);

            // 处理确定按钮
            modal.querySelector('.confirm-btn').addEventListener('click', async function() {
                const methodCheckboxes = modal.querySelectorAll('input[name="edit-method"]:checked');
                const methods = Array.from(methodCheckboxes).map(cb => cb.value);
                const newMethod = methods.length > 0 ? methods[0] : '';

                // TODO 这里需要根据不同组件 去组装不同协议 
                // 这里需要去组装整个req_cfg才能去发出
                const ok = await updateProtocolHttpMethod(protocolItem.id, newMethod);

                if (ok) {
                    valueElement.textContent = newMethod;
                    alert("http请求方法修改成功!");
                } else {
                    alert("http请求方法修改失败!");
                }

                document.body.removeChild(modal);
            });

            // 关闭模态框
            modal.querySelector('.close-modal').addEventListener('click', function() {
                document.body.removeChild(modal);
            });

            modal.querySelector('.cancel-btn').addEventListener('click', function() {
                document.body.removeChild(modal);
            });
            }); // 结束 methodField 点击事件
    } // 结束 if (methodField)

    // 添加请求路径编辑功能
    const pathField = protocolItem.querySelector('[data-field="path"]');
    if (pathField) {
        pathField.style.cursor = 'pointer';
        
        pathField.addEventListener('click', function(e) {
            if (e.target.closest('.delete-protocol-btn')) return;
            
            e.stopPropagation();
            e.preventDefault();
            
            const valueElement = this.querySelector('.value');
            if (!valueElement) {
                console.error('未能找到value元素');
                return;
            }
            
            const currentPath = valueElement.textContent.trim() || '/api/';
            
            const modal = document.createElement('div');
            modal.className = 'modal-overlay';
            modal.innerHTML = `
                <div class="edit-path-modal">
                    <div class="modal-header">
                        <h3>修改请求路径</h3>
                        <button class="close-modal">&times;</button>
                    </div>
                    <div class="modal-body">
                        <div class="form-group">
                            <label for="new-path">请求路径</label>
                            <input type="text" id="new-path" value="${currentPath}" placeholder="输入请求路径" required>
                            <div class="path-hint">必须以/开头，例如：/api/v1/test</div>
                        </div>
                        <div class="form-actions">
                            <button type="button" class="cancel-btn">取消</button>
                            <button type="button" class="confirm-btn">确定修改</button>
                        </div>
                    </div>
                </div>
            `;
            
            document.body.appendChild(modal);

            // 处理确定按钮
            modal.querySelector('.confirm-btn').addEventListener('click', async function() {
                const newPath = document.getElementById('new-path').value.trim();
                if (newPath && newPath.startsWith('/')) {

                    const ok = await updateProtocolHttpUrl(protocolItem.id, newPath);

                    if (ok) {
                        valueElement.textContent = newPath;
                        alert("http路径修改成功!");
                    } else {
                        alert("http路径修改失败!");
                    }

                    document.body.removeChild(modal);

                } else {
                    alert('路径必须以/开头');
                }
            });

            // 关闭模态框
            modal.querySelector('.close-modal').addEventListener('click', function() {
                document.body.removeChild(modal);
            });

            modal.querySelector('.cancel-btn').addEventListener('click', function() {
                document.body.removeChild(modal);
            });
        });
    }


    // xml文本解析辅助函数
    function formatXMLHelper(xmlString) {
        // 使用DOMParser解析XML
        const parser = new DOMParser();
        const xmlDoc = parser.parseFromString(xmlString, "application/xml");
        
        // 检查解析错误
        const parserError = xmlDoc.querySelector("parsererror");
        if (parserError) {
            throw new Error("XML解析错误: " + parserError.textContent);
        }
        
        // 递归格式化节点
        function formatNode(node, indentLevel) {
            const indent = ' '.repeat(indentLevel * 4);
            let output = '';
            
            // 处理元素节点
            if (node.nodeType === Node.ELEMENT_NODE) {
                // 开始标签
                output += `${indent}<${node.tagName}`;
                
                // 添加属性
                for (let i = 0; i < node.attributes.length; i++) {
                    const attr = node.attributes[i];
                    output += ` ${attr.name}="${attr.value}"`;
                }
                
                // 检查子节点
                const childNodes = node.childNodes;
                let hasElementChildren = false;
                let textContent = '';
                
                for (let i = 0; i < childNodes.length; i++) {
                    const child = childNodes[i];
                    if (child.nodeType === Node.ELEMENT_NODE) {
                        hasElementChildren = true;
                    } else if (child.nodeType === Node.TEXT_NODE && child.textContent.trim()) {
                        textContent += child.textContent.trim();
                    }
                }
                
                if (hasElementChildren || textContent) {
                    output += '>\n';
                    
                    // 处理子节点
                    for (let i = 0; i < childNodes.length; i++) {
                        const child = childNodes[i];
                        if (child.nodeType === Node.ELEMENT_NODE || 
                        (child.nodeType === Node.TEXT_NODE && child.textContent.trim())) {
                            output += formatNode(child, indentLevel + 1);
                        }
                    }
                    
                    output += `${indent}</${node.tagName}>\n`;
                } else {
                    output += ' />\n';
                }
            } 
            // 处理文本节点
            else if (node.nodeType === Node.TEXT_NODE) {
                const text = node.textContent.trim();
                if (text) {
                    output += `${indent}${text}\n`;
                }
            }
            // 处理注释节点
            else if (node.nodeType === Node.COMMENT_NODE) {
                output += `${indent}<!--${node.data}-->\n`;
            }
            // 处理文档类型节点
            else if (node.nodeType === Node.DOCUMENT_TYPE_NODE) {
                output += `${indent}<!DOCTYPE ${node.name}>\n`;
            }
            // 处理处理指令节点（如 <?xml ... ?>）
            else if (node.nodeType === Node.PROCESSING_INSTRUCTION_NODE) {
                output += `${indent}<?${node.target} ${node.data}?>\n`;
            }
            
            return output;
        }
        
        // 从文档元素开始格式化
        return formatNode(xmlDoc.documentElement, 0);
    };


    // 添加校验请求Body编辑功能
    const requestBodyField = protocolItem.querySelector('[data-field="request-body"]');
    if (requestBodyField) {
        requestBodyField.style.cursor = 'pointer';
        
        requestBodyField.addEventListener('click', async function(e) {
            if (e.target.closest('.delete-protocol-btn')) return;
            
            e.stopPropagation();
            e.preventDefault();
            
            const loading = showLoading('加载请求体信息...');

            const valueElement = this.querySelector('.value');
            const bodyIndicator = this.querySelector('.body-indicator');
            if (!valueElement || !bodyIndicator) return;
            
            // 发起一次获取Body信息的请求
            // body类型单独获取
            // body数据单独获取
            // 异步处理 等待最终结果
            let body_type;
            let body;
            try {

                const [tmp1, tmp2] = await getProtocolItemBody(protocolItem.id, 1);
                body_type = tmp1;
                body = tmp2;

            } catch(error) {
                console.error('获取协议请求体信息失败: ', error);
                hideLoading(loading);
                alert('获取协议请求体信息失败!');
                return;
            };
            await delay(500);
            hideLoading(loading);

            // 需要根据body_type 预处理一下body数据
            let currentBody = '';
            try {
                if(body && body.length > 0) {

                    if('json' === body_type) {

                        const root = JSON.parse(body);
                        currentBody = JSON.stringify(root, null, 2);
        
                    } else if('xml' == body_type) {
        
                        currentBody = formatXMLHelper(body);
                    }
                }
            } catch(error) {
                console.error('请求体数据解析出错! ', error.message);
                currentBody = body;
            }
            
            console.info("body_type: ", body_type);
            console.info("body: ", currentBody);
            
            const modal = document.createElement('div');
            modal.className = 'modal-overlay';
            modal.innerHTML = `
                <div class="edit-body-modal">
                    <div class="modal-header">
                        <h3>编辑校验请求Body</h3>
                        <button class="close-modal">&times;</button>
                    </div>
                    <div class="modal-body">
                        <div class="form-group">
                            <label>Body类型</label>
                            <select id="body-type">
                                <option value="json">JSON</option>
                                <option value="xml">XML</option>
                            </select>
                        </div>
                        <div class="form-group">
                            <label>Body内容</label>
                            <textarea id="body-content" placeholder="输入校验请求Body内容..." style="min-height: 200px"></textarea>
                        </div>
                        <div class="form-actions">
                            <button type="button" class="cancel-btn">取消</button>
                            <button type="button" class="confirm-btn">确定修改</button>
                        </div>
                    </div>
                </div>
            `;
            
            document.body.appendChild(modal);
            document.getElementById('body-type').value = body_type;
            document.getElementById('body-content').value = currentBody;

            
            // 处理确定按钮
            modal.querySelector('.confirm-btn').addEventListener('click', async function() {
                const newBodyType = document.getElementById('body-type').value;
                const newBody = document.getElementById('body-content').value.trim();

                console.info("newBody.length: ", newBody.length);
                console.info("newBody: ", newBody);

                const ok = await updateProtocolBody(protocolItem.id, 1, 'HTTP', newBodyType, newBody);
                if(!ok) {
                    alert("http修改请求校验Body失败!");
                    return;
                }

                if (newBody) {
                    // requestBody = newBody;

                    valueElement.textContent = '已设置';
                    bodyIndicator.classList.add('has-body');
                    bodyIndicator.classList.remove('no-body');
                } else {

                    // requestBody = '';
                    valueElement.textContent = '未设置';
                    bodyIndicator.classList.remove('has-body');
                    bodyIndicator.classList.add('no-body');
                }

                document.body.removeChild(modal);
            });

            // 关闭模态框
            modal.querySelector('.close-modal').addEventListener('click', function() {
                document.body.removeChild(modal);
            });

            modal.querySelector('.cancel-btn').addEventListener('click', function() {
                document.body.removeChild(modal);
            });
        });
    }

    // 添加目标响应Body编辑功能
    const responseBodyField = protocolItem.querySelector('[data-field="response-body"]');
    if (responseBodyField) {
        responseBodyField.style.cursor = 'pointer';
        
        responseBodyField.addEventListener('click', async function(e) {
            if (e.target.closest('.delete-protocol-btn')) return;
            
            e.stopPropagation();
            e.preventDefault();
            
            const valueElement = this.querySelector('.value');
            const bodyIndicator = this.querySelector('.body-indicator');
            if (!valueElement || !bodyIndicator) return;
            
            const loading = showLoading('加载请求体信息...');
            // 发起一次获取Body信息的请求
            // body类型单独获取
            // body数据单独获取
            // 异步处理 等待最终结果
            let body_type;
            let body;
            try {

                const [tmp1, tmp2] = await getProtocolItemBody(protocolItem.id, 2);
                body_type = tmp1;
                body = tmp2;
                console.info("tmp1: ", tmp1);
                console.info("tmp2: ", tmp2);
            } catch(error) {
                console.error('获取协议请求体信息失败: ', error);
                hideLoading(loading);
                alert('获取协议请求体信息失败!');
                return;
            };
            await delay(500);
            hideLoading(loading);

            // 需要根据body_type 预处理一下body数据
            let currentBody = '';
            try {
                if(body && body.length > 0) {

                    if('json' === body_type) {

                        const root = JSON.parse(body);
                        currentBody = JSON.stringify(root, null, 2);
        
                    } else if('xml' == body_type) {
        
                        currentBody = formatXMLHelper(body);
                    }
                }
            } catch(error) {
                console.error('请求体数据解析出错! ', error.message);
                currentBody = body;
            }
            
            console.info("body_type: ", body_type);
            console.info("body: ", currentBody);

            const modal = document.createElement('div');
            modal.className = 'modal-overlay';
            modal.innerHTML = `
                <div class="edit-body-modal">
                    <div class="modal-header">
                        <h3>编辑目标响应Body</h3>
                        <button class="close-modal">&times;</button>
                    </div>
                    <div class="modal-body">
                        <div class="form-group">
                            <label>Body类型</label>
                            <select id="body-type">
                                <option value="json">JSON</option>
                                <option value="xml">XML</option>
                            </select>
                        </div>
                        <div class="form-group">
                            <label>Body内容</label>
                            <textarea id="body-content" placeholder="输入目标响应Body内容..." style="min-height: 200px"></textarea>
                        </div>
                        <div class="form-actions">
                            <button type="button" class="cancel-btn">取消</button>
                            <button type="button" class="confirm-btn">确定修改</button>
                        </div>
                    </div>
                </div>
            `;
            
            document.body.appendChild(modal);
            document.getElementById('body-type').value = body_type;
            document.getElementById('body-content').value = currentBody;


            // 处理确定按钮
            modal.querySelector('.confirm-btn').addEventListener('click', async function() {
                const newBodyType = document.getElementById('body-type').value;
                const newBody = document.getElementById('body-content').value.trim();
                
                console.info("newBody.length: ", newBody.length);
                console.info("newBody: ", newBody);

                const ok = await updateProtocolBody(protocolItem.id, 2, 'HTTP', newBodyType, newBody);
                if(!ok) {
                    alert("http修改期望响应Body失败!");
                    return;
                }

                if (newBody) {
                    // responseBody = newBody;
                    valueElement.textContent = '已设置';
                    bodyIndicator.classList.add('has-body');
                    bodyIndicator.classList.remove('no-body');
                } else {
                    // responseBody = '';
                    valueElement.textContent = '未设置';
                    bodyIndicator.classList.remove('has-body');
                    bodyIndicator.classList.add('no-body');
                }
                document.body.removeChild(modal);
            });

            // 关闭模态框
            modal.querySelector('.close-modal').addEventListener('click', function() {
                document.body.removeChild(modal);
            });

            modal.querySelector('.cancel-btn').addEventListener('click', function() {
                document.body.removeChild(modal);
            });
        });
    }

    // 为协议项名称添加编辑功能
    const protocolTitle = protocolItem.querySelector('.protocol-name.editable');
    if (protocolTitle) {
        const editControls = document.createElement('div');
        editControls.className = 'title-edit-controls';
        editControls.style.display = 'none';
        editControls.innerHTML = `
            <button class="save-btn">✓</button>
            <button class="cancel-btn">✗</button>
        `;
        protocolTitle.parentNode.appendChild(editControls);

        let originalTitle = protocolTitle.textContent;

        function exitEditMode(save = false) {
            if (!save) {
                protocolTitle.textContent = originalTitle;
            }
            protocolTitle.contentEditable = false;
            editControls.style.display = 'none';
        }

        protocolTitle.addEventListener('click', function(e) {
            e.stopPropagation();
            if (e.target === protocolTitle && !protocolTitle.isContentEditable) {

                originalTitle = protocolTitle.textContent;
                protocolTitle.contentEditable = true;
                protocolTitle.focus();
                editControls.style.display = 'flex';
            }
        });

        editControls.querySelector('.save-btn').addEventListener('click', async function(e) {
            e.stopPropagation();

            const ok = await updateProtocolName(protocolItem.id, protocolTitle.textContent);
            if(ok) {
                alert('标题编辑成功！');
            } else {
                protocolTitle.textContent = originalTitle;
                alert('标题编辑失败!');
            }

            exitEditMode(true);
        });

        editControls.querySelector('.cancel-btn').addEventListener('click', function(e) {
            e.stopPropagation();
            exitEditMode(false);
        });

        document.addEventListener('click', function(e) {
            if (protocolTitle.isContentEditable && !protocolTitle.contains(e.target) && !editControls.contains(e.target)) {
                exitEditMode(false);
            }
        });
    }
}


// 获取协议项列表
async function getProtocolList(project_id, offset = 0, limit = 5) {

    try {
        /***********DEBUG******** */
        let result;
        if(DEBUG_MODE) {

            result = JSON.parse(`{
                "code": 0,
                "message": "success",
                "data":[
                {
                    "ctime": "2025-08-25 08:01:21",
                    "id": 2,
                    "name": "TCP测试协议",
                    "length_type": 1,
                    "head_length": 30,
                    "data_length_pos": 8,
                    "data_length_size": 4,
                    "message_type_pos": 10,
                    "message_type_size": 4,
                    "message_type_value": 2,
                    "project_id": ${project_id},
                    "req_body_status": 0,
                    "req_body_type": "json",
                    "resp_body_status": 0,
                    "resp_body_type": "json",
                    "status_code": 200,
                    "type": "TCP",
                    "utime": "2025-08-25 08:01:21"
                },
                {
                    "ctime": "2025-08-25 08:01:21",
                    "id": 1,
                    "method": "POST",
                    "name": "HTTP测试协议1",
                    "path": "/test1/hello",
                    "project_id": ${project_id},
                    "req_body_status": 1,
                    "req_body_type": "json",
                    "resp_body_status": 1,
                    "resp_body_type": "json",
                    "status_code": 200,
                    "type": "HTTP",
                    "utime": "2025-08-25 08:01:21"
                }
            ]}`);
            // 模拟延时
            await delay(1000);

        } else {
            const response = await fetch('/protocols/list', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({
                    project_id: project_id,
                    offset: offset,
                    limit: limit
                })
            });
    
    
            result = await response.json();

                    
            if (!response.ok || result.code !== 0) {
                throw new Error(result.message || '获取协议项列表请求失败');
            }
        
        }

        console.log('获取协议项列表请求成功:', {
            code: result.code,
            message: result.message,
            data: result.data
        });
        
        return result.data;
    } catch (error) {
        console.error('获取协议项列表请求出错:', error);
        throw error;
    }
}


// 获取单个协议项请求
async function getProtocolReq(protocol_id) {
    // 发Token
    // DEBUG: 固定发user_id
    try {
        const response = await fetch('/protocols/' + String(protocol_id), {
            method: 'GET'
        });

        const result = await response.json();
        
        if (!response.ok || result.code !== 0) {
            throw new Error(result.message || '获取单个协议项请求失败');
        }

        if(!Array.isArray(result.data) || result.data.length <= 0) {
            throw new Error('获取单个协议项请求为null');
        }
 
        console.log('获取单个协议项请求成功:', {
            code: result.code,
            message: result.message,
            data: result.data
        });

        return result.data;
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
        root = JSON.parse(body_data || ''); // TODO 校验放在输入阶段
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
        const formData = new FormData();
        // formData.append("protocol_cfg_header", JSON.stringify({
        //     "name": protocol.name,
        //     "type": protocol.type,
        //     "project_id": protocol.project_id,
        //     "req_body_type": protocol.req_body_type,
        //     "resp_body_type": protocol.resp_body_type,

        // }));
        formData.append("protocol_cfg_header", JSON.stringify(protocol.cfg_header));
        formData.append("protocol_req_cfg", JSON.stringify(protocol.req_cfg));
        formData.append("protocol_resp_cfg", JSON.stringify(protocol.resp_cfg));


        // 1为reqeust body  2为response body
        formBodyDataHeler(formData, 1,  protocol.cfg_header.req_body_type, protocol.request_body);
        formBodyDataHeler(formData, 2,  protocol.cfg_header.resp_body_type, protocol.response_body);
        
        for (const [key, value] of formData.entries()) {
            console.info(key, ' : ', value);
        }
        /*********DEBUG*********/
        if(!DEBUG_MODE) {

        const response = await fetch('/protocols/add', {
            method: 'POST',
            body: formData,

        });
        /*********DEBUG*********/

        const result = await response.json();

        

        if (!response.ok || result.code !== 0) {
            throw new Error('code: '+ result.code  + ",msg:" + result.message);
        }
        
        console.log('协议项添加请求成功:', {
            code: result.code,
            message: result.message,
            data: result.data
        });

        return result.data;
        }

        return {protocol_id: 1};
    } catch(error) {
        console.error('添加协议项请求失败:', error);

        throw error;
    }

}


// 添加HTTP协议项
async function  addHTTPProtocol(serviceCard, submit_protocol) {

    const loading = showLoading();
    let protocols;
    try {

        // 1. 先添加服务
        const addResult = await addHTTPProtocolReq(submit_protocol);
        console.info('添加完成的单个协议项id:', addResult.protocol_id);
        
        if(!DEBUG_MODE) {

        // 2. 再获取单个项
        protocols = await getProtocolReq(addResult.protocol_id);
        console.info('获取的单个协议项:', protocols);

        }else {
            protocols = JSON.parse(`[{
                "ctime": "2025-08-25 08:01:21",
                "id": 9,
                "method": "POST",
                "name": "HTTP测试协议5555",
                "path": "/test3/hello",
                "project_id": 2,
                "req_body_status": 1,
                "req_body_type": "json",
                "resp_body_status": 1,
                "resp_body_type": "json",
                "status_code": 200,
                "type": "HTTP",
                "utime": "2025-08-25 08:01:21"
            }]`);
        }

    } catch (error) {
        console.error('添加测试协议项失败: ', error);
        hideLoading(loading);
        alert('添加测试协议项失败!');
        return;
    }

    await delay(1000);
    hideLoading(loading);
    alert("添加测试协议项成功!");
    // 3. 添加到页面显示
    addProtocolItem(serviceCard, protocols[0], 1);

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
                                <option value=3>HTTPS</option>
                            </select>
                        </div>

                        <div class="form-group">
                            <label for="service-mode">测试模式</label>
                            <select id="service-mode" required>
                                <option value="">请选择测试模式</option>
                                <option value=1>服务器模式</option>
                                <option value=2>客户端模式</option>
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


async function updateProjectNameReq(project_id, name) {

    try {

        const response = await fetch('/projects/' + project_id + "/name", {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                name: name,
            })

        });

        const result = await response.json();
        
        if (!response.ok || result.code !== 0) {
            throw new Error('code: '+ result.code  + ",msg:" + result.message);
        }
        
        console.log('修改测试服务标题请求成功:', {
            code: result.code,
            message: result.message,
            data: result.data
        });

        return;

    } catch (error) {
        console.error('修改测试服务标题请求失败:', error.String());
        throw error;
    }

}

async function updateProjectName(id_str, tilte_name) {
    const serviceCardId = ExtractId(id_str)

    const serviceCard = document.getElementById(id_str);
    let projects;

    const loading = showLoading();

    try {

        if(!Number.isInteger(serviceCardId) || serviceCardId <= 0) {
            throw new Error("无效的测试服务ID");
        }
        if(!DEBUG_MODE) {
        await updateProjectNameReq(serviceCardId, tilte_name);

        
        projects = await getProjectReq(serviceCardId);
        
        } else {
            projects = JSON.parse(`[{
                "ctime": "2025-08-11 07:55:27",
                "id": 2,
                "listen_port": 12345,
                "protocol_type": 1,
                "mode": 2,
                "name": "${tilte_name}",
                "protocol_cnt": 10,
                "status": 0,
                "target_ip": "192.168.1.1:6666",
                "user_id": 1
            }]`);
        }
        // 动态更新页面数据
        updateServiceCard(id_str, projects[0]) 

    } catch(error) {
        console.error('修改测试服务标题失败:', error);
        hideLoading(loading);
        return false;
    } 


    await delay(500);
    hideLoading(loading);
    return true;
}

// 添加测试服务 http请求
async function addProjectReq(project) {
    
    try {

        console.info('addProjectReq body: ', project);

        const response = await fetch('/projects/add', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(project)
        });

        const result = await response.json();
        
        if (!response.ok || result.code !== 0) {
            throw new Error('code: '+ result.code  + ",msg:" + result.message);
        }
        
        console.log('服务添加成功:', {
            code: result.code,
            message: result.message,
            data: result.data
        });

        return result.data;

    } catch (error) {
        console.error('添加服务请求出错:', error.String());
        throw error;
    }
}

// 获取项目列表
async function getProjectList(offset, limit) {
    try {
        /***********DEBUG******** */
        let result;
        if(DEBUG_MODE) {

            result = JSON.parse(`{
                "code": 0,
                "message": "success",
                "data":[
                {
                    "id": 2,
                    "listen_port": 2222,
                    "protocol_type": 2,
                    "mode": 1,
                    "name": "test2222",
                    "status": 0,
                    "target_ip": "",
                    "user_id": 1,
                    "pattern_type": 1,
                    "ctime": "2025-08-11 07:55:27"
                },
                {

                    "id": 1,
                    "listen_port": 1111,
                    "protocol_type": 1,
                    "mode": 1,
                    "name": "test1111",
                    "status": 0,
                    "target_ip": "",
                    "user_id": 1,
                    "pattern_type": 0,
                    "ctime": "2025-08-11 07:55:15"
                }
            ]}`);
            
        } else {
            const response = await fetch('/projects/list', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({
                    offset: 0,
                    limit: 5
                })
            });
    
    
            result = await response.json();

                    
            if (!response.ok || Number(result.code) !== 0) {
                throw new Error(result.message || '获取测试服务列表失败');
            }
        
        }

        console.log('获取测试服务列表成功:', {
            code: result.code,
            message: result.message,
            data: result.data
        });
        
        return result.data;
    } catch (error) {
        console.error('获取项目列表出错:', error);
        throw error;
    }
}

// 获取单个测试服务 http请求
async function getProjectReq(project_id) {

    // 发Token
    // DEBUG: 固定发user_id
    try {
        const response = await fetch('/projects/' + String(project_id), {
            method: 'GET'
        });

        const result = await response.json();
        
        if (!response.ok || Number(result.code) !== 0) {
            throw new Error(result.message || '获取单个测试服务请求失败');
        }

        if(!Array.isArray(result.data) || result.data.length <= 0) {
            throw new Error('获取单个测试服务请求为null');
        }
        console.log("result.data.length: ", result.data.length);
        
        console.log('获取单个测试服务请求成功:', {
            code: result.code,
            message: result.message,
            data: result.data
        });

        return result.data;
    } catch (error) {
        console.error('获取单个测试服务请求失败:', error);
        throw error;
    }
}

// 删除单个测试服务 http请求
async function delProtocolReq(protocol_id, project_id) {

    try {
        /********DEBUG******* */
        if(!DEBUG_MODE) {

        const response = await fetch('/protocols/del', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                'id': protocol_id,
                'project_id': project_id
            })
        });

        const result = await response.json();
        
        if (!response.ok || result.code !== 0) {
            throw new Error('code: '+ result.code  + ",msg:" + result.message);
        }
        
        console.log('删除协议项成功:', {
            code: result.code,
            message: result.message,
            data: result.data
        });

        }
        /********DEBUG******* */

    } catch (error) {
        console.error('删除协议项请求出错:', error);
        throw error;
    }
}

async function delProtocol(id_str) {
    // 获取当前测试服务卡片ID
    const protocolItemId = ExtractId(id_str);
    const protocolItem = document.getElementById(id_str);
    const serviceCardId = ExtractId(protocolItem.projectId);

    const loading = showLoading();

    try {
        if(!Number.isInteger(protocolItemId) || protocolItemId <= 0) {
            throw new Error("无效的协议项ID");
        }
        
        await delProtocolReq(protocolItemId, serviceCardId)

    } catch(error) {
        console.error('删除协议项失败:', error);
        hideLoading(loading);
        return false;
    } 

    await delay(500);
    hideLoading(loading);
    return true;

}



// 删除单个测试服务 http请求
async function delProjectReq(project_id) {

    try {
        /********DEBUG******* */
        if(!DEBUG_MODE) {

        const response = await fetch('/projects/' + String(project_id), {
            method: 'DELETE'
        });

        const result = await response.json();
        
        if (!response.ok || result.code !== 0) {
            throw new Error('code: '+ result.code  + ",msg:" + result.message);
        }
        
        console.log('删除测试服务成功:', {
            code: result.code,
            message: result.message,
            data: result.data
        });

        }
        /********DEBUG******* */

        return true;
    } catch (error) {
        console.error('删除测试服务请求出错:', error);
        throw error;
    }
}

async function delProject(id_str) {
    // 获取当前测试服务卡片ID
    let str = id_str.slice(id_str.indexOf("-") + 1);
    const serviceCardId = Number(str.slice(str.indexOf("-") + 1));

    const serviceCard = document.getElementById(id_str);


    const loading = showLoading();

    try {
        if(!Number.isInteger(serviceCardId) || serviceCardId <= 0) {
            throw new Error("无效的测试服务ID");
        }
        
        await delProjectReq(serviceCardId)


    } catch(error) {
        console.error('删除测试服务失败:', error);
        hideLoading(loading);
        return false;
    } 

    await delay(500);
    hideLoading(loading);
    return true;

}

async function addProject(project) {

    const loading = showLoading();

    try {
        // 1. 先添加服务
        const addResult = await addProjectReq(project);
        console.info('添加完成的单个项目id:', addResult.project_id);

        // 2. 再获取单个项
        project = await getProjectReq(addResult.project_id);
        console.info('获取的单个项目:', project);

    } catch (error) {
        console.error('添加测试服务失败: ', error);
        hideLoading(loading);
        alert('添加测试服务失败!');
        return;
    }

    await delay(1000);
    hideLoading(loading);
    alert("添加测试服务成功!");
    // 3. 添加到页面显示
    addServiceCard(project[0], 1); // 默认插入到头部
    
}


// 组装卡片页面
function serviceCardHTML(id, name, protocol, port, mode, pattern_type, status = false) {


    console.info(id, name, protocol, port, mode, pattern_type, status);

return `<div class="service-header">
            <h3 class="service-title editable" data-default="Undef默认测试服务">${name || `默认测试服务${id}`}</h3>
            <div class="title-edit-controls" style="display:none">
                <button class="save-btn">✓</button>
                <button class="cancel-btn">✗</button>
            </div>
            <span class="toggle-icon">▼</span>
        </div>
            <div class="service-form">
            <div class="service-fields-container">
                <div class="service-field project-protocol-type" data-protocol_type='${protocol}'>
                    <span class="field-label">协议种类</span>
                    <span class="field-value">${ProtocolTypeStr[protocol]}</span>
                </div>
                <div class="service-field project-mode">
                    <span class="field-label">测试模式</span>
                    <span class="field-value">${ProjectModeStr[mode]}</span>
                </div>
                <div class="service-field project-${mode === ProjectMode.SERVER ? 'listen-port' : 'target-ip'}">
                    <span class="field-label">${mode === ProjectMode.SERVER ? '监听端口' : '目标IP/端口'}</span>
                    <span class="field-value">${port || '未设置'}</span>
                </div>
                <div class="service-field project-protocol-cnt" style="display: none;">
                <span class="field-label">协议数量</span>
                <span class="field-value">0</span>
                </div>
                <div class="service-field project-status">
                    <span class="field-label">服务状态</span>
                    <span class="field-value status ${status ? 'status-active' : 'status-inactive'}">
                        ${status ? '开启' : '未开启'}
                    </span>
                </div>
                ${ProtocolType.CUSTOM_TCP === protocol ?
                `<div class="service-field project-pattern" id="pattern-info-${id}" title="该信息点击可编辑">
                    <span class="field-label">格式信息</span>
                    <span class="field-value" data-target="pattern-info-${id}">${PatternTypeStr[pattern_type]}</span>
                </div>`: ``
                }
            </div>
            <div class="service-actions-container">
                <button class="add-protocol-btn">+ 添加协议项</button>
                <button class="delete-service-btn">删除</button>
            </div>
        </div>
        <div class="protocol-container">
            <div class="protocol-management">
                <div class="protocol-list"></div>
            </div>
        </div>`
}



// 动态更新页面数据
function updateServiceCard(id_str, project) {

    // 字段转换
    let mode_str;
    let protocol_str;

    console.info('updateServiceCard: ', project.id, project.name, project.protocol_type, project.listen_port, project.mode, project.status);


    if(1 === project.mode) {
        mode_str = '服务器模式'
    }else if (2 === project.mode) {
        mode_str = '客户端模式'
    } else {
        mode_str = '未知模式'
    }

    if(1 === project.protocol_type) {
        protocol_str = 'HTTP'
    }else if(2 ===  project.protocol_type) {
        protocol_str = '自定义TCP'
    } else if(3 ===  project.protocol_type){
        protocol_str = 'HTTPS'
    } else {
        protocol_str = "未知协议"
    }

    const serviceCard = document.getElementById(id_str);

    serviceCard.querySelector(".service-title").textContent = project.name;

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

    serviceCard.querySelector(".project-status .field-value").textContent = project.status === 1 ? '开启' : '未开启';
    serviceCard.querySelector(".project-status .field-value").className = `field-value status ${project.status === 1 ? 'status-active' : 'status-inactive'}`;
    
}

// 动态生成测试服务卡片页面 + 事件监听
function addServiceCard(project, pos = -1) {

    const serviceCard = document.createElement('div');
    serviceCard.className = 'service-card';
    serviceCard.id = String("service-card-" + project.id);

    serviceCard.innerHTML = serviceCardHTML(project.id, project.name, project.protocol_type, project.listen_port, project.mode, project.pattern_type, project.status);

    // 点击三角图标展开/折叠
    const toggleIcon = serviceCard.querySelector('.toggle-icon');
    toggleIcon.addEventListener('click', function(e) {
        e.stopPropagation();
        serviceCard.classList.toggle('expanded');
        toggleIcon.textContent = serviceCard.classList.contains('expanded') ? '▲' : '▼';
    });

    /* 标题编辑功能 */
    const titleElement = serviceCard.querySelector('.service-title');
    const editControls = serviceCard.querySelector('.title-edit-controls');
    let originalTitle = titleElement.textContent;

    function exitEditMode(save = false) {
        if (!save) {
            titleElement.textContent = originalTitle;
        }
        titleElement.contentEditable = false;
        editControls.style.display = 'none';
    }

    titleElement.addEventListener('click', function(e) {
        e.stopPropagation();
        if (e.target === titleElement && !titleElement.isContentEditable) {
            originalTitle = titleElement.textContent;
            titleElement.contentEditable = true;
            titleElement.focus();
            editControls.style.display = 'flex';
        }
    });

    document.addEventListener('click', function(e) {
        if (titleElement.isContentEditable && !titleElement.contains(e.target) && !editControls.contains(e.target)) {
            exitEditMode(false);
        }
    });

    serviceCard.querySelector('.save-btn').addEventListener('click', async function(e) {
        e.stopPropagation();

        const ok = await updateProjectName(serviceCard.id, titleElement.textContent)
        if(ok) {
            alert('标题编辑成功！');
        } else {
            titleElement.textContent = originalTitle;
            alert('标题编辑失败!');
        }

        exitEditMode(true);
    });

    serviceCard.querySelector('.cancel-btn').addEventListener('click', function(e) {
        e.stopPropagation();
        exitEditMode(false);
    });

    // 阻止点击标题时展开卡片
    titleElement.addEventListener('click', function(e) {
        e.stopPropagation();
    });
    /* 标题编辑功能 */

    /* TCP协议下多一个格式编辑*/
    if(ProtocolType.CUSTOM_TCP == project.protocol_type) {
        
        serviceCard.querySelector('.project-pattern').addEventListener('click', async function(e) {
            e.preventDefault();
            e.stopPropagation();

            // 单开一个模态框
            const modal = document.createElement('div');
            modal.className = 'modal-overlay';
            modal.innerHTML = `
            <div class="tcp-pattern-change-modal">
                <div class="modal-header">
                    <h3>TCP格式修改</h3>
                    <button class="close-modal">&times;</button>
                </div>
                <div class="modal-body">
                    <form id="tcp-pattern-change-form">

                    </form>
                </div>
            `
            document.body.appendChild(modal);

            const loading = showLoading('加载格式字段信息...');

            let inputPatternInfosMap = new Map;
            let curPatternInfosMap;
            const headerIndicator = this.querySelector('.header-fields-indicator');
            const valueElement = this.querySelector('.value');

            // 注意: 这里不需要对字段进行缓存
            try {
                // 每次点击 都去获取一下所有的字段信息?? 
                if(0) {

                    curPatternInfosMap = await getPatternFields(protocol.id, req_or_resp);

                }
                else {
                    /** 调试写法 **/
                    const specialFields = JSON.parse(`{ 
                        "start_magic_num_field": {
                            "name": "起始标识",
                            "idx": 0,
                            "byte_pos": 0,
                            "byte_len": 4,
                            "type": "INT32",
                            "value": "H23232323"
                        },

                        "body_length_field": {
                            "name": "报文体长度",
                            "idx": 4,
                            "byte_pos": 14,
                            "byte_len": 4,
                            "type": "UINT32",
                            "value": ""
                        },
                        "function_code_field": {
                            "name": "功能码",
                            "idx": 3,
                            "byte_pos": 12,
                            "byte_len": 2,
                            "type": "UINT16",
                            "value": ""
                        }
                    }`);
                    let commonFields = [];
                    if(req_or_resp === 1) {
                        commonFields = JSON.parse(`[
                            {"name":"消息总长度","idx":1,"byte_pos":4,"byte_len":4,"type":"UINT32","value":"H0209"},{"name":"消息序列号","idx":2,"byte_pos":8,"byte_len":4,"type":"UINT32","value":"H0003"},{"name":"消息时间戳","idx":5,"byte_pos":18,"byte_len":8,"type":"UINT64","value":""}
                        ]`);
                    } else if(req_or_resp === 2) {
                        commonFields = JSON.parse(`[
                            {"name":"消息总长度","idx":1,"byte_pos":4,"byte_len":4,"type":"UINT32","value":"H02090000"},{"name":"消息序列号","idx":2,"byte_pos":8,"byte_len":4,"type":"UINT32","value":"H00000003"},{"name":"消息时间戳","idx":5,"byte_pos":18,"byte_len":8,"type":"UINT64","value":"HA86D9F9F9A010000"}
                        ]`);
                    }


                    res = JSON.stringify({
                        special_fields: specialFields,
                        common_fields: commonFields,
                    });
                    /** 调试写法 **/
                    curPatternInfosMap = JSON.parse(res);

                }

            } catch (error) {
                console.error('获取格式字段信息失败: ', error);
                hideLoading(loading);
                alert('获取格式字段信息失败!');
                return;
            }
            await delay(200);
            hideLoading(loading);


            console.log('curPatternInfosMap:: ', curPatternInfosMap);

            // 注意: 对外发送的报文不需要特殊字段 收到的报文才需要
            // if(1 === req_or_resp) {

                const specialFieldsTmp = curPatternInfosMap["special_fields"];
                if(specialFieldsTmp) {
                    inputPatternInfosMap["special_fields"] = {};
                    Object.keys(specialFieldsTmp).forEach(key => {
                        inputPatternInfosMap["special_fields"][key]  = createPatternField('', key, specialFieldsTmp[key]);
                    });
                }
            // }

            const commonFieldsTmp = curPatternInfosMap["common_fields"];
            if(commonFieldsTmp) {
                inputPatternInfosMap["common_fields"] = [];
                commonFieldsTmp.forEach(item => {
                    inputPatternInfosMap["common_fields"].push(createPatternField('', '', item));
                });
            }

            console.log('inputPatternInfosMap: ', inputPatternInfosMap);

            createCustomTcpPatternModal(field, title, inputPatternInfosMap, null, false, async function(patternFieldInfos) {
                
                // 删除缓存
                delete field.dataset.patternInfos;
                
                console.log(curPatternInfosMap, ',', patternFieldInfos);

                // 没有改动则直接返回
                if(JSON.stringify(curPatternInfosMap["common_fields"]) === JSON.stringify(patternFieldInfos["common_fields"])
                ) {
                    console.log('字段信息没有发生变化!');
                    return;
                }

                const loading = showLoading();

                const ok = await updateProtocolCfg(protocol.id, req_or_resp, 'common_fields', patternFieldInfos["common_fields"]);

                if (!ok) {
                    alert("字段修改失败!");
                    return;
                }

                if (patternFieldInfos["common_fields"].length > 0) {
        
                    valueElement.textContent = '已设置';
                    headerIndicator.classList.add('has');
                    headerIndicator.classList.remove('no');

                } else {
                    valueElement.textContent = '未设置';
                    headerIndicator.classList.remove('has');
                    headerIndicator.classList.add('no');
                }

                await delay(200);
                hideLoading(loading);

            });
        });
    
    }

    /* TCP协议下多一个格式编辑*/

    /* 添加删除服务卡片功能 */
    const deleteBtn = serviceCard.querySelector('.delete-service-btn');
    if (deleteBtn) {
        deleteBtn.addEventListener('click', async function(e) {
            e.stopPropagation();
            if (confirm('确定要删除这个测试服务吗？')) {

                const ok = await delProject(serviceCard.id);
                if(true === ok) {
                            
                    serviceCard.remove();
                    checkEmptyState();

                    alert('删除测试服务成功!');
                } else {
                    alert('删除测试服务失败!');
                }
            }
        });
    }
    /* 添加删除服务卡片功能 */

    /* 添加协议项 */
    const addProtocolBtn = serviceCard.querySelector('.add-protocol-btn');
    if (addProtocolBtn) {
        addProtocolBtn.addEventListener('click', function(e) {
            e.stopPropagation();

            // 根据不同协议类型生成不同的框
            const modal = createAddProtocolModal(ProtocolTypeFunc[project.protocol_type], serviceCard);

            document.body.appendChild(modal);


        });
    }
    /* 添加协议项 */

    // 选择添加位置
    const serviceCards = document.querySelector('.service-cards');
    if(-1 === pos) {
        serviceCards.appendChild(serviceCard);
    } else if(pos >= 1) {
        serviceCards.insertBefore(serviceCard, serviceCards.childNodes[pos]);
    }

    return serviceCard;
}


// 刷新所有页面状态
function refreshServiceCards(projects) {

    projects.forEach(project => {

        const serviceCard = addServiceCard(project);

        // 异步请求每个服务下的协议项
        getProtocolList(project.id)
        .then(protocols => {

            if(!Array.isArray(protocols) || protocols.length <= 0) {
                throw new Error("协议项列表数据格式错误!");
            }

            protocols.forEach(protocol => {
                console.info('projectId= ', protocol.project_id, '获取的协议项:', protocol);

                addProtocolItem(serviceCard, protocol, 1);
            });

        })
        .catch(error => {
            console.error(project.m_name, ' 加载协议项出错: ', error);
            // alert(project.m_name + '加载协议项失败!');
        })

    });

}

// 加载当前页全部测试服务数据
async function loadAllProjects() {
    const loading = showLoading("正在加载测试服务列表...");

    try {
        // 获取测试服务列表
        const projects = await getProjectList();
        if(!Array.isArray(projects)) {
            throw new Error("数据格式错误");
        }

        console.info('获取的项目列表:', projects);

        // TODO 同步查协议数
        // 这里不能全量加载 开辟新的页面 去另外一个页面上加载协议项目
        // TODO 获取每个列表对应的协议项
        // 同步查询
        
        // 更新页面显示
        refreshServiceCards(projects);
        checkEmptyState();

    } catch(error) {
        console.error('加载服务列表出错:', error);
        alert('加载服务列表出错： ' + error.message);

    }finally{
        await delay(1000);
        hideLoading(loading);
    }

}





// 页面加载入口
document.addEventListener('DOMContentLoaded', function() {

    // 监听添加服务按钮点击
    document.getElementById('add-service')?.addEventListener('click',  async function() {

        const modal = document.createElement('div');
        modal.className = 'modal-overlay';
        modal.innerHTML = addNewServiceCardModalHTML();
        // 暂不考虑 模态框不需要重新再去获取
        // const modal = await loadModal('add_protocol_service.html');

        document.body.appendChild(modal);

        // 动态添加端口控件
        const serviceModeSelect = document.getElementById('service-mode');
        const formContainer = document.querySelector('.form-columns-container');
        
        if (serviceModeSelect && formContainer) {

            serviceModeSelect.addEventListener('change', function() {
                // 移除现有的端口控件
                const existingPortControl = document.querySelector('.port-control');
                if (existingPortControl) {
                    formContainer.removeChild(existingPortControl);
                }

                const mode = Number(this.value);
                // 服务器模式
                if (mode === ProjectMode.SERVER) {
                    // 添加监听端口控件
                    const portControl = document.createElement('div');
                    portControl.className = 'form-group port-control';
                    portControl.innerHTML = `
                        <label for="service-port">监听端口</label>
                        <input type="number" id="service-port" min="1" max="65535" placeholder="1-65535" required>
                    `;
                    formContainer.appendChild(portControl);
                } 
                else if (mode === ProjectMode.CLIENT) { // 客户端模式
                    // 添加目标IP/端口控件
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

                    // 添加输入校验
                    const addressInput = document.getElementById('target-address');
                    const errorMessage = portControl.querySelector('.error-message');
                    
                    addressInput.addEventListener('input', function() {
                        const ipPortRegex = /^((25[0-5]|2[0-4]\d|[01]?\d\d?)\.){3}(25[0-5]|2[0-4]\d|[01]?\d\d?):([1-9]\d{0,3}|[1-5]\d{4}|6[0-4]\d{3}|65[0-4]\d{2}|655[0-2]\d|6553[0-5])$/;
                        
                        if (!ipPortRegex.test(this.value)) {
                            errorMessage.textContent = '请输入有效的IP:端口格式，如：192.168.1.1:8080';
                            errorMessage.style.display = 'block';
                            this.setCustomValidity('IP地址或端口号格式不正确');
                        } else {
                            errorMessage.style.display = 'none';
                            this.setCustomValidity('');
                        }
                    });
                }
            });
        }


        const protocolTypeSelect = document.getElementById('protocol-type');
        if(protocolTypeSelect) {
            protocolTypeSelect.addEventListener('change', function() {
                const type = Number(this.value);
                console.info('选择的协议类型: ', type);

                // 移除现有的格式控件
                const existingPatternControl = document.querySelector('.pattern-control');
                if (existingPatternControl) {
                    formContainer.removeChild(existingPatternControl);
                }

                // TCP协议需要增加格式选项
                if(ProtocolType.CUSTOM_TCP === type) {

                    const patternControl = document.createElement('div');
                    patternControl.className = 'form-group pattern-control';
                    patternControl.innerHTML = `
                        <div class="pattern-header">
                            <label for="pattern-type">格式类型</label>
                            <scan class="import-status" id="first-pattern-import-status" style="display: none">格式已设置</scan>
                        </div>
                        <div class="pattern-container">
                            <select id="pattern-type" required>
                                <option value="">请选择格式类型</option>
                                <option value=1>${PatternTypeStr[1]}</option>
                                <option value=2>${PatternTypeStr[2]}</option>
                                <option value=3>${PatternTypeStr[3]}</option>
                            </select>
                            <button type="button" class="pattern-config-btn" id="pattern-infos" data-target="pattern-infos">配置</button>
                        </div>
                    `;

                    patternControl.querySelector('.pattern-config-btn').disabled = true;
                    
                    formContainer.appendChild(patternControl);

                    let prePatternTypeVal = 0;
                    document.getElementById('pattern-type').addEventListener('change', function() {
                        const type = Number(this.value);
    
                        const targetId = document.getElementById('pattern-infos').getAttribute('data-target');
                        const targetField = document.getElementById(targetId);

                        if(prePatternTypeVal != type && targetField.dataset.patternInfos) {

                            if(!confirm('格式已配置, 切换会清空，是否继续?')) {
                                this.value = String(prePatternTypeVal);
                                return;
                            }

                            delete targetField.dataset.patternInfos;
                            document.getElementById('pattern-import-status').style.display = 'none';

                        }

                        prePatternTypeVal = type;
                        if(0 === type) {
                            patternControl.querySelector('.pattern-config-btn').disabled = true;
                        } else {
                            patternControl.querySelector('.pattern-config-btn').disabled = false;
                        }

                    });
            
          
                    // 处理配置按钮点击
                    patternControl.querySelector('.pattern-config-btn').addEventListener('click', function() {

                        const tcpPatternType = Number(document.getElementById('pattern-type').value);
                        const targetId = this.getAttribute('data-target');
                        // const cachedPatternInfos = document.getElementById(targetId).dataset.patternInfos;
                        let inputPatternInfosMap = {};

                        const cachedPatternInfos = `{"least_byte_len":26,"special_fields":{"start_magic_num_field":{"name":"起始标识","idx":0,"byte_pos":0,"byte_len":4,"type":"INT32","value":"H23232323"},"function_code_field":{"name":"功能码","idx":3,"byte_pos":12,"byte_len":2,"type":"UINT16","value":""},"body_length_field":{"name":"报文体长度","idx":4,"byte_pos":14,"byte_len":4,"type":"UINT32","value":""}}}`;

                        // 先看是否已设置过 读取缓存
                        if(!cachedPatternInfos) {

                            inputPatternInfosMap = {
                                "least_byte_len": 0,
                                "special_fields":{
                                    // 起始魔数字
                                    "start_magic_num_field": createPatternField("起始魔数字", "start-magic-num-field"),
                                    // 功能码
                                    "function_code_field": createPatternField("功能码", "function-code-field"),
                                }
                            };

                            switch (tcpPatternType) {
                                case PatternType.BODY_LENGTH_DEP:
                                    inputPatternInfosMap["special_fields"]["body_length_field"] = createPatternField("Body长度", "body-length-field");
                                    break;
                                case PatternType.TOTAL_LENGTH_DEP:
                                    inputPatternInfosMap["special_fields"]["total_length_field"] = createPatternField("Body长度", "total-length-field");
                                    break;
                                default:
                                    break;
                            }
                        } else {

                            const tmp = JSON.parse(cachedPatternInfos);
                            inputPatternInfosMap["least_byte_len"] = tmp["least_byte_len"];
                            
                            
                            const specialFieldsTmp = tmp["special_fields"];
                            if(specialFieldsTmp) {
                                inputPatternInfosMap["special_fields"] = {};
                                Object.keys(specialFieldsTmp).forEach(key => {
                                    inputPatternInfosMap["special_fields"][key] =  createPatternField('', key, specialFieldsTmp[key]);
                                });
                            }
    
                        }
                        

                        console.log('specialPatternFieldMap: ', inputPatternInfosMap);
                        createCustomTcpPatternModal(targetId, '头部特殊字段', inputPatternInfosMap, 'first-pattern-import-status', true);

                    });

                }

            });
        }

        // 处理表单提交
        modal.querySelector('#add-service-form').addEventListener('submit', function(e) {
            e.preventDefault();

            const serviceName = document.getElementById('service-name').value;
            const protocolType = Number(document.getElementById('protocol-type').value);
            const serviceMode = Number(document.getElementById('service-mode').value);
            let servicePort = 0;
            let target_ip = '';
            // TODO: 格式信息怎么获取
            let pattern_type = PatternType.STANDARD;
            let pattern_info = {};

            if (serviceMode === ProjectMode.SERVER) {
                const portInput = document.getElementById('service-port');
                servicePort = portInput ? Number(portInput.value) : 0;

            } else if (serviceMode === ProjectMode.CLIENT) {
                const addressInput = document.getElementById('target-address');

                if (!addressInput || !addressInput.reportValidity()) {
                    return; // 阻止表单提交
                }
                target_ip = addressInput.value;
            }

            // TCP模式需要带格式
            if(protocolType === ProtocolType.CUSTOM_TCP) {
                //TODO  从表格中获取格式信息
                pattern_type = Number(document.getElementById('pattern-type').value)
                const cachedPatternInfos = document.getElementById('pattern-infos').dataset.patternInfos;
                
                if (!cachedPatternInfos) {
                    alert('格式类型具体内容未配置，请检查!');
                    return; // 阻止表单提交
                }
                const tmp = JSON.parse(cachedPatternInfos);
                pattern_info =  {
                    "least_byte_len":  tmp["least_byte_len"],
                    "special_fields":  tmp["special_fields"],
                };
            }


            // TODO 服务器卡片和客户端卡片不一样
            addProject({
                name: serviceName,
                mode: serviceMode,
                protocol_type: protocolType,
                listen_port: servicePort,
                target_ip: target_ip,
                pattern_type: pattern_type,
                pattern_info: pattern_info,
            });

            checkEmptyState();
            document.body.removeChild(modal);

        });

        // 关闭模态框
        modal.querySelector('.close-modal').addEventListener('click', function() {
            document.body.removeChild(modal);
        });

        modal.querySelector('.cancel-btn').addEventListener('click', function() {
            document.body.removeChild(modal);
        });
    });

    loadAllProjects();

    // checkEmptyState();
});

function updatePatternField(field_node, field_info, is_special = false) {

    field_node.querySelector(".pattern-field-name").value = field_info["name"];
    field_node.querySelector(".pattern-field-idx").value = field_info["idx"];
    field_node.querySelector(".pattern-field-byte-len").value = field_info["byte_len"];
    if(is_special) {
        field_node.querySelector(".pattern-field-byte-pos").value = field_info["byte_pos"];
    }
    field_node.querySelector(".pattern-field-type").value = field_info["type"];
    field_node.querySelector(".pattern-field-value").value = field_info["value"];

    return field_node;
}


/**
 * 创建字段控件
 * @param {String} id 
 * @returns 
 */
function createPatternField(name_placeholder = '', special_name = '', field_info = null) {

    const fieldDiv = document.createElement('div');
    fieldDiv.className = 'pattern-field-container';
    if(special_name.length > 0) {
        fieldDiv.id = special_name;
    }

    const name = field_info ? field_info['name'] : '';
    const idx = field_info ? field_info['idx'] : '';
    const byte_pos = field_info ? field_info['byte_pos']  : '';
    const byte_len = field_info ? field_info['byte_len']  : '';
    const type = field_info ? field_info['type']  : '';
    const value = field_info ? field_info['value']  : '';
 
    fieldDiv.innerHTML = `
        <button type="button" class="del-field-btn">x</button>
        <div class="pattern-field">
            <input type="number" class="pattern-field-idx" value="${idx}" min="0" max="64" placeholder="从0开始" required>
            <input type="text" class="pattern-field-name" value="${name}" placeholder="${name_placeholder}" required>
            <input type="number" class="pattern-field-byte-pos" value="${byte_pos}" min="0" max="64" placeholder="0-64" required>
            <input type="number" class="pattern-field-byte-len" value="${byte_len}" min="1" max="64" placeholder="1-64" required>
            <select class="pattern-field-type" required>
                <option value="">请选择字段类型</option>
                <option value="${PatternFiledTypeStr[1]}">${PatternFiledTypeStr[1]}</option>
                <option value="${PatternFiledTypeStr[2]}">${PatternFiledTypeStr[2]}</option>
                <option value="${PatternFiledTypeStr[3]}">${PatternFiledTypeStr[3]}</option>
                <option value="${PatternFiledTypeStr[4]}">${PatternFiledTypeStr[4]}</option>
                <option value="${PatternFiledTypeStr[5]}">${PatternFiledTypeStr[5]}</option>
                <option value="${PatternFiledTypeStr[6]}">${PatternFiledTypeStr[6]}</option>
                <option value="${PatternFiledTypeStr[7]}">${PatternFiledTypeStr[7]}</option>
                <option value="${PatternFiledTypeStr[8]}">${PatternFiledTypeStr[8]}</option>
                <option value="${PatternFiledTypeStr[9]}">${PatternFiledTypeStr[9]}</option>
                <option value="${PatternFiledTypeStr[10]}">${PatternFiledTypeStr[10]}</option>
                <option value="${PatternFiledTypeStr[11]}">${PatternFiledTypeStr[11]}</option>
            </select>
            <div class="pattern-field-value-container">
                <input type="text" class="pattern-field-value" value="${value}" placeholder="示例: H010203...">
                <button type="button" class="pattern-field-value-display-btn" title="用于转换值显示方式">H</button>
            </div>
        </div>
    `;

    fieldDiv.querySelector('.pattern-field-type').value = type;


    // 处理字段删除按钮
    fieldDiv.querySelector('.del-field-btn').addEventListener('click', function() {
        console.log(this.className, ',' ,this.parentNode.className);
        this.parentNode.parentNode?.removeChild(this.parentNode);
    });

    // 名称可以快捷使用默认填充名
    fieldDiv.querySelector('.pattern-field-name').addEventListener('dblclick', function(e) {
        const realName = this.value;
        const placeholderName = this.placeholder;
        if(realName.length <= 0 && placeholderName.length > 0) {
            this.value = placeholderName;
        }
    
    });

    return fieldDiv;
}

/**
 * 把字段按数组下标idx从小到大排序一下
 * @param {Node} pattern_list 
 * @returns 返回排序好的数组结果
 */
function patternListSortByIdx(pattern_list) {
    const newChildren = Array.from(pattern_list.children).sort((a, b) => {
        const intA = parseInt(a.querySelector('.pattern-field-idx').value);
        const intB = parseInt(b.querySelector('.pattern-field-idx').value);
        return intA - intB;
    });
    console.log('newChildren: ', newChildren);
    return newChildren;
}


/**
 * 创建TCP格式配置模态框
 * @param {dataset} targetId 
 * @param {Node数组} pattern_fields 
 */
function createCustomTcpPatternModal(target_element, field_title, pattern_infos_map, status_element, is_special = false, userHandleCb = null) {

    /*
    缓存结构
    {
        "type": 1, 
        "least_byte_len": 10,
        "special_fields": {
            "start_magic_num_field": { },
            "function_code_field": { },
        },
        "common_fields": [
            {
                "name": "field1",
                "idx": 0,
                "byte_len": 2
            },...
        ]
        ...
    }
    */
    console.info('pattern_infos_map: ', pattern_infos_map);

    const special_fields = pattern_infos_map ? pattern_infos_map["special_fields"] : null;
    const common_fields = pattern_infos_map ? pattern_infos_map["common_fields"] : null;

    const configModal = document.createElement('div');
    configModal.className = 'modal-overlay';
    configModal.innerHTML = `
        <div class="config-pattern-modal">
            <div class="modal-header">
                <h3>配置TCP格式信息</h3>
                <button class="close-modal">&times;</button>
            </div>
            <div class="modal-body">
                <form id="config-pattern-modal-form">
                    ${is_special ? `
                    <div class="form-columns-container">
                        <div class="form-group">
                            <label for="pattern-least-length">解析所需最小长度(Byte)</label>
                            <input type="number" class="pattern-least-length" value="${pattern_infos_map["least_byte_len"] ? pattern_infos_map["least_byte_len"] : ''}" min="1" max="64" placeholder="1-64" required>
                        </div>
                    </div>`
                    : ''}
                    <div class="pattern-field-info" id="special-pattern-fields">
                        <div class="pattern-field-info-header">
                            <label for="title">${field_title}</label>
                            <button type="button" class="add-field-btn" style="${!is_special ? 'display: inline;' : 'display: none;'}">+</button>
                            <button type="button" class="sort-field-btn" title="默认升序排列">0-9↑</button>
                        </div>
                        <div class="pattern-field-labels">
                            <label for="idx">数组下标</label>
                            <label for="name">名称</label>
                            ${is_special ? `
                            <label for="byte_pos">Byte起始位置</label>
                            ` : ''}
                            <label for="byte_len">Byte长度</label>
                            <label for="type">类型</label>
                            <label for="value">值</label>
                        </div>
                        <div class="pattern-list">
                        </div>
                    </div>
                    <div class="form-actions">
                        <button type="button" class="clear-btn">清除</button>
                        <button type="button" class="cancel-btn">取消</button>
                        <button type="sumbit" class="confirm-btn">确定</button>
                    </div>
                </form>

            </div>
        </div>
    `;

    document.body.appendChild(configModal);

    /* 恢复字段信息*/
    const pattern_list = configModal.querySelector(".pattern-list");

    if(special_fields) {
        console.log('special_fields: ', special_fields);

        Object.keys(special_fields).forEach(key => {

            let field = special_fields[key];
            // 删除按钮对于特殊字段都是不生效
            field.querySelector('.del-field-btn').disabled = true;

            if(!is_special) {
                field.querySelector('.pattern-field-byte-pos').required = false;

                field.querySelector('.pattern-field-byte-pos').style.display = 'none';

                field.title = '不可编辑';
                field.querySelector('.pattern-field').style.backgroundColor = 'light-dark(rgba(239, 239, 239, 0.3), rgba(19, 1, 1, 0.3))';
                // field.querySelector('.pattern-field').disabled = true;

                field.querySelectorAll('*').forEach((childElement, index) => {

                    childElement.disabled = true;

                });
            }
  

            pattern_list.appendChild(field);
    
        });
    }

    if(common_fields) {
        common_fields.forEach(field => {
            field.querySelector('.pattern-field-byte-pos').style.display = 'none';
            field.querySelector('.pattern-field-byte-pos').required = false;
    
            pattern_list.appendChild(field);
        });
    }

    // 默认按idx从小到大排序
    pattern_list.replaceChildren(...patternListSortByIdx(pattern_list));

    /* 恢复字段信息*/

    // 处理添加按钮(添加普通字段)
    // TODO: 后续把特殊字段也做成自由添加
    configModal.querySelector('.add-field-btn').addEventListener('click', function() {
        const field = createPatternField('其他普通字段', '');

        field.querySelector('.pattern-field-byte-pos').style.display = 'none';
        field.querySelector('.pattern-field-byte-pos').required = false;

        pattern_list.appendChild(field);
    });

    // 处理排序按钮
    configModal.querySelector('.sort-field-btn').addEventListener('click', function() {
        console.log('before pattern_list: ', pattern_list);
        
        const new_children = patternListSortByIdx(pattern_list);
        pattern_list.replaceChildren(...new_children);

        console.log('after pattern_list: ', pattern_list);
    });

    // 处理取消按钮
    configModal.querySelector('.cancel-btn').addEventListener('click', function() {
        document.body.removeChild(configModal);
    });

    // 处理关闭按钮
    configModal.querySelector('.close-modal').addEventListener('click', function() {
        document.body.removeChild(configModal);
    });

    // 处理确定按钮
    configModal.querySelector('#config-pattern-modal-form').addEventListener('submit', async function(e) {
        // 禁止默认提交行为
        e.stopPropagation();
        e.preventDefault();

        let patternFieldInfos = {
            "least_byte_len": 0,
            "special_fields": {},
            "common_fields": []
        };

        const least_byte_len = configModal.querySelector('.pattern-least-length')?.value || 0;
    
        patternFieldInfos["least_byte_len"] = Number(least_byte_len);
        
        
        let pos = 0; // 每个字段的pos信息

        // 遍历每个字段
        configModal.querySelectorAll('.pattern-field').forEach(fieldDiv => {

            // 没有具体名称说明是普通字段
            let key = fieldDiv.parentNode?.id;
            let new_field = {};

            const field_name = fieldDiv.querySelector('.pattern-field-name').value || 'default';
            new_field["name"] = field_name;

            const field_idx = fieldDiv.querySelector('.pattern-field-idx').value || 0;
            new_field["idx"] = Number(field_idx);

            const field_byte_len = fieldDiv.querySelector('.pattern-field-byte-len').value || 0;
            new_field["byte_len"] = Number(field_byte_len);

            // 起始位置分为计算和填值
            if(is_special) {
                const field_byte_pos = fieldDiv.querySelector('.pattern-field-byte-pos').value || 0;
                new_field["byte_pos"] = Number(field_byte_pos);
            } else {
                
                new_field["byte_pos"] = pos;
                pos += Number(field_byte_len);
            }

            const field_type = fieldDiv.querySelector('.pattern-field-type').value || '';
            new_field["type"] = field_type;

            const field_value = fieldDiv.querySelector('.pattern-field-value').value || '';
            // TODO 字面显示 转换 实际十六进制
            new_field["value"] = field_value;

            if(key) {
                key = key.replaceAll('-', '_');
                patternFieldInfos["special_fields"][key] = new_field;
            } else {
                patternFieldInfos["common_fields"].push(new_field);
            }

        });

        console.log('submit patternFieldInfos: ', patternFieldInfos);

        target_element.dataset.patternInfos = JSON.stringify(patternFieldInfos);
        // 更新导入状态提示
        if (status_element) {
            status_element.style.display = 'inline';
        }

        // 设置一个回调函数进行用户处理
        if(userHandleCb) {

            await userHandleCb(patternFieldInfos);

        }

        document.body.removeChild(configModal);
    });

    // 处理清除按钮
    configModal.querySelector('.clear-btn').addEventListener('click', async function(e) {
        e.stopPropagation();
        if (confirm('确定要清除当前全部格式信息吗？')) {
            
            // 清空格式缓存
            delete target_element.dataset.patternInfos;
            
            // 遍历每个字段 把当前已添加的普通字段全部清除
            configModal.querySelectorAll('.pattern-field-container').forEach(fieldContainerDiv => {
                
                // 没有具体名称说明是普通字段
                if(!fieldContainerDiv.id) {
                    configModal.querySelector('.pattern-list')?.removeChild(fieldContainerDiv);
                } 
            });

            // 更新导入状态提示
            if (status_element) {
                status_element.style.display = 'none';
            }

            // 不关闭模态框
            // document.body.removeChild(configModal);
        }
    });
}

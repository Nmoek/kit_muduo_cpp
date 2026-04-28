(function initKitProxyMocks(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});

    function clone(value) {
        // Mock 返回深拷贝，避免页面代码直接改坏内存态数据源。
        return JSON.parse(JSON.stringify(value));
    }

    // 模拟后端中“项目 TCP 解析格式”的特殊字段配置。
    const defaultSpecialFields = {
        start_magic_num_field: {
            name: '起始标识',
            idx: 0,
            byte_pos: 0,
            byte_len: 4,
            type: 'INT32',
            value: 'H23232323',
        },
        body_length_field: {
            name: '报文体长度',
            idx: 4,
            byte_pos: 14,
            byte_len: 4,
            type: 'UINT32',
            value: '',
        },
        function_code_field: {
            name: '功能码',
            idx: 3,
            byte_pos: 12,
            byte_len: 2,
            type: 'UINT16',
            value: '',
        },
    };

    // V1 Mock 是浏览器内存态：刷新页面会还原，适合本地无后端时验证 UI 流程。
    const state = {
        nextProjectId: 100,
        nextProtocolId: 1000,
        projects: [
            {
                id: 2,
                listen_port: 2222,
                protocol_type: 2,
                mode: 1,
                name: 'test2222',
                status: 0,
                target_ip: '',
                user_id: 1,
                pattern_type: 1,
                ctime: '2025-08-11 07:55:27',
            },
            {
                id: 1,
                listen_port: 1111,
                protocol_type: 1,
                mode: 1,
                name: 'test1111',
                status: 0,
                target_ip: '',
                user_id: 1,
                pattern_type: 0,
                ctime: '2025-08-11 07:55:15',
            },
        ],
        protocols: [
            {
                id: 2,
                name: 'test2',
                project_id: 2,
                type: 'TCP',
                req_cfg: {
                    function_code_filed_value: 'H1000',
                    common_fields: [
                        { name: '消息总长度', idx: 1, byte_pos: 4, byte_len: 4, type: 'UINT32', value: 'H0209' },
                        { name: '消息序列号', idx: 2, byte_pos: 8, byte_len: 4, type: 'UINT32', value: 'H0003' },
                        { name: '消息时间戳', idx: 5, byte_pos: 18, byte_len: 8, type: 'UINT64', value: '' },
                    ],
                },
                resp_cfg: {
                    function_code_filed_value: 'H1080',
                    common_fields: [
                        { name: '消息总长度', idx: 1, byte_pos: 4, byte_len: 4, type: 'UINT32', value: 'H02090000' },
                        { name: '消息序列号', idx: 2, byte_pos: 8, byte_len: 4, type: 'UINT32', value: 'H00000003' },
                        { name: '消息时间戳', idx: 5, byte_pos: 18, byte_len: 8, type: 'UINT64', value: 'HA86D9F9F9A010000' },
                    ],
                },
                resp_body_status: 0,
                resp_body_type: 'json',
                req_body_status: 0,
                req_body_type: 'json',
                ctime: '2025-12-02 06:01:03',
                utime: '2025-12-02 06:01:03',
            },
            {
                id: 1,
                name: 'test1',
                project_id: 1,
                type: 'HTTP',
                req_cfg: {
                    method: 'GET',
                    path: '/api/test1',
                },
                resp_cfg: {
                    status_code: 0,
                },
                resp_body_status: 0,
                resp_body_type: 'json',
                req_body_status: 0,
                req_body_type: 'json',
                ctime: '2025-12-02 06:01:03',
                utime: '2025-12-02 06:01:03',
            },
        ],
        bodies: {
            '1-1': { body_type: 'json', body_data: '{\n  "test1": "111111",\n  "test2": "222222"\n}' },
            '1-2': { body_type: 'json', body_data: '{\n  "ok": true\n}' },
            '2-1': { body_type: 'json', body_data: '' },
            '2-2': { body_type: 'json', body_data: '' },
        },
        patternInfos: {
            2: {
                least_byte_len: 26,
                special_fields: clone(defaultSpecialFields),
            },
        },
    };

    function nowText() {
        return new Date().toISOString().slice(0, 19).replace('T', ' ');
    }

    /**
     * @param {any} projectId
     */
    function findProject(projectId) {
        return state.projects.find(project => Number(project.id) === Number(projectId));
    }

    /**
     * @param {any} protocolId
     */
    function findProtocol(protocolId) {
        return state.protocols.find(protocol => Number(protocol.id) === Number(protocolId));
    }

    /**
     * @param {any} text
     */
    function encodeText(text) {
        return new TextEncoder().encode(text || '');
    }

    const mocks = {
        state,
        clone,
        getProjectList(offset = 0, limit = state.projects.length) {
            return clone(state.projects.slice(offset, offset + limit));
        },
        getProject(projectId) {
            const project = findProject(projectId);
            return project ? [clone(project)] : [];
        },
        addProject(project) {
            const projectId = ++state.nextProjectId;
            // 尽量模拟真实后端“先添加再返回 id，再查单项”的交互方式。
            const created = Object.assign({
                id: projectId,
                status: 0,
                user_id: 1,
                ctime: nowText(),
            }, clone(project));

            created.id = projectId;
            state.projects.unshift(created);

            if (created.pattern_info) {
                state.patternInfos[projectId] = clone(created.pattern_info);
            }

            return { project_id: projectId };
        },
        updateProjectName(projectId, name) {
            const project = findProject(projectId);
            if (project) project.name = name;
            return true;
        },
        deleteProject(projectId) {
            state.projects = state.projects.filter(project => Number(project.id) !== Number(projectId));
            state.protocols = state.protocols.filter(protocol => Number(protocol.project_id) !== Number(projectId));
            return true;
        },
        getProtocolList(projectId, offset = 0, limit = 10) {
            return clone(
                state.protocols
                    .filter(protocol => Number(protocol.project_id) === Number(projectId))
                    .slice(offset, offset + limit)
            );
        },
        getProtocol(protocolId) {
            const protocol = findProtocol(protocolId);
            return protocol ? [clone(protocol)] : [];
        },
        addProtocol(protocol) {
            const protocolId = ++state.nextProtocolId;
            const type = protocol.cfg_header.type || 'HTTP';
            // Mock 接收的 submit_protocol 与真实 /protocols/add payload 同源。
            const created = {
                id: protocolId,
                name: protocol.cfg_header.name,
                project_id: protocol.cfg_header.project_id,
                type,
                req_cfg: clone(protocol.req_cfg || {}),
                resp_cfg: clone(protocol.resp_cfg || {}),
                req_body_status: protocol.request_body ? 1 : 0,
                req_body_type: protocol.cfg_header.req_body_type || 'json',
                resp_body_status: protocol.response_body ? 1 : 0,
                resp_body_type: protocol.cfg_header.resp_body_type || 'json',
                ctime: nowText(),
                utime: nowText(),
            };

            state.protocols.unshift(created);
            state.bodies[`${protocolId}-1`] = {
                body_type: created.req_body_type,
                body_data: protocol.request_body || '',
            };
            state.bodies[`${protocolId}-2`] = {
                body_type: created.resp_body_type,
                body_data: protocol.response_body || '',
            };

            return { protocol_id: protocolId };
        },
        updateProtocolName(protocolId, name) {
            const protocol = findProtocol(protocolId);
            if (protocol) {
                protocol.name = name;
                protocol.utime = nowText();
            }
            return true;
        },
        deleteProtocol(protocolId) {
            state.protocols = state.protocols.filter(protocol => Number(protocol.id) !== Number(protocolId));
            delete state.bodies[`${protocolId}-1`];
            delete state.bodies[`${protocolId}-2`];
            return true;
        },
        updateProtocolCfg(protocolId, reqOrResp, cfgPatch) {
            const protocol = findProtocol(protocolId);
            if (!protocol) return true;

            const key = Number(reqOrResp) === 1 ? 'req_cfg' : 'resp_cfg';
            protocol[key] = Object.assign({}, protocol[key], clone(cfgPatch));
            protocol.utime = nowText();
            return true;
        },
        getProtocolDetailsCfg(protocolId) {
            const protocol = findProtocol(protocolId);
            if (!protocol) return null;

            return clone({
                req_cfg: protocol.req_cfg || {},
                resp_cfg: protocol.resp_cfg || {},
            });
        },
        updateProtocolBody(protocolId, reqOrResp, bodyType, body) {
            const protocol = findProtocol(protocolId);
            if (protocol) {
                const isReq = Number(reqOrResp) === 1;
                protocol[isReq ? 'req_body_status' : 'resp_body_status'] = body ? 1 : 0;
                protocol[isReq ? 'req_body_type' : 'resp_body_type'] = bodyType;
                protocol.utime = nowText();
            }

            state.bodies[`${protocolId}-${reqOrResp}`] = {
                body_type: bodyType,
                body_data: body || '',
            };
            return true;
        },
        getProtocolBody(protocolId, reqOrResp) {
            const body = state.bodies[`${protocolId}-${reqOrResp}`] || {
                body_type: 'json',
                body_data: '',
            };

            return [body.body_type, encodeText(body.body_data)];
        },
        getProjectPatternInfo(projectId) {
            return clone(state.patternInfos[projectId] || {
                least_byte_len: 26,
                special_fields: defaultSpecialFields,
            });
        },
        getTcpCommonFields(protocolId, reqOrResp) {
            const protocol = findProtocol(protocolId);
            if (!protocol) return [];

            const cfg = Number(reqOrResp) === 1 ? protocol.req_cfg : protocol.resp_cfg;
            return clone(cfg.common_fields || []);
        },
        getAllPatternFields(projectId, protocolId, reqOrResp) {
            return [
                this.getProjectPatternInfo(projectId),
                this.getTcpCommonFields(protocolId, reqOrResp),
            ];
        },
    };

    KitProxy.mocks = mocks;
})(typeof window !== 'undefined' ? window : globalThis);

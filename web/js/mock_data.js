(function initKitProxyMocks(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});

    function clone(value) {
        // Mock 返回深拷贝，避免页面代码直接改坏内存态数据源。
        return JSON.parse(JSON.stringify(value));
    }

    // 模拟后端中“项目 TCP 解析格式”的 JSON V2 配置。
    const defaultPatternInfo = {
        version: 2,
        header_bytes: 26,
        byte_order: 'big',
        length_policy: 'body_length',
        fields: [
            { name: '起始标识', byte_pos: 0, byte_len: 4, type: 'UINT32', role: 'start_magic', match: 'H23232323' },
            { name: '消息总长度', byte_pos: 4, byte_len: 4, type: 'UINT32', role: 'common' },
            { name: '消息序列号', byte_pos: 8, byte_len: 4, type: 'UINT32', role: 'common' },
            { name: '功能码', byte_pos: 12, byte_len: 2, type: 'UINT16', role: 'function_code' },
            { name: '报文体长度', byte_pos: 14, byte_len: 4, type: 'UINT32', role: 'body_length' },
            { name: '消息时间戳', byte_pos: 18, byte_len: 8, type: 'UINT64', role: 'common' },
        ],
    };

    const MOCK_SESSION_COOKIE = 'kit_mock_session';

    // V1 Mock 是浏览器内存态：刷新页面会还原，适合本地无后端时验证 UI 流程。
    const state = {
        nextUserId: 2,
        nextProjectId: 100,
        nextProtocolId: 1000,
        users: [
            {
                id: 1,
                note: 'admin',
                role: 'admin',
                status: 1,
                password: 'admin123',
                ctime: '2025-08-11 07:50:00',
                utime: '2025-08-11 07:50:00',
            },
            {
                id: 2,
                note: 'testuser',
                role: 'normal',
                status: 1,
                password: '',
                ctime: '2025-08-11 07:51:00',
                utime: '2025-08-11 07:51:00',
            },
        ],
        projects: [
            {
                id: 1,
                listen_port: 18080,
                protocol_type: 1,
                mode: 1,
                name: 'HTTP测试服务示例',
                status: 1,
                active: 1,
                target_ip: '',
                user_id: 1,
                ctime: '2025-08-11 07:55:15',
            },
            {
                id: 2,
                listen_port: 0,
                protocol_type: 2,
                length_policy: 'body_length',
                mode: 1,
                name: 'TCP测试服务示例',
                status: 1,
                active: 0,
                target_ip: '',
                user_id: 1,
                ctime: '2025-08-11 07:55:27',
            },
            {
                // 软删样例保留删除前的业务字段，页面只通过“状态”体现已删除。
                id: 3,
                listen_port: 18081,
                protocol_type: 1,
                mode: 1,
                name: 'HTTP归档测试服务示例',
                status: 0,
                active: 0,
                target_ip: '',
                user_id: 1,
                ctime: '2025-08-12 10:20:15',
            },
            /*
             * HTTPS 暂未支持，Mock 不生成 HTTPS 服务样例，避免主页面出现暂不可用协议。
             * 待后端和前端协议项编辑能力补齐后，再恢复 protocol_type: 3 的样例。
             */
        ],
        protocols: [
            {
                id: 1,
                name: 'HTTP健康检查示例',
                project_id: 1,
                type: 'HTTP',
                req_cfg: {
                    method: 'GET',
                    path: '/api/test1',
                    headers: {},
                },
                resp_cfg: {
                    status_code: 200,
                    headers: {},
                },
                resp_body_status: 0,
                resp_body_type: 'json',
                req_body_status: 0,
                req_body_type: 'json',
                ctime: '2025-12-02 06:00:03',
                utime: '2025-12-02 06:00:03',
                status: 1,
            },
            {
                id: 2,
                name: 'TCP开包检测示例',
                project_id: 2,
                type: 'TCP',
                req_cfg: {
                    function_code: 'H1000',
                    fields: {
                        4: 'H00000209',
                        8: 'H00000003',
                    },
                },
                resp_cfg: {
                    function_code: 'H1080',
                    fields: {
                        4: 'H00000209',
                        8: 'H00000003',
                        18: 'HA86D9F9F9A010000',
                    },
                },
                resp_body_status: 0,
                resp_body_type: 'json',
                req_body_status: 0,
                req_body_type: 'json',
                ctime: '2025-12-02 06:01:03',
                utime: '2025-12-02 06:01:03',
                status: 1,
            },
            {
                id: 3,
                name: 'HTTP旅检提交示例',
                project_id: 1,
                type: 'HTTP',
                req_cfg: {
                    method: 'POST',
                    path: '/api/mock/checkpoints',
                    headers: {},
                },
                resp_cfg: {
                    status_code: 204,
                    headers: {},
                },
                resp_body_status: 0,
                resp_body_type: 'json',
                req_body_status: 0,
                req_body_type: 'json',
                ctime: '2025-12-03 08:00:00',
                utime: '2025-12-03 08:30:00',
                status: 2,
            },
            {
                id: 4,
                name: 'TCP布控同步示例',
                project_id: 2,
                type: 'TCP',
                req_cfg: {
                    function_code: 'H2000',
                    fields: {
                        4: 'H0000001A',
                        8: 'H00000008',
                    },
                },
                resp_cfg: {
                    function_code: 'H2080',
                    fields: {
                        4: 'H0000001A',
                        8: 'H00000008',
                    },
                },
                resp_body_status: 0,
                resp_body_type: 'json',
                req_body_status: 0,
                req_body_type: 'json',
                ctime: '2025-12-03 09:00:00',
                utime: '2025-12-03 09:30:00',
                status: 2,
            },
        ],
        bodies: {
            '1-1': { body_type: 'json', body_data: '{\n  "test1": "111111",\n  "test2": "222222"\n}' },
            '1-2': { body_type: 'json', body_data: '{\n  "ok": true\n}' },
            '2-1': { body_type: 'json', body_data: '' },
            '2-2': { body_type: 'json', body_data: '' },
            '3-1': { body_type: 'json', body_data: '{\n  "checkpointId": "demo-001"\n}' },
            '3-2': { body_type: 'json', body_data: '' },
            '4-1': { body_type: 'json', body_data: '' },
            '4-2': { body_type: 'json', body_data: '' },
        },
        patternInfos: {
            2: clone(defaultPatternInfo),
        },
    };

    function nowText() {
        return new Date().toISOString().slice(0, 19).replace('T', ' ');
    }

    /**
     * @param {any} userId
     * @returns {string}
     */
    function userNoteById(userId) {
        const user = findUser(userId);
        return user ? user.note : '';
    }

    /**
     * Mock 示例只覆盖当前已支持的协议类型；HTTPS 暂未接入，所以这里不生成 HTTPS 示例。
     * @param {any} project
     * @returns {any}
     */
    function decorateProject(project) {
        if (!project) return project;
        return Object.assign({}, project, {
            owner_note: userNoteById(project.user_id),
            note_name: userNoteById(project.user_id),
        });
    }

    /**
     * @param {any} projects
     * @returns {Array<any>}
     */
    function decorateProjects(projects) {
        return (Array.isArray(projects) ? projects : []).map(decorateProject);
    }

    /**
     * @param {number} userId
     * @param {number} protocolType
     * @returns {boolean}
     */
    function hasUserProjectExample(userId, protocolType) {
        return state.projects.some(project => (
            Number(project.user_id) === Number(userId) &&
            Number(project.protocol_type) === Number(protocolType) &&
            Number(project.status) === 1
        ));
    }

    /**
     * @param {number} projectId
     * @param {string} protocolType
     * @returns {boolean}
     */
    function hasProjectProtocolExample(projectId, protocolType) {
        return state.protocols.some(protocol => (
            Number(protocol.project_id) === Number(projectId) &&
            String(protocol.type || '').toUpperCase() === protocolType &&
            Number(protocol.status || 1) === 1
        ));
    }

    /**
     * @param {any} projectPatch
     * @returns {any}
     */
    function createMockProject(projectPatch) {
        const project = Object.assign({
            id: ++state.nextProjectId,
            status: 1,
            active: 0,
            target_ip: '',
            ctime: nowText(),
        }, clone(projectPatch));
        state.projects.push(project);

        if (Number(project.protocol_type) === 2) {
            state.patternInfos[project.id] = clone(defaultPatternInfo);
            project.length_policy = project.length_policy || defaultPatternInfo.length_policy;
        }

        return project;
    }

    /**
     * @param {any} protocolPatch
     * @returns {any}
     */
    function createMockProtocol(protocolPatch) {
        const protocol = Object.assign({
            id: ++state.nextProtocolId,
            req_body_status: 0,
            req_body_type: 'json',
            resp_body_status: 0,
            resp_body_type: 'json',
            ctime: nowText(),
            utime: nowText(),
            status: 1,
        }, clone(protocolPatch));
        state.protocols.push(protocol);
        state.bodies[`${protocol.id}-1`] = state.bodies[`${protocol.id}-1`] || {
            body_type: protocol.req_body_type,
            body_data: '',
        };
        state.bodies[`${protocol.id}-2`] = state.bodies[`${protocol.id}-2`] || {
            body_type: protocol.resp_body_type,
            body_data: '',
        };
        return protocol;
    }

    /**
     * Mock 普通用户可以任意输入 note 登录，因此进入主页面前要为当前用户补齐参考示例。
     * @param {any} user
     */
    function ensureSupportedProtocolExamples(user) {
        if (!user || isAdminUser(user)) return;

        let httpProject = state.projects.find(project => (
            Number(project.user_id) === Number(user.id) &&
            Number(project.protocol_type) === 1 &&
            Number(project.status) === 1
        ));
        if (!httpProject && !hasUserProjectExample(user.id, 1)) {
            httpProject = createMockProject({
                listen_port: 18080 + Number(user.id),
                protocol_type: 1,
                mode: 1,
                name: 'HTTP测试服务示例',
                active: 1,
                user_id: user.id,
                ctime: '2025-08-11 07:55:15',
            });
        }

        if (httpProject && !hasProjectProtocolExample(httpProject.id, 'HTTP')) {
            createMockProtocol({
                name: 'HTTP健康检查示例',
                project_id: httpProject.id,
                type: 'HTTP',
                req_cfg: {
                    method: 'GET',
                    path: '/api/test1',
                    headers: {},
                },
                resp_cfg: {
                    status_code: 200,
                    headers: {},
                },
                ctime: '2025-12-02 06:00:03',
                utime: '2025-12-02 06:00:03',
            });
        }

        let tcpProject = state.projects.find(project => (
            Number(project.user_id) === Number(user.id) &&
            Number(project.protocol_type) === 2 &&
            Number(project.status) === 1
        ));
        if (!tcpProject && !hasUserProjectExample(user.id, 2)) {
            tcpProject = createMockProject({
                listen_port: 0,
                protocol_type: 2,
                length_policy: 'body_length',
                mode: 1,
                name: 'TCP测试服务示例',
                active: 0,
                user_id: user.id,
                ctime: '2025-08-11 07:55:27',
            });
        }

        if (tcpProject && !hasProjectProtocolExample(tcpProject.id, 'TCP')) {
            createMockProtocol({
                name: 'TCP开包检测示例',
                project_id: tcpProject.id,
                type: 'TCP',
                req_cfg: {
                    function_code: 'H1000',
                    fields: {
                        4: 'H00000209',
                        8: 'H00000003',
                    },
                },
                resp_cfg: {
                    function_code: 'H1080',
                    fields: {
                        4: 'H00000209',
                        8: 'H00000003',
                        18: 'HA86D9F9F9A010000',
                    },
                },
                ctime: '2025-12-02 06:01:03',
                utime: '2025-12-02 06:01:03',
            });
        }
    }

    /**
     * Mock 登录态用普通 Cookie 模拟后端 session，跨页面跳转后仍能读取当前用户。
     * @returns {number}
     */
    function readMockSessionUserId() {
        if (typeof document === 'undefined') return 0;
        const cookies = String(document.cookie || '').split(';');
        for (const cookie of cookies) {
            const parts = cookie.trim().split('=');
            if (parts[0] === MOCK_SESSION_COOKIE) {
                const userId = Number(decodeURIComponent(parts.slice(1).join('=')));
                return Number.isInteger(userId) && userId > 0 ? userId : 0;
            }
        }
        return 0;
    }

    /**
     * @param {number} userId
     */
    function writeMockSessionUserId(userId) {
        if (typeof document === 'undefined') return;
        document.cookie = `${MOCK_SESSION_COOKIE}=${encodeURIComponent(String(userId))}; path=/`;
    }

    function clearMockSession() {
        if (typeof document === 'undefined') return;
        document.cookie = `${MOCK_SESSION_COOKIE}=; path=/; max-age=0`;
    }

    /**
     * @param {any} note
     * @returns {boolean}
     */
    function isValidNote(note) {
        return /^[A-Za-z0-9]{3,32}$/.test(String(note || ''));
    }

    /**
     * @param {any} userId
     */
    function findUser(userId) {
        return state.users.find(user => Number(user.id) === Number(userId));
    }

    /**
     * @param {any} note
     */
    function findUserByNote(note) {
        return state.users.find(user => user.note === String(note || '').trim());
    }

    function publicUser(user) {
        if (!user) return null;
        const copyUser = clone(user);
        delete copyUser.password;
        return copyUser;
    }

    function getCurrentMockUser() {
        const user = findUser(readMockSessionUserId());
        if (!user || Number(user.status) !== 1) {
            const error = new Error('请先登录');
            error.status = 401;
            throw error;
        }
        return user;
    }

    function isAdminUser(user) {
        return user && user.role === 'admin';
    }

    function requireAdminUser() {
        const user = getCurrentMockUser();
        if (!isAdminUser(user)) {
            const error = new Error('权限不足，请使用管理员账号登录');
            error.status = 403;
            throw error;
        }
        return user;
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
            const currentUser = getCurrentMockUser();
            ensureSupportedProtocolExamples(currentUser);
            let projects = state.projects.slice();
            if (isAdminUser(currentUser)) {
                if (!(arguments[2] && arguments[2].include_deleted)) {
                    projects = projects.filter(project => Number(project.status) === 1);
                }
            } else {
                projects = projects.filter(project => (
                    Number(project.user_id) === Number(currentUser.id)
                    && Number(project.status) === 1
                ));
            }
            return clone(decorateProjects(projects.slice(offset, offset + limit)));
        },
        getProject(projectId) {
            const project = findProject(projectId);
            if (project) ensureSupportedProtocolExamples(getCurrentMockUser());
            return project ? [clone(decorateProject(project))] : [];
        },
        addProject(project) {
            const projectId = ++state.nextProjectId;
            // 尽量模拟真实后端“先添加再返回 id，再查单项”的交互方式。
            const created = Object.assign({
                id: projectId,
                status: 1,
                active: 0,
                user_id: getCurrentMockUser().id,
                ctime: nowText(),
            }, clone(project));

            created.id = projectId;
            created.status = 1;
            created.active = 0;
            state.projects.unshift(created);

            if (created.pattern_info) {
                state.patternInfos[projectId] = clone(created.pattern_info);
                created.length_policy = created.pattern_info.length_policy || '';
            }

            return { project_id: projectId };
        },
        setProjectActive(projectId, active) {
            const project = findProject(projectId);
            if (!project) return {};

            project.active = active ? 1 : 0;
            if (Number(project.mode) === 1) {
                if (project.active) {
                    project.listen_port = Number(project.listen_port) > 0
                        ? Number(project.listen_port)
                        : 30000 + Number(project.id);
                } else {
                    project.listen_port = 0;
                }
            }

            return clone({
                active: project.active,
                listen_port: project.listen_port,
            });
        },
        updateProjectName(projectId, name) {
            const project = findProject(projectId);
            if (project) project.name = name;
            return true;
        },
        deleteProject(projectId) {
            const project = findProject(projectId);
            if (project) {
                project.status = 0;
                project.active = 0;
            }
            return true;
        },
        restoreProject(projectId) {
            requireAdminUser();
            const project = findProject(projectId);
            if (project) {
                project.status = 1;
                project.active = 0;
            }
            return true;
        },
        getProtocolList(projectId, offset = 0, limit = 10) {
            const currentUser = getCurrentMockUser();
            ensureSupportedProtocolExamples(currentUser);
            const project = findProject(projectId);
            const includeInactive = isAdminUser(currentUser) && arguments[3] && arguments[3].include_inactive;
            return clone(
                state.protocols
                    .filter(protocol => Number(protocol.project_id) === Number(projectId))
                    .filter(protocol => {
                        if (!project) return false;
                        if (!isAdminUser(currentUser) && Number(project.user_id) !== Number(currentUser.id)) return false;
                        return includeInactive ? true : Number(protocol.status || 1) === 1;
                    })
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
                status: 1,
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
            const protocol = findProtocol(protocolId);
            if (protocol) {
                protocol.status = 2;
                protocol.utime = nowText();
            }
            return true;
        },
        restoreProtocol(protocolId) {
            requireAdminUser();
            const protocol = findProtocol(protocolId);
            if (protocol) {
                protocol.status = 1;
                protocol.utime = nowText();
            }
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
            return clone(state.patternInfos[projectId] || defaultPatternInfo);
        },
        updateProjectPatternInfo(projectId, patternInfo) {
            state.patternInfos[projectId] = clone(patternInfo || {});
            const project = findProject(projectId);
            if (project) {
                project.length_policy = patternInfo && patternInfo.length_policy ? patternInfo.length_policy : '';
            }
            return true;
        },
        getTcpCommonFields(protocolId, reqOrResp) {
            const protocol = findProtocol(protocolId);
            if (!protocol) return [];

            const cfg = Number(reqOrResp) === 1 ? protocol.req_cfg : protocol.resp_cfg;
            if (Array.isArray(cfg.common_fields)) return clone(cfg.common_fields);

            const patternInfo = state.patternInfos[protocol.project_id] || defaultPatternInfo;
            return clone((patternInfo.fields || [])
                .filter(field => field.role === 'common')
                .map(field => Object.assign({}, field, {
                    value: cfg.fields && cfg.fields[String(field.byte_pos)] ? cfg.fields[String(field.byte_pos)] : '',
                })));
        },
        getAllPatternFields(projectId, protocolId, reqOrResp) {
            const protocol = findProtocol(protocolId);
            const cfg = protocol
                ? (Number(reqOrResp) === 1 ? protocol.req_cfg : protocol.resp_cfg)
                : {};
            const patternInfo = this.getProjectPatternInfo(projectId);
            const fields = KitProxy.tcpPatternEditor
                ? KitProxy.tcpPatternEditor.patternInfoToItemFields(patternInfo, cfg)
                : this.getTcpCommonFields(protocolId, reqOrResp);
            return [
                patternInfo,
                fields,
            ];
        },
        login(note, loginType = 'normal', password = '') {
            const normalizedNote = String(note || '').trim();
            const normalizedLoginType = loginType === 'admin' ? 'admin' : 'normal';
            if (!isValidNote(normalizedNote)) {
                throw new Error('note 必须是 3-32 位英文字母和数字');
            }

            let user = findUserByNote(normalizedNote);
            if (normalizedLoginType === 'admin') {
                if (!user || user.role !== 'admin' || Number(user.status) !== 1 || user.password !== String(password || '')) {
                    throw new Error('管理员 note 或密码错误');
                }
            } else if (!user) {
                user = {
                    id: ++state.nextUserId,
                    note: normalizedNote,
                    role: 'normal',
                    status: 1,
                    password: '',
                    ctime: nowText(),
                    utime: nowText(),
                };
                state.users.push(user);
            } else if (Number(user.status) !== 1) {
                throw new Error('该用户已停用，请联系管理员');
            }

            ensureSupportedProtocolExamples(user);
            writeMockSessionUserId(user.id);
            return publicUser(user);
        },
        logout() {
            clearMockSession();
            return true;
        },
        getCurrentUser() {
            return publicUser(getCurrentMockUser());
        },
        listUsers(offset = 0, limit = 10, status = 'all') {
            requireAdminUser();
            const normalizedStatus = String(status || 'all');
            const users = state.users.filter(user => {
                if (normalizedStatus === 'active') return Number(user.status) === 1;
                if (normalizedStatus === 'inactive' || normalizedStatus === 'disabled') return Number(user.status) !== 1;
                return true;
            });
            return clone(users.slice(offset, offset + limit).map(publicUser));
        },
        addUser(user) {
            requireAdminUser();
            const note = String(user && user.note || '').trim();
            const role = user && user.role === 'admin' ? 'admin' : 'normal';
            const password = String(user && user.password || '');
            if (!isValidNote(note)) {
                throw new Error('note 必须是 3-32 位英文字母和数字');
            }
            if (findUserByNote(note)) {
                throw new Error('note 已存在');
            }
            if (role === 'admin' && !password) {
                throw new Error('管理员用户必须填写密码');
            }

            const created = {
                id: ++state.nextUserId,
                note,
                role,
                status: 1,
                password: role === 'admin' ? password : '',
                ctime: nowText(),
                utime: nowText(),
            };
            state.users.unshift(created);
            return { user_id: created.id };
        },
        getUser(userId) {
            requireAdminUser();
            const user = findUser(userId);
            return user ? [publicUser(user)] : [];
        },
        updateUser(userId, patch) {
            requireAdminUser();
            const user = findUser(userId);
            if (!user) throw new Error('用户不存在');

            const nextNote = patch && Object.prototype.hasOwnProperty.call(patch, 'note')
                ? String(patch.note || '').trim()
                : user.note;
            const nextRole = patch && patch.role === 'admin' ? 'admin' : (patch && patch.role === 'normal' ? 'normal' : user.role);
            const nextStatus = patch && Object.prototype.hasOwnProperty.call(patch, 'status') ? Number(patch.status) : Number(user.status);
            const nextPassword = patch && Object.prototype.hasOwnProperty.call(patch, 'password') ? String(patch.password || '') : '';

            if (!isValidNote(nextNote)) {
                throw new Error('note 必须是 3-32 位英文字母和数字');
            }
            const sameNoteUser = findUserByNote(nextNote);
            if (sameNoteUser && Number(sameNoteUser.id) !== Number(user.id)) {
                throw new Error('note 已存在');
            }
            if (nextRole === 'admin' && user.role !== 'admin' && !nextPassword && !user.password) {
                throw new Error('新增管理员身份时必须填写密码');
            }

            user.note = nextNote;
            user.role = nextRole;
            user.status = nextStatus === 1 ? 1 : 0;
            if (nextPassword) {
                user.password = nextPassword;
            } else if (nextRole !== 'admin') {
                user.password = '';
            }
            user.utime = nowText();
            return publicUser(user);
        },
        deleteUser(userId) {
            requireAdminUser();
            const user = findUser(userId);
            if (user) {
                user.status = 0;
                user.utime = nowText();
            }
            return true;
        },
        restoreUser(userId) {
            requireAdminUser();
            const user = findUser(userId);
            if (user) {
                user.status = 1;
                user.utime = nowText();
            }
            return true;
        },
    };

    KitProxy.mocks = mocks;
})(typeof window !== 'undefined' ? window : globalThis);

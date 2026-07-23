(function initKitProxyApi(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});

    // UI 层只调用 KitProxy.api；这里集中决定走 Mock 还是走真实后端。
    function isMockMode() {
        return KitProxy.config && KitProxy.config.apiMode === 'mock';
    }

    function apiUrl(path) {
        // apiBaseUrl 为空时保持同源部署；设置后可临时指向代理或后端调试地址。
        const baseUrl = KitProxy.config && KitProxy.config.apiBaseUrl ? KitProxy.config.apiBaseUrl : '';
        return `${baseUrl}${path}`;
    }

    function buildApiError(message, response, data) {
        const error = new Error(message);
        error.status = response ? response.status : 0;
        error.data = data;
        return error;
    }

    async function parseJsonResponse(response, fallbackMessage) {
        // 后端业务协议统一为 { code, message, data }，这里把 HTTP 错误和业务错误收敛成异常。
        let result;

        try {
            result = await response.json();
        } catch (error) {
            if (!response.ok) {
                throw buildApiError(`${fallbackMessage || '请求失败'}: HTTP ${response.status}`, response);
            }
            throw buildApiError(`${fallbackMessage || '响应解析失败'}: ${error.message}`, response);
        }

        if (!response.ok) {
            throw buildApiError(result.message || `${fallbackMessage || '请求失败'}: HTTP ${response.status}`, response, result);
        }

        if (Number(result.code) !== 0) {
            const error = buildApiError(result.message || `业务错误: code=${result.code}`, response, result);
            error.code = result.code;
            throw error;
        }

        return result.data;
    }

    async function requestJson(path, options, fallbackMessage) {
        // 普通 JSON 接口走这个入口；multipart 或二进制响应保留专门处理。
        const response = await fetch(apiUrl(path), Object.assign({ credentials: 'same-origin' }, options || {}));
        return parseJsonResponse(response, fallbackMessage);
    }

    function requestJsonBody(path, body, fallbackMessage, options = {}) {
        return requestJson(path, Object.assign({
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify(body || {}),
        }, options), fallbackMessage);
    }

    function normalizePatternInfoForBackend(patternInfo) {
        if (!patternInfo || Number(patternInfo.version) !== 2) return patternInfo;

        const normalized = Object.assign({}, patternInfo);
        if (normalized.byte_order && !normalized.default_order) {
            normalized.default_order = normalized.byte_order;
        }
        return normalized;
    }

    function normalizeProjectForBackend(project) {
        const normalized = Object.assign({}, project || {});
        normalized.protocol_type = projectProtocolTypeForBackend(normalized.protocol_type);
        if (normalized.pattern_info) {
            normalized.pattern_info = normalizePatternInfoForBackend(normalized.pattern_info);
        }
        return normalized;
    }

    /**
     * 新增项目和新增协议统一使用 HTTP/TCP/HTTPS 字符串表达协议类型。
     * @param {any} protocolType
     * @returns {string}
     */
    function projectProtocolTypeForBackend(protocolType) {
        if (typeof protocolType === 'string') {
            const normalized = protocolType.trim().toUpperCase();
            if (normalized === 'CUSTOM_TCP') return 'TCP';
            if (normalized === 'HTTP' || normalized === 'TCP' || normalized === 'HTTPS') return normalized;
        }

        const value = Number(protocolType);
        if (value === 1) return 'HTTP';
        if (value === 2) return 'TCP';
        if (value === 3) return 'HTTPS';
        return '';
    }

    /**
     * 后端可能返回 HTTP/TCP/HTTPS 字符串，页面内部仍统一使用 1/2/3 数值常量。
     * @param {any} protocolType
     * @returns {number}
     */
    function projectProtocolTypeForPage(protocolType) {
        if (typeof protocolType === 'string') {
            const normalized = protocolType.trim().toUpperCase();
            if (normalized === 'HTTP') return 1;
            if (normalized === 'TCP' || normalized === 'CUSTOM_TCP') return 2;
            if (normalized === 'HTTPS') return 3;
        }

        const value = Number(protocolType);
        return [1, 2, 3].includes(value) ? value : 0;
    }

    /**
     * 项目 VO 以 runtime_state 表示运行态；active 只作为旧页面兼容字段保留。
     * @param {any} project
     * @returns {any}
     */
    function normalizeProject(project) {
        if (!project) return null;
        const runtimeState = project.runtime_state != null
            ? Number(project.runtime_state)
            : (Number(project.active) === 1 ? 1 : 0);
        return Object.assign({}, project, {
            protocol_type: projectProtocolTypeForPage(project.protocol_type),
            runtime_state: runtimeState === 1 ? 1 : 0,
            active: runtimeState === 1 ? 1 : 0,
        });
    }

    /**
     * @param {any} projects
     * @returns {Array<any>}
     */
    function normalizeProjects(projects) {
        return Array.isArray(projects) ? projects.map(normalizeProject).filter(Boolean) : [];
    }

    /**
     * 协议 VO 以 config_state 表示上线态；runtime_enabled 只允许作为命令入参。
     * @param {any} protocol
     * @returns {any}
     */
    function normalizeProtocol(protocol) {
        if (!protocol) return null;
        const configState = protocol.config_state != null ? Number(protocol.config_state) : 0;
        const normalized = Object.assign({}, protocol, {
            config_state: [0, 1, 2].includes(configState) ? configState : 0,
        });
        delete normalized.runtime_enabled;
        return normalized;
    }

    /**
     * @param {any} protocols
     * @returns {Array<any>}
     */
    function normalizeProtocols(protocols) {
        return Array.isArray(protocols) ? protocols.map(normalizeProtocol).filter(Boolean) : [];
    }

    /**
     * @param {any} bodyData
     * @returns {string}
     */
    function decodeProtocolBodyData(bodyData) {
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
     * 协议项编辑/重配置页需要从数据库重新读取当前完整详情，并一次性回填表单控件。
     * @param {number | string} protocolId
     * @returns {Promise<any>}
     */
    async function getProtocolEditDetail(protocolId) {
        const [protocols, requestBodyInfo, responseBodyInfo] = await Promise.all([
            api.getProtocol(protocolId),
            api.getProtocolBody(protocolId, 1),
            api.getProtocolBody(protocolId, 2),
        ]);

        if (!Array.isArray(protocols) || protocols.length <= 0) {
            throw new Error('获取协议项详情失败');
        }

        const protocol = protocols[0];
        return Object.assign({}, protocol, {
            request_body: decodeProtocolBodyData(requestBodyInfo && requestBodyInfo[1]),
            response_body: decodeProtocolBodyData(responseBodyInfo && responseBodyInfo[1]),
            req_body_type: (requestBodyInfo && requestBodyInfo[0]) || protocol.req_body_type || 'json',
            resp_body_type: (responseBodyInfo && responseBodyInfo[0]) || protocol.resp_body_type || 'json',
        });
    }

    /**
     * @param {any} runtimeData
     * @param {boolean} running
     * @returns {any}
     */
    function normalizeProjectRuntimeResult(runtimeData, running) {
        const runtimeState = runtimeData && runtimeData.runtime_state != null
            ? Number(runtimeData.runtime_state)
            : (running ? 1 : 0);
        return Object.assign({}, runtimeData || {}, {
            runtime_state: runtimeState === 1 ? 1 : 0,
            active: runtimeState === 1 ? 1 : 0,
        });
    }

    /**
     * 后端用户字段使用 user_id/note_name/status 字符串，页面统一使用 id/note/status 字符串。
     * @param {any} status
     * @returns {'active' | 'disabled' | 'unknown'}
     */
    function normalizeUserStatus(status) {
        const value = String(status == null ? '' : status).toLowerCase();
        if (status === 1 || value === '1' || value === 'active') return 'active';
        if (status === 0 || status === 2 || value === '0' || value === '2' || value === 'disabled' || value === 'inactive') {
            return 'disabled';
        }
        return 'unknown';
    }

    /**
     * 抹平 mock 和真实后端的用户字段差异，避免页面层到处判断 note/note_name。
     * @param {any} user
     * @returns {any}
     */
    function normalizeUser(user) {
        if (!user) return null;
        const id = user.id != null ? user.id : user.user_id;
        const note = user.note != null ? user.note : user.note_name;
        const normalized = Object.assign({}, user, {
            id,
            user_id: id,
            note: note || '',
            note_name: note || '',
            role: user.role === 'admin' ? 'admin' : 'normal',
            status: normalizeUserStatus(user.status),
        });
        return normalized;
    }

    /**
     * @param {any} users
     * @returns {Array<any>}
     */
    function normalizeUsers(users) {
        return Array.isArray(users) ? users.map(normalizeUser).filter(Boolean) : [];
    }

    /**
     * @param {any} data
     * @returns {any}
     */
    function normalizeUserDetail(data) {
        if (Array.isArray(data)) return normalizeUser(data[0]);
        return normalizeUser(data);
    }

    /**
     * 管理员用户接口入参转换为真实后端字段。
     * @param {any} user
     * @returns {any}
     */
    function userPayloadForBackend(user) {
        const source = user || {};
        const payload = {
            note_name: String(source.note_name != null ? source.note_name : source.note || '').trim(),
            role: source.role === 'admin' ? 'admin' : 'normal',
        };
        if (Object.prototype.hasOwnProperty.call(source, 'status')) {
            payload.status = normalizeUserStatus(source.status);
        }
        if (Object.prototype.hasOwnProperty.call(source, 'password')) {
            payload.password = String(source.password || '');
        }
        return payload;
    }

    /**
     * Mock 数据源仍使用 note 和数值状态，这里做一次兼容转换。
     * @param {any} user
     * @returns {any}
     */
    function userPayloadForMock(user) {
        const source = user || {};
        const payload = Object.assign({}, source);
        if (payload.note == null && payload.note_name != null) {
            payload.note = payload.note_name;
        }
        if (Object.prototype.hasOwnProperty.call(payload, 'status')) {
            payload.status = normalizeUserStatus(payload.status) === 'active' ? 1 : 0;
        }
        return payload;
    }

    function runMutation(key, action, message) {
        if (KitProxy.utils && typeof KitProxy.utils.runMutationOnce === 'function') {
            return KitProxy.utils.runMutationOnce(key, action, {
                message,
                mockVisibleDelayMs: 1200,
            });
        }

        return action();
    }

    const api = {
        isMockMode,
        apiUrl,
        async login(note, loginType = 'normal', password = '') {
            if (isMockMode()) return normalizeUser(KitProxy.mocks.login(note, loginType, password));

            return normalizeUser(await requestJsonBody('/auth/login', {
                note,
                login_type: loginType,
                password,
            }, '登录失败'));
        },
        async logout() {
            if (isMockMode()) return KitProxy.mocks.logout();

            await requestJson('/auth/logout', {
                method: 'POST',
            }, '退出登录失败');

            return true;
        },
        async getCurrentUser() {
            if (isMockMode()) return normalizeUser(KitProxy.mocks.getCurrentUser());

            return normalizeUser(await requestJson('/auth/me', {
                method: 'GET',
            }, '获取当前用户失败'));
        },
        async listUsers(offset = 0, limit = 10, status = 'all') {
            const normalizedStatus = status === 'inactive' ? 'disabled' : (status || 'all');
            if (isMockMode()) return normalizeUsers(KitProxy.mocks.listUsers(offset, limit, normalizedStatus));

            return requestJsonBody('/users/list', {
                offset,
                limit,
                status: normalizedStatus,
            }, '获取用户列表失败').then(normalizeUsers);
        },
        async addUser(user) {
            return runMutation('api-add-user', async function() {
                if (isMockMode()) return KitProxy.mocks.addUser(userPayloadForMock(user));

                return requestJsonBody('/users/add', userPayloadForBackend(user), '新增用户失败');
            }, '正在新增用户...');
        },
        async getUser(userId) {
            if (isMockMode()) return normalizeUserDetail(KitProxy.mocks.getUser(userId));

            return normalizeUserDetail(await requestJson('/users/' + String(userId), {
                method: 'GET',
            }, '获取用户详情失败'));
        },
        async updateUser(userId, patch) {
            return runMutation(`api-update-user-${userId}`, async function() {
                if (isMockMode()) return normalizeUser(KitProxy.mocks.updateUser(userId, userPayloadForMock(patch)));

                const result = await requestJsonBody('/users/' + String(userId), userPayloadForBackend(patch), '修改用户失败');
                return result && (result.id || result.user_id) ? normalizeUser(result) : true;
            }, '正在保存用户...');
        },
        async deleteUser(userId) {
            return runMutation(`api-delete-user-${userId}`, async function() {
                if (isMockMode()) return KitProxy.mocks.deleteUser(userId);

                await requestJson('/users/' + String(userId), {
                    method: 'DELETE',
                }, '停用用户失败');

                return true;
            }, '正在停用用户...');
        },
        async restoreUser(userId) {
            return runMutation(`api-restore-user-${userId}`, async function() {
                if (isMockMode()) return KitProxy.mocks.restoreUser(userId);

                await requestJson('/users/' + String(userId) + '/restore', {
                    method: 'POST',
                }, '恢复用户失败');

                return true;
            }, '正在恢复用户...');
        },
        async getProjectList(offset = 0, limit = 10) {
            const options = arguments.length >= 3 && arguments[2] ? arguments[2] : {};
            if (isMockMode()) return normalizeProjects(KitProxy.mocks.getProjectList(offset, limit, options));

            return requestJsonBody('/projects/list', Object.assign({ offset, limit }, options), '获取测试服务列表失败')
                .then(normalizeProjects);
        },
        async getProject(projectId) {
            if (isMockMode()) return normalizeProjects(KitProxy.mocks.getProject(projectId));

            return requestJson('/projects/' + String(projectId), {
                method: 'GET',
            }, '获取单个测试服务失败').then(normalizeProjects);
        },
        async addProject(project) {
            return runMutation('api-add-project', async function() {
                if (isMockMode()) return KitProxy.mocks.addProject(project);

                return requestJson('/projects/add', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify(normalizeProjectForBackend(project)),
                }, '添加测试服务失败');
            }, '正在添加测试服务...');
        },
        async setProjectRuntimeState(projectId, running) {
            return runMutation(`api-set-project-runtime-state-${projectId}`, async function() {
                if (isMockMode()) {
                    const result = KitProxy.mocks.setProjectRuntimeState
                        ? KitProxy.mocks.setProjectRuntimeState(projectId, running)
                        : KitProxy.mocks.setProjectActive(projectId, running);
                    return normalizeProjectRuntimeResult(result, running);
                }

                const operation = running ? 1 : 0;
                const result = await requestJson('/projects/' + String(projectId) + '/runtime_state?operation=' + String(operation), {
                    method: 'POST',
                }, running ? '启动测试服务失败' : '停止测试服务失败');
                return normalizeProjectRuntimeResult(result, running);
            }, running ? '正在启动测试服务...' : '正在停止测试服务...');
        },
        async setProjectActive(projectId, active) {
            // 兼容旧调用名；新增代码应使用 setProjectRuntimeState。
            return api.setProjectRuntimeState(projectId, active);
        },
        async updateProjectName(projectId, name) {
            return runMutation(`api-update-project-name-${projectId}`, async function() {
                if (isMockMode()) return KitProxy.mocks.updateProjectName(projectId, name);

                return requestJson('/projects/' + projectId + '/name', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify({ name }),
                }, '修改测试服务标题失败');
            }, '正在保存测试服务名称...');
        },
        async deleteProject(projectId) {
            return runMutation(`api-delete-project-${projectId}`, async function() {
                if (isMockMode()) return KitProxy.mocks.deleteProject(projectId);

                await requestJson('/projects/' + String(projectId), {
                    method: 'DELETE',
                }, '删除测试服务失败');

                return true;
            }, '正在删除测试服务...');
        },
        async restoreProject(projectId) {
            return runMutation(`api-restore-project-${projectId}`, async function() {
                if (isMockMode()) return KitProxy.mocks.restoreProject(projectId);

                await requestJson('/projects/' + String(projectId) + '/restore', {
                    method: 'POST',
                }, '恢复测试服务失败');

                return true;
            }, '正在恢复测试服务...');
        },
        async getProjectPatternInfo(projectId) {
            if (isMockMode()) return KitProxy.mocks.getProjectPatternInfo(projectId);

            return requestJson('/projects/' + projectId + '/pattern_info', {
                method: 'GET',
            }, '获取TCP格式信息失败');
        },
        async updateProjectPatternInfo(projectId, patternInfo) {
            return runMutation(`api-update-project-pattern-${projectId}`, async function() {
                if (isMockMode()) return KitProxy.mocks.updateProjectPatternInfo(projectId, patternInfo);

                return requestJson('/projects/pattern_info', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify({
                        id: projectId,
                        pattern_info: normalizePatternInfoForBackend(patternInfo),
                    }),
                }, '修改TCP格式信息失败');
            }, '正在保存 TCP 格式信息...');
        },
        async getProtocolList(projectId, offset = 0, limit = 10) {
            const options = arguments.length >= 4 && arguments[3] ? arguments[3] : {};
            if (isMockMode()) return normalizeProtocols(KitProxy.mocks.getProtocolList(projectId, offset, limit, options));

            return requestJsonBody('/protocols/list', Object.assign({
                project_id: projectId,
                offset,
                limit,
            }, options), '获取协议项列表失败').then(normalizeProtocols);
        },
        async getProtocol(protocolId) {
            if (isMockMode()) return normalizeProtocols(KitProxy.mocks.getProtocol(protocolId));

            return requestJson('/protocols/' + String(protocolId), {
                method: 'GET',
            }, '获取单个协议项失败').then(normalizeProtocols);
        },
        getProtocolEditDetail,
        async addProtocol(protocol) {
            const projectId = protocol && protocol.cfg_header ? protocol.cfg_header.project_id : 'unknown';
            return runMutation(`api-add-protocol-${projectId}`, async function() {
                if (isMockMode()) return KitProxy.mocks.addProtocol(protocol);

                const formData = KitProxy.utils.createAddProtocolFormData(protocol);

                return requestJson('/protocols/add', {
                    method: 'POST',
                    body: formData,
                }, '添加协议项失败');
            }, '正在添加协议项...');
        },
        async updateProtocolName(protocolId, name) {
            return runMutation(`api-update-protocol-name-${protocolId}`, async function() {
                if (isMockMode()) return KitProxy.mocks.updateProtocolName(protocolId, name);

                await requestJson('/protocols/' + String(protocolId) + '/name', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify({
                        name,
                    }),
                }, '修改协议项标题失败');

                return true;
            }, '正在保存协议项名称...');
        },
        async setProtocolRuntime(protocolId, enabled) {
            return runMutation(`api-set-protocol-runtime-${protocolId}`, async function() {
                if (isMockMode()) return KitProxy.mocks.setProtocolRuntime(protocolId, enabled);

                return requestJsonBody('/protocols/' + String(protocolId) + '/runtime_enabled', {
                    runtime_enabled: enabled ? 1 : 0,
                }, enabled ? '上线协议项失败' : '下线协议项失败');
            }, enabled ? '正在上线协议项...' : '正在下线协议项...');
        },
        async reconfigProtocol(protocolId, protocol) {
            return runMutation(`api-reconfig-protocol-${protocolId}`, async function() {
                if (isMockMode()) return KitProxy.mocks.reconfigProtocol(protocolId, protocol);

                const formData = KitProxy.utils.createAddProtocolFormData(protocol);
                return requestJson('/protocols/' + String(protocolId) + '/reconfig', {
                    method: 'POST',
                    body: formData,
                }, '重配置协议项失败');
            }, '正在重配置协议项...');
        },
        async deleteProtocol(protocolId, projectId) {
            return runMutation(`api-delete-protocol-${protocolId}`, async function() {
                if (isMockMode()) return KitProxy.mocks.deleteProtocol(protocolId, projectId);

                await requestJson('/protocols/' + String(protocolId), {
                    method: 'DELETE',
                }, '删除协议项失败');

                return true;
            }, '正在删除协议项...');
        },
        async restoreProtocol(protocolId) {
            return runMutation(`api-restore-protocol-${protocolId}`, async function() {
                if (isMockMode()) return KitProxy.mocks.restoreProtocol(protocolId);

                await requestJson('/protocols/' + String(protocolId) + '/restore', {
                    method: 'POST',
                }, '恢复协议项失败');

                return true;
            }, '正在恢复协议项...');
        },
        async updateProtocolCfg(protocolId, projectId, reqOrResp, cfgJson) {
            return runMutation(`api-update-protocol-cfg-${protocolId}-${reqOrResp}-${JSON.stringify(cfgJson || {})}`, async function() {
                if (isMockMode()) return KitProxy.mocks.updateProtocolCfg(protocolId, reqOrResp, cfgJson);

                await requestJson('/protocols/' + String(protocolId) + '/details/cfg', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify({
                        side: reqOrResp,
                        cfg_data: cfgJson,
                    }),
                }, '修改协议项配置失败');

                return true;
            }, '正在保存协议项配置...');
        },
        async getProtocolDetailsCfg(protocolId) {
            if (isMockMode()) return KitProxy.mocks.getProtocolDetailsCfg(protocolId);

            return requestJson(`/protocols/${protocolId}/details/cfg`, {
                method: 'GET',
            }, '查询协议项配置失败');
        },
        async updateProtocolBody(protocolId, projectId, reqOrResp, protocolType, bodyType, body) {
            return runMutation(`api-update-protocol-body-${protocolId}-${reqOrResp}`, async function() {
                if (isMockMode()) return KitProxy.mocks.updateProtocolBody(protocolId, reqOrResp, bodyType, body);

                const formData = KitProxy.utils.createProtocolBodyFormData(
                    protocolId,
                    projectId,
                    reqOrResp,
                    protocolType,
                    bodyType,
                    body,
                );

                await requestJson('/protocols/' + String(protocolId) + '/details/body', {
                    method: 'POST',
                    body: formData,
                }, '修改协议项Body失败');

                return true;
            }, reqOrResp === 1 ? '正在保存校验请求Body...' : '正在保存目标响应Body...');
        },
        async getProtocolBody(protocolId, reqOrResp) {
            if (isMockMode()) return KitProxy.mocks.getProtocolBody(protocolId, reqOrResp);

            // Body 类型是 JSON 响应，Body 数据是原始字节流，所以这里不能用 requestJson 合并处理。
            const [typeResponse, dataResponse] = await Promise.all([
                fetch(apiUrl('/protocols/' + String(protocolId) + '/details/body_type?side=' + encodeURIComponent(String(reqOrResp))), {
                    method: 'GET',
                    credentials: 'same-origin',
                }),
                fetch(apiUrl('/protocols/' + String(protocolId) + '/details/body_data?side=' + encodeURIComponent(String(reqOrResp))), {
                    method: 'GET',
                    credentials: 'same-origin',
                }),
            ]);

            const bodyType = await parseJsonResponse(typeResponse, '获取Body类型失败');

            if (!dataResponse.ok) {
                throw new Error('获取Body数据失败: HTTP ' + dataResponse.status);
            }

            const dataBuffer = await dataResponse.arrayBuffer();
            return [bodyType.body_type, new Uint8Array(dataBuffer)];
        },
        async getTcpCommonFields(protocolId, reqOrResp) {
            if (isMockMode()) return KitProxy.mocks.getTcpCommonFields(protocolId, reqOrResp);

            return requestJson('/protocols/' + String(protocolId) + '/details/tcp/common_fields?side=' + encodeURIComponent(String(reqOrResp)), {
                method: 'GET',
            }, '获取普通字段信息失败');
        },
        async getAllPatternFields(projectId, protocolId, reqOrResp) {
            if (isMockMode()) return KitProxy.mocks.getAllPatternFields(projectId, protocolId, reqOrResp);

            const [patternInfo, cfgInfo] = await Promise.all([
                api.getProjectPatternInfo(projectId),
                api.getProtocolDetailsCfg(protocolId),
            ]);
            const sideCfg = Number(reqOrResp) === 1
                ? (cfgInfo && cfgInfo.req_cfg) || {}
                : (cfgInfo && cfgInfo.resp_cfg) || {};
            const fields = KitProxy.tcpPatternEditor
                ? KitProxy.tcpPatternEditor.patternInfoToItemFields(patternInfo, sideCfg)
                : [];
            return [patternInfo, fields];
        },
    };

    KitProxy.api = api;

    if (!KitProxy.auth) {
        // 兼容旧 HTML 或本地静态服务漏加载 auth.js 的场景；auth.js 正常加载时会覆盖这里的轻量实现。
        const authState = {
            currentUser: null,
            loadingPromise: null,
        };

        /**
         * @param {string} path
         * @returns {string}
         */
        function buildFallbackPageUrl(path) {
            const params = new URLSearchParams(global.location.search);
            const keepKeys = ['apiMode', 'apiBaseUrl', 'enableDebugLog'];
            const nextParams = new URLSearchParams();
            keepKeys.forEach(key => {
                if (params.has(key)) nextParams.set(key, params.get(key));
            });
            const query = nextParams.toString();
            try {
                const url = new URL(path, global.location.href);
                url.search = query;
                return url.pathname + (url.search ? `?${url.searchParams.toString()}` : '') + url.hash;
            } catch (error) {
                return query ? `${path}?${query}` : path;
            }
        }

        function fallbackIsAdmin(user) {
            return String(user && user.role || '').toLowerCase() === 'admin';
        }

        function fallbackRoleText(user) {
            return fallbackIsAdmin(user) ? '管理员' : '普通用户';
        }

        function fallbackNoteText(user) {
            return String(user && (user.note || user.note_name) || '');
        }

        function fallbackLoginUrl() {
            const loginUrl = new URL(buildFallbackPageUrl('/html/login.html'), global.location.href);
            const current = global.location.pathname + global.location.search + global.location.hash;
            const returnTo = fallbackNormalizeReturnTo(current);
            if (returnTo) loginUrl.searchParams.set('returnTo', returnTo);
            return loginUrl.pathname + (loginUrl.search ? `?${loginUrl.searchParams.toString()}` : '') + loginUrl.hash;
        }

        function fallbackMainUrl() {
            return buildFallbackPageUrl('/html/main.html');
        }

        function fallbackNormalizeReturnTo(href) {
            const value = String(href || '').trim();
            if (!value || value.startsWith('//') || value.startsWith('\\')
                || /^[a-z][a-z\d+.-]*:/i.test(value)) return '';
            try {
                const url = new URL(value, global.location.href);
                if (url.origin !== global.location.origin || !/^\/html\/[^/?#]+\.html$/i.test(url.pathname)) {
                    return '';
                }
                return url.pathname + url.search + url.hash;
            } catch (error) {
                return '';
            }
        }

        function fallbackPostLoginUrl() {
            const params = new URLSearchParams(global.location.search);
            const returnTo = fallbackNormalizeReturnTo(params.get('returnTo'));
            return returnTo || buildFallbackPageUrl('/html/main.html');
        }

        function fallbackRedirectToLogin(options = {}) {
            if (!global.location) return;
            if (options.includeReturnTo === false) {
                global.location.href = buildFallbackPageUrl('/html/login.html');
                return;
            }
            global.location.href = fallbackLoginUrl();
        }

        function fallbackRedirectToMain() {
            if (global.location) global.location.href = fallbackMainUrl();
        }

        /**
         * @param {any} user
         * @returns {any}
         */
        function fallbackApplyCurrentUser(user) {
            authState.currentUser = user;
            if (typeof document !== 'undefined' && document.body) {
                document.body.dataset.userRole = fallbackIsAdmin(user) ? 'admin' : 'normal';
            }
            return user;
        }

        /**
         * @param {{redirectOnUnauthorized?: boolean}=} options
         * @returns {Promise<any>}
         */
        async function fallbackLoadCurrentUser(options = {}) {
            const redirectOnUnauthorized = options.redirectOnUnauthorized !== false;

            if (authState.currentUser) return authState.currentUser;
            if (authState.loadingPromise) return authState.loadingPromise;

            authState.loadingPromise = api.getCurrentUser()
                .then(fallbackApplyCurrentUser)
                .catch(function(error) {
                    authState.currentUser = null;
                    if (Number(error && error.status) === 401 && redirectOnUnauthorized) {
                        fallbackRedirectToLogin();
                    }
                    throw error;
                })
                .finally(function() {
                    authState.loadingPromise = null;
                });

            return authState.loadingPromise;
        }

        /**
         * @param {{requireAdmin?: boolean, redirectOnUnauthorized?: boolean}=} options
         * @returns {Promise<any>}
         */
        async function fallbackRequireCurrentUser(options = {}) {
            const user = await fallbackLoadCurrentUser(options);
            if (options.requireAdmin && !fallbackIsAdmin(user)) {
                const error = new Error('无权限访问用户管理');
                error.status = 403;
                throw error;
            }
            return user;
        }

        KitProxy.auth = {
            state: authState,
            buildPageUrl: buildFallbackPageUrl,
            buildLoginUrl: fallbackLoginUrl,
            buildMainUrl: fallbackMainUrl,
            normalizeReturnTo: fallbackNormalizeReturnTo,
            getSafeReturnToFromLoginUrl: function() {
                return fallbackNormalizeReturnTo(new URLSearchParams(global.location.search).get('returnTo'));
            },
            buildPostLoginUrl: fallbackPostLoginUrl,
            loadCurrentUser: fallbackLoadCurrentUser,
            requireCurrentUser: fallbackRequireCurrentUser,
            applyCurrentUser: fallbackApplyCurrentUser,
            redirectToLogin: fallbackRedirectToLogin,
            redirectToMain: fallbackRedirectToMain,
            isAdmin: fallbackIsAdmin,
            roleText: fallbackRoleText,
            noteText: fallbackNoteText,
            getCurrentUser: function() {
                return authState.currentUser;
            },
            isCurrentUserAdmin: function() {
                return fallbackIsAdmin(authState.currentUser);
            },
        };
    }
})(typeof window !== 'undefined' ? window : globalThis);

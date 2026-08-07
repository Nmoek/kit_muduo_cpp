(function initProtocolInteractionLive(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});

    const STATES = Object.freeze({
        DISCONNECTED: 'disconnected',
        CONNECTING: 'connecting',
        CATCHING_UP: 'catching_up',
        ACTIVE: 'active',
        PAUSING: 'pausing',
        PAUSED: 'paused',
        RESUMING: 'resuming',
        RECONNECT_WAIT: 'reconnect_wait',
        DISCONNECTING: 'disconnecting',
        ERROR: 'error',
    });

    const DEFAULT_OPTIONS = Object.freeze({
        maxVisibleRecords: 20,
        maxPendingRecords: 20,
        mergeIntervalMs: 1000,
        maxTotalRecords: 40,
        commandTimeoutMs: 8000,
        reconnectDelaysMs: [1000, 2000, 4000, 8000, 15000],
    });

    const MERGE_SPEED_INTERVALS = Object.freeze({
        slow: 1500,
        medium: 1000,
        fast: 600,
    });

    const DELIVERY_VALUES = new Set(['catch_up', 'live']);
    const SCOPE_VALUES = new Set(['protocol', 'project']);

    function numberId(value) {
        const result = Number(value);
        return Number.isInteger(result) && result > 0 ? result : 0;
    }

    function positiveInteger(value) {
        const result = Number(value);
        return Number.isInteger(result) && result >= 0 ? result : null;
    }

    /**
     * @param {any} cursor
     * @returns {{cacheInstanceId: number, seq: number} | null}
     */
    function normalizeCursor(cursor) {
        if (!cursor) return null;
        const cacheInstanceId = numberId(cursor.cacheInstanceId != null
            ? cursor.cacheInstanceId
            : cursor.cache_instance_id);
        const seq = positiveInteger(cursor.seq);
        return cacheInstanceId > 0 && seq != null
            ? { cacheInstanceId, seq }
            : null;
    }

    function cursorFromWire(cursor) {
        if (!cursor || typeof cursor !== 'object') return null;
        return normalizeCursor(cursor);
    }

    function cursorToWire(cursor) {
        const normalized = normalizeCursor(cursor);
        return normalized
            ? {
                cache_instance_id: normalized.cacheInstanceId,
                seq: normalized.seq,
            }
            : null;
    }

    function cursorKey(scope) {
        return scope === 'project' ? 'projectCursor' : 'protocolCursor';
    }

    function queueKey(scope) {
        return scope === 'project' || scope === 'notice' ? 'notice' : 'protocol';
    }

    function scopeName(scope) {
        if (scope === 'project' || scope === 'notice') return 'project';
        if (scope === 'protocol') return 'protocol';
        return null;
    }

    function normalizeMergeSpeed(speed) {
        const value = String(speed || '').toLowerCase();
        return Object.prototype.hasOwnProperty.call(MERGE_SPEED_INTERVALS, value) ? value : 'medium';
    }

    function mergeSpeedForInterval(intervalMs) {
        const value = Number(intervalMs);
        return Object.entries(MERGE_SPEED_INTERVALS)
            .sort((left, right) => Math.abs(left[1] - value) - Math.abs(right[1] - value))[0][0];
    }

    /**
     * 构建与后端同源 Cookie 一起使用的 WebSocket URL。
     * @param {{protocolId: number|string, apiBaseUrl?: string, includeProjectNotice?: boolean, protocolCursor?: any, projectCursor?: any, location?: Location}} options
     * @returns {string}
     */
    function buildWebSocketUrl(options = {}) {
        const location = options.location || global.location;
        const baseUrl = options.apiBaseUrl || (location && location.origin) || '';
        let url;

        try {
            url = new global.URL('/ws/protocol-interactions/live', baseUrl || undefined);
        } catch (error) {
            const fallbackOrigin = location && location.origin ? location.origin : '';
            url = new global.URL('/ws/protocol-interactions/live', fallbackOrigin || undefined);
        }

        if (url.protocol === 'http:') url.protocol = 'ws:';
        if (url.protocol === 'https:') url.protocol = 'wss:';
        url.searchParams.set('protocol_id', String(options.protocolId));
        url.searchParams.set('include_project_notice', options.includeProjectNotice === false ? '0' : '1');

        const cursors = [
            ['protocol', normalizeCursor(options.protocolCursor)],
            ['project', normalizeCursor(options.projectCursor)],
        ];
        cursors.forEach(([scope, cursor]) => {
            if (!cursor) return;
            url.searchParams.set(`after_${scope}_cache_instance_id`, String(cursor.cacheInstanceId));
            url.searchParams.set(`after_${scope}_seq`, String(cursor.seq));
        });

        return url.toString();
    }

    /**
     * @param {string} scope
     * @param {number|string} cacheInstanceId
     * @param {number|string} seq
     * @returns {string}
     */
    function buildRecordKey(scope, cacheInstanceId, seq) {
        return `${scope}:${cacheInstanceId}:${seq}`;
    }

    function attachmentKey(scope, cacheInstanceId, seq, attachmentId) {
        return `${scope}:${cacheInstanceId}:${seq}:${attachmentId}`;
    }

    function interactionPersistence() {
        return KitProxy.protocolInteractionPersistence || null;
    }

    function cursorEqual(left, right) {
        return Boolean(left && right)
            && left.cacheInstanceId === right.cacheInstanceId
            && left.seq === right.seq;
    }

    function safeJsonParse(value) {
        try {
            return JSON.parse(value);
        } catch (error) {
            return null;
        }
    }

    function cloneCursorState(state) {
        return state ? { cacheInstanceId: state.cacheInstanceId, seq: state.seq } : null;
    }

    function bodyAttachmentRefs(record) {
        const refs = [];
        ['request', 'response'].forEach(side => {
            const value = record && record[side];
            if (!value) return;
            if (value.body && Array.isArray(value.body.attachments)) {
                value.body.attachments.forEach(ref => refs.push({ side, ref }));
            }
            if (value.raw_packet && Array.isArray(value.raw_packet.attachments)) {
                value.raw_packet.attachments.forEach(ref => refs.push({ side, ref }));
            }
        });
        return refs;
    }

    function attachmentKindMime(kind) {
        const map = {
            image: 'image/*',
            audio: 'audio/*',
            video: 'video/*',
            pdf: 'application/pdf',
            archive: 'application/octet-stream',
            binary: 'application/octet-stream',
        };
        return map[String(kind || '').toLowerCase()] || 'application/octet-stream';
    }

    /**
     * 解析后端的 4 字节大端长度 + UTF-8 JSON Header + payload 二进制帧。
     * @param {ArrayBuffer|Uint8Array} data
     * @returns {{header: any, payload: Uint8Array}|{error: string}}
     */
    function parseAttachmentFrame(data) {
        let bytes;
        if (data instanceof ArrayBuffer) {
            bytes = new Uint8Array(data);
        } else if (ArrayBuffer.isView(data)) {
            bytes = new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
        } else {
            return { error: '附件帧不是二进制数据' };
        }

        if (bytes.byteLength < 4) return { error: '附件帧缺少 Header 长度' };
        const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        const headerLength = view.getUint32(0, false);
        if (headerLength <= 0 || headerLength > bytes.byteLength - 4) {
            return { error: '附件 Header 长度非法' };
        }

        const headerBytes = bytes.subarray(4, 4 + headerLength);
        let headerText;
        try {
            headerText = new global.TextDecoder('utf-8', { fatal: true }).decode(headerBytes);
        } catch (error) {
            return { error: '附件 Header 不是有效 UTF-8' };
        }

        const header = safeJsonParse(headerText);
        if (!header || header.type !== 'attachment') {
            return { error: '附件 Header 类型非法' };
        }
        return {
            header,
            payload: new Uint8Array(bytes.subarray(4 + headerLength)),
        };
    }

    function createEventMap() {
        return new Map();
    }

    function createClient(options = {}) {
        const projectId = numberId(options.projectId);
        const protocolId = numberId(options.protocolId);
        const timers = options.timers || {};
        const setTimer = timers.setTimeout || global.setTimeout.bind(global);
        const clearTimer = timers.clearTimeout || global.clearTimeout.bind(global);
        const now = timers.now || (() => Date.now());
        const config = Object.assign({}, DEFAULT_OPTIONS, options);
        config.maxVisibleRecords = Number.isInteger(Number(options.maxVisibleRecords))
            && Number(options.maxVisibleRecords) > 0
            ? Number(options.maxVisibleRecords)
            : DEFAULT_OPTIONS.maxVisibleRecords;
        config.maxPendingRecords = Number.isInteger(Number(options.maxPendingRecords))
            && Number(options.maxPendingRecords) > 0
            ? Number(options.maxPendingRecords)
            : DEFAULT_OPTIONS.maxPendingRecords;
        config.maxTotalRecords = Number.isInteger(Number(options.maxTotalRecords))
            && Number(options.maxTotalRecords) > 0
            ? Number(options.maxTotalRecords)
            : config.maxVisibleRecords + config.maxPendingRecords;
        config.mergeIntervalMs = Number.isInteger(Number(options.mergeIntervalMs))
            && Number(options.mergeIntervalMs) > 0
            ? Number(options.mergeIntervalMs)
            : DEFAULT_OPTIONS.mergeIntervalMs;
        const configuredMergeSpeed = options.mergeSpeed != null
            ? normalizeMergeSpeed(options.mergeSpeed)
            : mergeSpeedForInterval(config.mergeIntervalMs);
        if (options.mergeSpeed != null) config.mergeIntervalMs = MERGE_SPEED_INTERVALS[configuredMergeSpeed];
        config.reconnectDelaysMs = Array.isArray(options.reconnectDelaysMs)
            ? options.reconnectDelaysMs.slice()
            : DEFAULT_OPTIONS.reconnectDelaysMs.slice();

        const persistence = interactionPersistence();
        const authUser = options.user || (KitProxy.auth && typeof KitProxy.auth.getCurrentUser === 'function'
            ? KitProxy.auth.getCurrentUser()
            : null);
        const workspaceIdentity = options.workspaceIdentity
            || (persistence && typeof persistence.createWorkspaceIdentity === 'function'
                ? persistence.createWorkspaceIdentity({
                    apiBaseUrl: options.apiBaseUrl != null
                        ? options.apiBaseUrl
                        : (KitProxy.config && KitProxy.config.apiBaseUrl),
                    user: authUser,
                    projectId,
                    protocolId,
                })
                : { enabled: false, reason: 'persistence_unavailable', snapshotKey: '' });
        const persistenceAdapter = options.persistenceAdapter
            || (persistence && typeof persistence.createPersistenceAdapter === 'function'
                ? persistence.createPersistenceAdapter({
                    indexedDB: options.indexedDB,
                    forceMemory: options.forceMemoryPersistence === true,
                })
                : null);
        if (workspaceIdentity.enabled && persistenceAdapter && typeof persistenceAdapter.purgeExpired === 'function') {
            persistenceAdapter.purgeExpired(now()).catch(() => {});
        }

        const state = {
            projectId,
            protocolId,
            protocolType: options.protocolType || '',
            connectionState: STATES.DISCONNECTED,
            desiredConnected: false,
            sessionId: 0,
            acceptedClientSeq: 0,
            nextClientSeq: 1,
            pendingCommand: null,
            visibleRecords: [],
            pendingRecords: [],
            pendingQueues: {
                protocol: [],
                notice: [],
                reflow: [],
            },
            recordKeys: new Set(),
            recordOrder: options.recordOrder === 'asc' ? 'asc' : 'desc',
            mergePaused: false,
            mergeSpeed: configuredMergeSpeed,
            allowBufferOverflow: false,
            protocolCursor: null,
            projectCursor: null,
            protocolDurableCursor: normalizeCursor(options.protocolDurableCursor),
            projectDurableCursor: normalizeCursor(options.projectDurableCursor),
            protocolDurableCursorPending: null,
            projectDurableCursorPending: null,
            attachmentPayloads: new Map(),
            pendingAttachmentRefs: new Map(),
            persistedRecords: new Map(),
            pendingPersistenceRecords: new Map(),
            selectedRecordKey: null,
            restoreConnectionPaused: false,
            uiState: Object.assign({
                filter: 'all',
                detailTab: 'overview',
                fullscreen: false,
                mobileDetail: false,
                drawerOpen: false,
                followLive: true,
                readRecordKeys: [],
            }, options.uiState || {}),
            socket: null,
            socketSessionId: 0,
            reconnectTimer: null,
            mergeTimer: null,
            mergeGeneration: 0,
            bufferOverflowTimer: null,
            commandTimer: null,
            reconnectAttempt: 0,
            catchUpExpected: 0,
            catchUpReceived: 0,
            lastError: '',
            warnings: [],
            persistenceStatus: workspaceIdentity.enabled && persistenceAdapter ? 'ready' : 'disabled',
            persistenceSnapshotKey: workspaceIdentity.snapshotKey || '',
        };

        const listeners = createEventMap();
        const socketSessionIds = new WeakMap();
        let intentionalClose = false;
        let destroyed = false;
        let queryInFlight = false;
        let binaryChain = Promise.resolve();
        let persistenceChain = Promise.resolve();
        let persistenceFailureReported = false;
        let persistenceQuotaRecoveryAttempted = false;
        let persistenceSnapshotTimer = null;
        let persistenceSnapshotVersion = 0;
        const persistenceRecordGeneration = {
            protocol: 0,
            project: 0,
        };
        let hydrating = false;

        function persistenceSnapshot(overrides = {}) {
            if (!workspaceIdentity.enabled || !workspaceIdentity.snapshotKey) return null;
            const timestamp = now();
            return {
                snapshotKey: workspaceIdentity.snapshotKey,
                schemaVersion: workspaceIdentity.schemaVersion,
                backendIdentity: workspaceIdentity.backendIdentity,
                userId: workspaceIdentity.userId,
                tabSessionId: workspaceIdentity.tabSessionId,
                projectId,
                protocolId,
                createdAt: timestamp,
                updatedAt: timestamp,
                expiresAt: timestamp + 24 * 60 * 60 * 1000,
                protocolLiveCursor: cloneCursorState(state.protocolCursor),
                projectLiveCursor: cloneCursorState(state.projectCursor),
                protocolDurableCursor: cloneCursorState(overrides.protocolDurableCursor || state.protocolDurableCursor),
                projectDurableCursor: cloneCursorState(overrides.projectDurableCursor || state.projectDurableCursor),
                visibleKeys: state.visibleRecords.map(record => record._key),
                protocolPendingKeys: state.pendingQueues.protocol.map(record => record._key),
                noticePendingKeys: state.pendingQueues.notice.map(record => record._key),
                reflowKeys: state.pendingQueues.reflow.map(record => record._key),
                selectedRecordKey: state.selectedRecordKey,
                readRecordKeys: Array.isArray(state.uiState.readRecordKeys)
                    ? state.uiState.readRecordKeys.slice()
                    : [],
                uiState: Object.assign({}, state.uiState, {
                    sortDirection: state.recordOrder,
                    mergeSpeed: state.mergeSpeed,
                }),
                connectionIntent: {
                    desiredConnected: state.desiredConnected,
                    mergePaused: state.mergePaused,
                    connectionState: state.connectionState,
                    connectionPaused: state.connectionState === STATES.PAUSED
                        || state.connectionState === STATES.PAUSING,
                },
            };
        }

        function persistenceQueueSnapshot() {
            const snapshot = persistenceSnapshot();
            if (!snapshot) return;
            snapshot.visibleKeys = state.visibleRecords.map(record => record._key);
            snapshot.protocolPendingKeys = state.pendingQueues.protocol.map(record => record._key);
            snapshot.noticePendingKeys = state.pendingQueues.notice.map(record => record._key);
            snapshot.reflowKeys = state.pendingQueues.reflow.map(record => record._key);
            queuePersistence('saveSnapshot', snapshot);
        }

        function schedulePersistenceSnapshot(immediate = false) {
            if (hydrating || !workspaceIdentity.enabled || !persistenceAdapter) return;
            persistenceSnapshotVersion += 1;
            if (persistenceSnapshotTimer) {
                clearTimer(persistenceSnapshotTimer);
                persistenceSnapshotTimer = null;
            }
            if (immediate) {
                persistenceQueueSnapshot();
                return;
            }
            const version = persistenceSnapshotVersion;
            persistenceSnapshotTimer = setTimer(() => {
                persistenceSnapshotTimer = null;
                if (version <= persistenceSnapshotVersion) persistenceQueueSnapshot();
            }, 150);
        }

        function updatePersistenceUiState(patch = {}, immediate = false) {
            state.uiState = Object.assign({}, state.uiState, patch);
            if (Array.isArray(patch.readRecordKeys)) state.uiState.readRecordKeys = patch.readRecordKeys.slice();
            if (persistence && typeof persistence.writeRestoreMarker === 'function') {
                persistence.writeRestoreMarker(workspaceIdentity, state.uiState, options.sessionStorage || global.sessionStorage, now);
            }
            schedulePersistenceSnapshot(immediate);
        }

        function expectedAttachmentKeys(record) {
            return bodyAttachmentRefs(record)
                .map(({ ref }) => attachmentKey(
                    record.scope,
                    record.cache_instance_id,
                    record.seq,
                    String(ref.attachment_id),
                ));
        }

        function comparableRecord(record) {
            function normalize(value) {
                if (Array.isArray(value)) return value.map(normalize);
                if (!value || typeof value !== 'object') return value;
                const result = {};
                Object.keys(value).sort().forEach(key => {
                    if (key === 'attachments' || key === '_key' || key === '_delivery'
                        || key === 'binary_sidecars') return;
                    result[key] = normalize(value[key]);
                });
                return result;
            }
            try {
                return JSON.stringify(normalize(record || {}));
            } catch (error) {
                return '';
            }
        }

        function mergeAttachmentRefs(existing, incoming) {
            ['request', 'response'].forEach(side => {
                const currentSide = existing && existing[side];
                const incomingSide = incoming && incoming[side];
                if (!currentSide || !incomingSide) return;
                ['body', 'raw_packet'].forEach(container => {
                    const currentValue = currentSide[container];
                    const incomingValue = incomingSide[container];
                    if (!currentValue || !incomingValue || !Array.isArray(incomingValue.attachments)) return;
                    const refs = Array.isArray(currentValue.attachments) ? currentValue.attachments.slice() : [];
                    const keys = new Set(refs.map(ref => String(ref && ref.attachment_id || '')));
                    incomingValue.attachments.forEach(ref => {
                        const refKey = String(ref && ref.attachment_id || '');
                        if (refKey && !keys.has(refKey)) {
                            refs.push(ref);
                            keys.add(refKey);
                        }
                    });
                    currentValue.attachments = refs;
                });
            });
        }

        function persistenceRecordEntry(record) {
            const expectedKeys = expectedAttachmentKeys(record);
            return {
                recordStorageKey: `${workspaceIdentity.snapshotKey}:${record._key}`,
                snapshotKey: workspaceIdentity.snapshotKey,
                recordKey: record._key,
                scope: record.scope,
                cacheInstanceId: numberId(record.cache_instance_id),
                seq: positiveInteger(record.seq),
                record: Object.assign({}, record),
                completionState: expectedKeys.length ? 'incomplete' : 'complete',
                expectedAttachmentKeys: expectedKeys,
                storedAttachmentKeys: [],
                receivedAt: now(),
                updatedAt: now(),
                persistenceGeneration: persistenceRecordGeneration[record.scope] || 0,
            };
        }

        function mergeDuplicateInteraction(record, key, delivery) {
            const existing = findRecord(key);
            if (!existing) return false;
            const existingRefs = new Set(expectedAttachmentKeys(existing));
            const incomingRefs = expectedAttachmentKeys(record);
            const stored = state.persistedRecords.get(key) || state.pendingPersistenceRecords.get(key);
            if (comparableRecord(existing) !== comparableRecord(record)) {
                addWarning('重复交互记录内容发生冲突，保留已存在版本', {
                    kind: 'duplicate_interaction_conflict',
                    recordKey: key,
                });
                return true;
            }
            if (stored && stored.completionState === 'complete' && incomingRefs.some(ref => !existingRefs.has(ref))) {
                addWarning('重复交互记录的附件集合发生冲突，保留已完成版本', {
                    kind: 'duplicate_interaction_conflict',
                    recordKey: key,
                });
                return true;
            }
            if (stored && stored.completionState === 'incomplete') {
                mergeAttachmentRefs(existing, record);
                stored.expectedAttachmentKeys = Array.from(new Set(stored.expectedAttachmentKeys.concat(incomingRefs)));
                stored.record = existing;
                stored.updatedAt = now();
                registerRecordAttachments(existing);
                queuePersistence('putRecordBundle', { snapshot: persistenceSnapshot(), record: stored, attachments: [] });
            }
            return true;
        }

        function durableCursorName(scope) {
            return scope === 'project' ? 'projectDurableCursor' : 'protocolDurableCursor';
        }

        function completePersistedRecords(scope) {
            return Array.from(state.persistedRecords.values())
                .filter(entry => entry.scope === scope && entry.completionState === 'complete')
                .sort((left, right) => left.seq - right.seq);
        }

        function planDurableCursor(scope) {
            const cursorName = durableCursorName(scope);
            let cursor = state[cursorName];
            if (!cursor) return null;
            const entries = completePersistedRecords(scope)
                .filter(entry => entry.cacheInstanceId === cursor.cacheInstanceId)
                .sort((left, right) => left.seq - right.seq);
            let next = entries.find(entry => entry.seq === cursor.seq + 1);
            while (next) {
                cursor = { cacheInstanceId: next.cacheInstanceId, seq: next.seq };
                next = entries.find(entry => entry.seq === cursor.seq + 1);
            }
            return cursorEqual(cursor, state[cursorName]) ? null : cursor;
        }

        function persistDurableCursor(scope) {
            if (!workspaceIdentity.enabled || !persistenceAdapter) return;
            const cursorName = durableCursorName(scope);
            const cursor = planDurableCursor(scope);
            if (!cursor) return;
            state[`${cursorName}Pending`] = cursor;
            queuePersistence('saveSnapshot', persistenceSnapshot({ [cursorName]: cursor }), result => {
                if (result && (result.status === 'stored' || result.status === 'hit')) {
                    state[cursorName] = cursor;
                }
                if (cursorEqual(state[`${cursorName}Pending`], cursor)) {
                    state[`${cursorName}Pending`] = null;
                }
                persistDurableCursor(scope);
            });
        }

        function markRecordPersisted(recordEntry, result) {
            if (recordEntry.persistenceGeneration !== (persistenceRecordGeneration[recordEntry.scope] || 0)) return;
            if (!result || !['stored', 'hit'].includes(result.status)) return;
            state.persistedRecords.set(recordEntry.recordKey, recordEntry);
            state.pendingPersistenceRecords.delete(recordEntry.recordKey);
            persistDurableCursor(recordEntry.scope);
        }

        function reportPersistenceFailure(result, operation) {
            if (!result || result.status === 'stored' || result.status === 'hit'
                || result.status === 'miss' || result.status === 'deleted') return;
            state.persistenceStatus = result.status === 'disabled' ? 'disabled' : 'degraded';
            if (persistenceFailureReported) return;
            persistenceFailureReported = true;
            addWarning('实时工作区恢复不可用，实时查看仍可继续', {
                kind: 'persistence_unavailable',
                operation,
                persistenceStatus: state.persistenceStatus,
            });
        }

        function isQuotaFailure(result) {
            const error = result && result.error;
            if (!error) return false;
            return String(error.name || '').toLowerCase() === 'quotaexceedederror'
                || Number(error.code) === 22
                || /quota|storage.?full/i.test(String(error.message || ''));
        }

        async function executePersistence(operation, input) {
            const invoke = async () => {
                try {
                    return await persistenceAdapter[operation](input);
                } catch (error) {
                    return { status: 'error', error };
                }
            };

            let result = await invoke();
            if (operation !== 'purgeExpired' && isQuotaFailure(result)
                && !persistenceQuotaRecoveryAttempted
                && typeof persistenceAdapter.purgeExpired === 'function') {
                persistenceQuotaRecoveryAttempted = true;
                try {
                    await persistenceAdapter.purgeExpired(now());
                } catch (error) {
                    // The original operation is retried below and reports the final status.
                }
                result = await invoke();
            }
            return result;
        }

        function queuePersistence(operation, input, onSuccess) {
            if (!workspaceIdentity.enabled || !persistenceAdapter || typeof persistenceAdapter[operation] !== 'function') {
                return persistenceChain;
            }
            persistenceChain = persistenceChain
                .then(() => executePersistence(operation, input))
                .then(result => {
                    reportPersistenceFailure(result, operation);
                    if (typeof onSuccess === 'function') onSuccess(result);
                    return result;
                })
                .catch(error => {
                    reportPersistenceFailure({ status: 'error', error }, operation);
                    return { status: 'error', error };
                });
            return persistenceChain;
        }

        function persistInteractionRecord(record) {
            const entry = persistenceRecordEntry(record);
            state.pendingPersistenceRecords.set(record._key, entry);
            queuePersistence('putRecordBundle', {
                snapshot: persistenceSnapshot(),
                record: entry,
                attachments: [],
            }, result => markRecordPersisted(entry, result));
        }

        function emit(type, payload) {
            const callbacks = listeners.get(type) || [];
            callbacks.slice().forEach(callback => {
                try {
                    callback(payload, state);
                } catch (error) {
                    if (global.console && console.error) console.error('实时详情回调失败:', error);
                }
            });
            if (type !== 'state') {
                const all = listeners.get('*') || [];
                all.slice().forEach(callback => callback(payload, state));
            }
            schedulePersistenceSnapshot();
        }

        function on(type, callback) {
            if (typeof callback !== 'function') return () => {};
            const list = listeners.get(type) || [];
            list.push(callback);
            listeners.set(type, list);
            return () => {
                const current = listeners.get(type) || [];
                listeners.set(type, current.filter(item => item !== callback));
            };
        }

        function setConnectionState(nextState, detail = {}) {
            if (state.connectionState === nextState && !detail.force) return;
            state.connectionState = nextState;
            emit('state', Object.assign({ state: nextState }, detail));
        }

        function addWarning(message, detail = {}) {
            state.lastError = String(message || '');
            state.warnings.push({ message: state.lastError, time: now() });
            if (state.warnings.length > 20) state.warnings.shift();
            emit('warning', Object.assign({ message: state.lastError }, detail));
        }

        function getCursor(scope) {
            return state[cursorKey(scope)];
        }

        function setCursor(scope, cursor) {
            const normalized = normalizeCursor(cursor);
            if (!normalized) return;
            const key = cursorKey(scope);
            const current = state[key];
            if (current && current.cacheInstanceId === normalized.cacheInstanceId
                && current.seq > normalized.seq) {
                return;
            }
            state[key] = normalized;
        }

        function clearRecordResources(record) {
            bodyAttachmentRefs(record).forEach(({ ref }) => {
                const key = attachmentKey(record.scope, record.cache_instance_id, record.seq, ref.attachment_id);
                const payload = state.attachmentPayloads.get(key);
                if (payload && payload.objectUrl && global.URL && global.URL.revokeObjectURL) {
                    global.URL.revokeObjectURL(payload.objectUrl);
                }
                state.attachmentPayloads.delete(key);
                state.pendingAttachmentRefs.delete(key);
            });
        }

        function clearPersistedRecordEntries(scope) {
            if (scope) {
                persistenceRecordGeneration[scope] = (persistenceRecordGeneration[scope] || 0) + 1;
            } else {
                Object.keys(persistenceRecordGeneration).forEach(name => {
                    persistenceRecordGeneration[name] += 1;
                });
            }
            Array.from(state.persistedRecords.entries()).forEach(([key, entry]) => {
                if (!scope || entry.scope === scope) state.persistedRecords.delete(key);
            });
            Array.from(state.pendingPersistenceRecords.entries()).forEach(([key, entry]) => {
                if (!scope || entry.scope === scope) state.pendingPersistenceRecords.delete(key);
            });
        }

        function persistScopeReset(scope, snapshot) {
            if (!snapshot || !workspaceIdentity.enabled || !persistenceAdapter) return;
            queuePersistence('clearScope', {
                snapshotKey: workspaceIdentity.snapshotKey,
                scope,
                snapshot,
            });
        }

        function clearScope(scope) {
            const isScope = record => record && record.scope === scope;
            state.visibleRecords.filter(isScope).forEach(clearRecordResources);
            state.pendingQueues[queueKey(scope)].filter(isScope).forEach(clearRecordResources);
            state.pendingQueues.reflow.filter(isScope).forEach(clearRecordResources);
            state.visibleRecords = state.visibleRecords.filter(record => !isScope(record));
            state.pendingQueues[queueKey(scope)] = state.pendingQueues[queueKey(scope)].filter(record => !isScope(record));
            state.pendingQueues.reflow = state.pendingQueues.reflow.filter(record => !isScope(record));
            syncPendingRecords();
            clearPersistedRecordEntries(scope);

            Array.from(state.recordKeys).forEach(key => {
                if (key.startsWith(`${scope}:`)) state.recordKeys.delete(key);
            });
            Array.from(state.attachmentPayloads.keys()).forEach(key => {
                if (key.startsWith(`${scope}:`)) {
                    const payload = state.attachmentPayloads.get(key);
                    if (payload && payload.objectUrl && global.URL && global.URL.revokeObjectURL) {
                        global.URL.revokeObjectURL(payload.objectUrl);
                    }
                    state.attachmentPayloads.delete(key);
                }
            });
            Array.from(state.pendingAttachmentRefs.keys()).forEach(key => {
                if (key.startsWith(`${scope}:`)) state.pendingAttachmentRefs.delete(key);
            });
            const cursorName = cursorKey(scope);
            state[cursorName] = null;
            const durableName = durableCursorName(scope);
            state[durableName] = null;
            state[`${durableName}Pending`] = null;
            state.uiState.readRecordKeys = Array.isArray(state.uiState.readRecordKeys)
                ? state.uiState.readRecordKeys.filter(key => !key.startsWith(`${scope}:`))
                : [];
            if (state.selectedRecordKey && state.selectedRecordKey.startsWith(`${scope}:`)) {
                state.selectedRecordKey = null;
            }
            const snapshot = persistenceSnapshot();
            persistScopeReset(scope, snapshot);
            emit('recordsReset', { scope });
            schedulePersistenceSnapshot(true);
        }

        function allRecords() {
            return state.visibleRecords.concat(state.pendingRecords);
        }

        function findRecord(key) {
            return allRecords().find(record => record._key === key) || null;
        }

        function compareRecordTime(left, right, direction = state.recordOrder) {
            const leftTime = Number(left && left.time_ms);
            const rightTime = Number(right && right.time_ms);
            const leftValid = Number.isFinite(leftTime);
            const rightValid = Number.isFinite(rightTime);
            let comparison = 0;
            if (leftValid && rightValid) comparison = leftTime - rightTime;
            else if (leftValid) comparison = -1;
            else if (rightValid) comparison = 1;
            if (comparison === 0) comparison = String(left && left._key || '').localeCompare(String(right && right._key || ''));
            return direction === 'asc' ? -comparison : comparison;
        }

        function sortVisibleRecords() {
            state.visibleRecords.sort(compareRecordTime);
        }

        function syncPendingRecords() {
            state.pendingRecords = state.pendingQueues.reflow
                .concat(state.pendingQueues.protocol)
                .concat(state.pendingQueues.notice);
        }

        function pendingCount() {
            return state.pendingQueues.reflow.length
                + state.pendingQueues.protocol.length
                + state.pendingQueues.notice.length;
        }

        function totalRecordCount(scope = null) {
            if (scope) {
                const normalizedScope = scopeName(scope);
                if (!normalizedScope) return 0;
                const key = queueKey(normalizedScope);
                return state.visibleRecords.filter(record => scopeName(record.scope) === normalizedScope).length
                    + state.pendingQueues[key].length;
            }
            return state.visibleRecords.length + pendingCount();
        }

        function enqueuePendingRecord(record) {
            if (!record) return;
            const queue = state.pendingQueues[queueKey(record.scope)];
            if (state.recordOrder === 'asc') queue.push(record);
            else queue.unshift(record);
            syncPendingRecords();
        }

        function pendingRecordAtConsumptionEnd(queue) {
            if (!queue || queue.length === 0) return null;
            return state.recordOrder === 'asc' ? queue[0] : queue[queue.length - 1];
        }

        function takeNextPendingRecord() {
            if (state.pendingQueues.reflow.length) {
                const record = state.pendingQueues.reflow.shift();
                syncPendingRecords();
                return record;
            }
            const candidates = ['protocol', 'notice']
                .map(key => ({
                    key,
                    record: pendingRecordAtConsumptionEnd(state.pendingQueues[key]),
                }))
                .filter(item => item.record);
            if (!candidates.length) return null;
            candidates.sort((left, right) => compareRecordTime(left.record, right.record));
            const selected = candidates[0];
            if (state.recordOrder === 'asc') state.pendingQueues[selected.key].shift();
            else state.pendingQueues[selected.key].pop();
            syncPendingRecords();
            return selected.record;
        }

        function clearBufferOverflowTimer() {
            if (state.bufferOverflowTimer) clearTimer(state.bufferOverflowTimer);
            state.bufferOverflowTimer = null;
        }

        function releaseOldestPendingRecord(reason, scope = null) {
            const normalizedScope = scopeName(scope);
            const queueKeys = normalizedScope
                ? [queueKey(normalizedScope)]
                : ['protocol', 'notice'];
            const candidates = queueKeys.reduce((all, key) => all.concat(state.pendingQueues[key]), []);
            if (!candidates.length) return false;
            candidates.sort((left, right) => {
                const leftTime = Number(left && left.time_ms);
                const rightTime = Number(right && right.time_ms);
                if (Number.isFinite(leftTime) && Number.isFinite(rightTime) && leftTime !== rightTime) {
                    return leftTime - rightTime;
                }
                return String(left && left._key || '').localeCompare(String(right && right._key || ''));
            });
            const removed = candidates[0];
            const queue = state.pendingQueues[queueKey(removed.scope)];
            const index = queue.findIndex(record => record._key === removed._key);
            if (index >= 0) queue.splice(index, 1);
            syncPendingRecords();
            releaseRecord(removed, reason);
            return true;
        }

        function trimTotalCapacity(scope = null) {
            const scopes = scopeName(scope)
                ? [scopeName(scope)]
                : ['protocol', 'project'];
            scopes.forEach(name => {
                const key = queueKey(name);
                while (state.pendingQueues[key].length > config.maxPendingRecords
                    && releaseOldestPendingRecord('pending_capacity', name)) {
                    // Each scope owns its pending budget; never evict the other scope here.
                }
                while (totalRecordCount(name) > config.maxTotalRecords
                    && releaseOldestPendingRecord('pending_capacity', name)) {
                    // visible + pending is bounded independently for this scope.
                }
            });
            const withinLimits = ['protocol', 'project'].every(name => {
                const key = queueKey(name);
                return state.pendingQueues[key].length <= config.maxPendingRecords
                    && totalRecordCount(name) <= config.maxTotalRecords;
            });
            if (withinLimits) state.allowBufferOverflow = false;
        }

        function setRecordOrder(order) {
            state.recordOrder = order === 'asc' ? 'asc' : 'desc';
            sortVisibleRecords();
        }

        function setMergeSpeed(speed) {
            const nextSpeed = normalizeMergeSpeed(speed);
            state.mergeSpeed = nextSpeed;
            config.mergeIntervalMs = MERGE_SPEED_INTERVALS[nextSpeed];
            clearMergeTimer();
            scheduleMerge();
            emit('recordsChanged', {
                reason: 'merge_speed',
                mergeSpeed: nextSpeed,
                mergeIntervalMs: config.mergeIntervalMs,
                pendingCount: pendingCount(),
            });
            return nextSpeed;
        }

        function pauseMerge() {
            state.mergePaused = true;
            clearMergeTimer();
        }

        function resumeMerge() {
            if (!state.mergePaused) return;
            state.mergePaused = false;
            scheduleMerge();
        }

        function trimRecordsToLimits() {
            ['protocol', 'project'].forEach(scope => {
                const visibleCount = () => state.visibleRecords
                    .filter(record => scopeName(record.scope) === scope).length;
                while (visibleCount() > config.maxVisibleRecords) {
                    let index = -1;
                    for (let cursor = 0; cursor < state.visibleRecords.length; cursor += 1) {
                        if (scopeName(state.visibleRecords[cursor].scope) === scope) {
                            index = cursor;
                            if (state.recordOrder !== 'asc') break;
                        }
                    }
                    if (index < 0) break;
                    const removed = state.visibleRecords.splice(index, 1)[0];
                    releaseRecord(removed, 'visible_capacity');
                }
            });
            syncPendingRecords();
            if (!state.allowBufferOverflow) trimTotalCapacity();
        }

        function beginVisibleReflow(records) {
            const uniqueRecords = [];
            const seenKeys = new Set();
            (Array.isArray(records) ? records : []).forEach(record => {
                if (!record || !record._key || seenKeys.has(record._key)) return;
                seenKeys.add(record._key);
                uniqueRecords.push(record);
            });

            const visibleRecords = [];
            const reflowRecords = [];
            ['protocol', 'project'].forEach(scope => {
                const oldestFirst = uniqueRecords
                    .filter(record => scopeName(record.scope) === scope)
                    .sort((left, right) => compareRecordTime(left, right, 'desc'));
                const directRecords = oldestFirst.length <= config.maxVisibleRecords
                    ? oldestFirst
                    : oldestFirst.slice(0, oldestFirst.length - config.maxVisibleRecords);
                visibleRecords.push(...(state.recordOrder === 'asc'
                    ? directRecords.slice().reverse()
                    : directRecords));
                // The queue is consumed from oldest to newest so every animated record
                // is newer than the current list head/tail at the moment it is inserted.
                if (oldestFirst.length > config.maxVisibleRecords) {
                    reflowRecords.push(...oldestFirst.slice(oldestFirst.length - config.maxVisibleRecords));
                }
            });
            state.visibleRecords = visibleRecords.sort(compareRecordTime);
            state.pendingQueues.reflow = reflowRecords.sort((left, right) => (
                compareRecordTime(left, right, 'desc')
            ));
            syncPendingRecords();
            return state.pendingQueues.reflow.length;
        }

        function promoteReflowRecords() {
            if (!state.pendingQueues.reflow.length) return false;
            const combined = state.visibleRecords.concat(state.pendingQueues.reflow);
            const fits = ['protocol', 'project'].every(scope => combined
                .filter(record => scopeName(record.scope) === scope).length <= config.maxVisibleRecords);
            if (!fits) return false;
            state.visibleRecords = combined.sort(compareRecordTime);
            state.pendingQueues.reflow = [];
            syncPendingRecords();
            return true;
        }

        function rebalanceRecordsToLimits(placement = {}) {
            const preserveVisibleKeys = placement.preserveVisibleKeys;
            const records = allRecords().sort(compareRecordTime);
            const preserveSet = preserveVisibleKeys instanceof Set
                ? preserveVisibleKeys
                : new Set(Array.isArray(preserveVisibleKeys) ? preserveVisibleKeys : []);
            const preferredVisible = [];
            ['protocol', 'project'].forEach(scope => {
                const scopeRecords = records.filter(record => scopeName(record.scope) === scope);
                const preserved = preserveSet.size > 0
                    ? scopeRecords.filter(record => preserveSet.has(record._key)).slice(0, config.maxVisibleRecords)
                    : [];
                const preservedKeys = new Set(preserved.map(record => record._key));
                const fill = scopeRecords
                    .filter(record => !preservedKeys.has(record._key))
                    .slice(0, Math.max(0, config.maxVisibleRecords - preserved.length));
                preferredVisible.push(...preserved, ...fill);
            });
            preferredVisible.sort(compareRecordTime);
            const visibleKeys = new Set(preferredVisible.map(record => record._key));
            const previousVisibleKeys = new Set(state.visibleRecords.map(record => record._key));
            const previousPendingQueues = {
                protocol: state.pendingQueues.protocol.slice(),
                notice: state.pendingQueues.notice.slice(),
            };
            const previousReflowKeys = new Set(state.pendingQueues.reflow.map(record => record._key));
            const remaining = records.filter(record => !visibleKeys.has(record._key));
            const movedFromVisible = remaining.filter(record => previousVisibleKeys.has(record._key));
            state.visibleRecords = preferredVisible;
            state.pendingQueues.reflow = remaining.filter(record => previousReflowKeys.has(record._key));
            ['protocol', 'notice'].forEach(key => {
                const moved = movedFromVisible
                    .filter(record => queueKey(record.scope) === key);
                const existing = previousPendingQueues[key]
                    .filter(record => !visibleKeys.has(record._key));
                // 回退集合按最新到较旧选择，但正序从队首消费，因此最终队列需反转为较旧到最新。
                // 这样每条回退记录都会比当前列表更新，消费后才能稳定插入列表开头。
                if (state.recordOrder === 'asc') {
                    state.pendingQueues[key] = moved.slice().reverse().concat(existing);
                } else {
                    state.pendingQueues[key] = existing.concat(moved);
                }
            });
            syncPendingRecords();
            if (movedFromVisible.length) {
                state.allowBufferOverflow = true;
                clearBufferOverflowTimer();
                state.bufferOverflowTimer = setTimer(() => {
                    state.bufferOverflowTimer = null;
                    state.allowBufferOverflow = false;
                    trimTotalCapacity();
                    emit('recordsChanged', {
                        reason: 'buffer_overflow_trimmed',
                        pendingCount: pendingCount(),
                    });
                }, Math.max(config.mergeIntervalMs * 3, 900));
            }
            return movedFromVisible.length;
        }

        function setBufferLimits(limits = {}, placement = {}) {
            const visibleLimit = Number(limits.maxVisibleRecords);
            const pendingLimit = Number(limits.maxPendingRecords);
            if (Number.isInteger(visibleLimit) && visibleLimit > 0) {
                config.maxVisibleRecords = visibleLimit;
            }
            if (Number.isInteger(pendingLimit) && pendingLimit > 0) {
                config.maxPendingRecords = pendingLimit;
            }
            config.maxTotalRecords = config.maxVisibleRecords + config.maxPendingRecords;
            let movedCount = 0;
            let handledReflow = false;
            if (Array.isArray(placement.reflowVisibleRecords)) {
                beginVisibleReflow(placement.reflowVisibleRecords);
                handledReflow = true;
            } else if (['protocol', 'project'].every(scope => {
                const visibleCount = state.visibleRecords.filter(record => scopeName(record.scope) === scope).length;
                const reflowCount = state.pendingQueues.reflow.filter(record => scopeName(record.scope) === scope).length;
                return visibleCount + reflowCount <= config.maxVisibleRecords;
            })) {
                handledReflow = promoteReflowRecords();
            }
            if (!handledReflow) movedCount = rebalanceRecordsToLimits(placement);
            if (!movedCount && state.allowBufferOverflow) {
                clearBufferOverflowTimer();
                state.allowBufferOverflow = false;
                trimTotalCapacity();
            }
            emit('recordsChanged', {
                reason: 'buffer_limits',
                pendingCount: pendingCount(),
            });
            scheduleMerge();
        }

        function releaseRecord(record, reason) {
            if (!record) return;
            clearRecordResources(record);
            state.recordKeys.delete(record._key);
            if (state.selectedRecordKey === record._key) {
                state.selectedRecordKey = null;
                emit('selectionChanged', {
                    record: null,
                    reason: 'selected_record_evicted',
                    evictedRecord: record,
                });
            }
            emit('recordEvicted', { record, reason });
        }

        function insertNextPendingRecord() {
            state.mergeTimer = null;
            if (state.mergePaused) return;
            const record = takeNextPendingRecord();
            if (!record) return;

            if (state.recordOrder === 'asc') state.visibleRecords.unshift(record);
            else state.visibleRecords.push(record);

            trimRecordsToLimits();
            emit('recordVisible', {
                record,
                pendingCount: pendingCount(),
                fromBuffer: true,
            });
            emit('recordsChanged', { reason: 'merge', pendingCount: pendingCount() });
            if (pendingCount() > 0) {
                scheduleMerge();
            }
        }

        function scheduleMerge() {
            if (state.mergePaused || state.mergeTimer || pendingCount() === 0) return;
            const generation = state.mergeGeneration;
            state.mergeTimer = setTimer(() => {
                if (generation !== state.mergeGeneration) return;
                insertNextPendingRecord();
            }, config.mergeIntervalMs);
        }

        function createObjectUrl(bytes, ref) {
            if (!global.Blob || !global.URL || typeof global.URL.createObjectURL !== 'function') return '';
            try {
                return global.URL.createObjectURL(new global.Blob([bytes], {
                    type: attachmentKindMime(ref && ref.kind),
                }));
            } catch (error) {
                return '';
            }
        }

        function bindAttachmentPayload(key, payload) {
            const pending = state.pendingAttachmentRefs.get(key);
            if (!pending) return false;
            const objectUrl = createObjectUrl(payload.bytes, pending.ref);
            const stored = Object.assign({}, payload, {
                key,
                objectUrl,
                recordKey: pending.recordKey,
                ref: pending.ref,
            });
            state.attachmentPayloads.set(key, stored);
            state.pendingAttachmentRefs.delete(key);
            emit('attachmentAvailable', { key, payload: stored, record: findRecord(pending.recordKey) });
            return true;
        }

        function registerRecordAttachments(record) {
            bodyAttachmentRefs(record).forEach(({ ref }) => {
                const key = attachmentKey(record.scope, record.cache_instance_id, record.seq, ref.attachment_id);
                state.pendingAttachmentRefs.set(key, {
                    recordKey: record._key,
                    ref,
                });
                const payload = state.attachmentPayloads.get(key);
                if (payload) {
                    bindAttachmentPayload(key, payload);
                }
            });
        }

        function persistedBytes(value) {
            if (value instanceof Uint8Array) return new Uint8Array(value);
            if (value instanceof ArrayBuffer) return new Uint8Array(value.slice(0));
            if (ArrayBuffer.isView(value)) {
                return new Uint8Array(value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength));
            }
            return null;
        }

        function clearHydrationState() {
            releaseAllResources();
            state.pendingQueues.protocol = [];
            state.pendingQueues.notice = [];
            state.pendingQueues.reflow = [];
            state.pendingRecords = [];
            state.persistedRecords.clear();
            state.pendingPersistenceRecords.clear();
            state.protocolCursor = null;
            state.projectCursor = null;
            state.protocolDurableCursor = null;
            state.projectDurableCursor = null;
            state.protocolDurableCursorPending = null;
            state.projectDurableCursorPending = null;
            state.uiState.readRecordKeys = [];
            syncPendingRecords();
        }

        function hydrationRecordMap(workspace) {
            const map = new Map();
            const entries = Array.isArray(workspace && workspace.records) ? workspace.records : [];
            entries.forEach(entry => {
                if (!entry || !entry.recordKey || !entry.record || map.has(entry.recordKey)) return;
                const record = Object.assign({}, entry.record, {
                    _key: entry.recordKey,
                    _delivery: entry.record._delivery || 'catch_up',
                });
                if (validateRecord(record, record._delivery)) return;
                map.set(entry.recordKey, record);
                state.persistedRecords.set(entry.recordKey, Object.assign({}, entry, {
                    record,
                    completionState: entry.completionState || 'incomplete',
                    expectedAttachmentKeys: Array.isArray(entry.expectedAttachmentKeys)
                        ? entry.expectedAttachmentKeys.slice()
                        : expectedAttachmentKeys(record),
                    storedAttachmentKeys: Array.isArray(entry.storedAttachmentKeys)
                        ? entry.storedAttachmentKeys.slice()
                        : [],
                    persistenceGeneration: persistenceRecordGeneration[record.scope] || 0,
                }));
            });
            return map;
        }

        function hydrationQueue(map, keys, name, seen) {
            if (!Array.isArray(keys)) throw new Error(name + ' 不是数组');
            return keys.map(key => {
                if (typeof key !== 'string' || !map.has(key) || seen.has(key)) {
                    throw new Error(name + ' 引用了无效或重复记录');
                }
                seen.add(key);
                return map.get(key);
            });
        }

        function hydrateAttachments(workspace, map) {
            const attachments = Array.isArray(workspace && workspace.attachments) ? workspace.attachments : [];
            attachments.forEach(attachment => {
                if (!attachment || !attachment.recordKey || !map.has(attachment.recordKey)
                    || !attachment.attachmentKey) throw new Error('附件引用的记录不存在');
                const record = map.get(attachment.recordKey);
                const entry = state.persistedRecords.get(attachment.recordKey);
                if (!entry || !entry.expectedAttachmentKeys.includes(attachment.attachmentKey)) {
                    throw new Error('附件不属于记录的预期附件集合');
                }
                const bytes = persistedBytes(attachment.payload);
                if (!bytes) throw new Error('附件 payload 不可恢复');
                const ref = bodyAttachmentRefs(record).find(item => attachmentKey(
                    record.scope,
                    record.cache_instance_id,
                    record.seq,
                    String(item.ref.attachment_id),
                ) === attachment.attachmentKey);
                if (!ref) throw new Error('附件引用缺少对应的记录描述');
                const key = attachment.attachmentKey;
                state.attachmentPayloads.set(key, {
                    bytes,
                    sha1: String(attachment.sha1 || ''),
                    capturedSize: bytes.byteLength,
                    valid: true,
                    objectUrl: '',
                });
                entry.storedAttachmentKeys = Array.from(new Set(entry.storedAttachmentKeys.concat(key)));
                if (entry.expectedAttachmentKeys.every(expected => entry.storedAttachmentKeys.includes(expected))) {
                    entry.completionState = 'complete';
                }
                state.pendingAttachmentRefs.set(key, {
                    recordKey: record._key,
                    ref: ref.ref,
                });
                bindAttachmentPayload(key, state.attachmentPayloads.get(key));
            });
        }

        function registerHydratedAttachments(map) {
            map.forEach(record => registerRecordAttachments(record));
        }

        async function hydrateWorkspace() {
            if (!workspaceIdentity.enabled || !persistenceAdapter
                || typeof persistenceAdapter.loadWorkspace !== 'function') {
                return { status: 'disabled', restored: false };
            }
            hydrating = true;
            try {
                const result = await persistenceAdapter.loadWorkspace(workspaceIdentity.snapshotKey);
                if (result && (result.status === 'error' || result.status === 'disabled')) {
                    reportPersistenceFailure(result, 'hydrateWorkspace');
                    return { status: result.status, restored: false, error: result.error };
                }
                if (!result || result.status === 'miss' || !result.workspace) {
                    return { status: result && result.status || 'miss', restored: false };
                }
                const persistenceApi = interactionPersistence();
                const validation = persistenceApi && typeof persistenceApi.validateSnapshot === 'function'
                    ? persistenceApi.validateSnapshot(result.workspace.snapshot)
                    : { valid: true, snapshot: result.workspace.snapshot };
                if (!validation.valid) throw new Error(validation.reason || '快照身份校验失败');

                clearHydrationState();
                const map = hydrationRecordMap(result.workspace);
                const snapshot = validation.snapshot;
                const seen = new Set();
                const visible = hydrationQueue(map, snapshot.visibleKeys, 'visibleKeys', seen);
                const protocol = hydrationQueue(map, snapshot.protocolPendingKeys, 'protocolPendingKeys', seen);
                const notice = hydrationQueue(map, snapshot.noticePendingKeys, 'noticePendingKeys', seen);
                const reflow = hydrationQueue(map, snapshot.reflowKeys, 'reflowKeys', seen);
                state.recordKeys = new Set(map.keys());
                state.visibleRecords = visible;
                state.pendingQueues.protocol = protocol;
                state.pendingQueues.notice = notice;
                state.pendingQueues.reflow = reflow;
                syncPendingRecords();
                state.selectedRecordKey = snapshot.selectedRecordKey && map.has(snapshot.selectedRecordKey)
                    ? snapshot.selectedRecordKey
                    : null;
                state.protocolCursor = normalizeCursor(snapshot.protocolLiveCursor);
                state.projectCursor = normalizeCursor(snapshot.projectLiveCursor);
                state.protocolDurableCursor = normalizeCursor(snapshot.protocolDurableCursor);
                state.projectDurableCursor = normalizeCursor(snapshot.projectDurableCursor);
                state.uiState = Object.assign({}, state.uiState, snapshot.uiState || {}, {
                    readRecordKeys: (Array.isArray(snapshot.readRecordKeys) ? snapshot.readRecordKeys : [])
                        .filter(key => map.has(key)),
                });
                state.recordOrder = state.uiState.sortDirection === 'asc' ? 'asc' : 'desc';
                state.mergeSpeed = normalizeMergeSpeed(state.uiState.mergeSpeed);
                config.mergeIntervalMs = MERGE_SPEED_INTERVALS[state.mergeSpeed];
                state.mergePaused = snapshot.connectionIntent && snapshot.connectionIntent.mergePaused === true;
                state.desiredConnected = snapshot.connectionIntent && snapshot.connectionIntent.desiredConnected === true;
                state.restoreConnectionPaused = snapshot.connectionIntent
                    && snapshot.connectionIntent.connectionPaused === true;
                registerHydratedAttachments(map);
                hydrateAttachments(result.workspace, map);
                if (!state.mergePaused) scheduleMerge();
                if (state.desiredConnected) connect(false);
                return { status: 'hit', restored: true, snapshot };
            } catch (error) {
                clearHydrationState();
                if (typeof persistenceAdapter.deleteSnapshot === 'function') {
                    await persistenceAdapter.deleteSnapshot(workspaceIdentity.snapshotKey);
                }
                reportPersistenceFailure({ status: 'error', error }, 'hydrateWorkspace');
                return { status: 'error', restored: false, error };
            } finally {
                hydrating = false;
            }
        }

        function validateRecord(record, delivery) {
            if (!record || typeof record !== 'object') return '交互记录为空';
            if (!DELIVERY_VALUES.has(delivery)) return '交互投递类型非法';
            if (!SCOPE_VALUES.has(record.scope)) return '交互 scope 非法';
            if (positiveInteger(record.seq) == null || numberId(record.cache_instance_id) <= 0) {
                return '交互记录游标非法';
            }
            if (numberId(record.project_id) !== projectId) return '交互记录项目不匹配';
            if (record.scope === 'protocol' && numberId(record.protocol_id) !== protocolId) {
                return '交互记录协议项不匹配';
            }
            if (record.scope === 'project' && Number(record.protocol_id) !== 0) {
                return '项目 Notice 的 protocol_id 必须为 0';
            }
            return '';
        }

        function completeCatchUpIfReady() {
            if (state.connectionState === STATES.CATCHING_UP
                && state.catchUpExpected > 0
                && state.catchUpReceived >= state.catchUpExpected
                && !state.pendingCommand) {
                setConnectionState(STATES.ACTIVE, { reason: 'open_catch_up_complete' });
                restorePausedConnectionIfReady();
            }
        }

        function receiveInteraction(message) {
            const record = message && message.record;
            const delivery = message && message.delivery;
            const validationError = validateRecord(record, delivery);
            if (validationError) {
                addWarning(validationError, { message: message, kind: 'invalid_interaction' });
                return false;
            }

            const key = buildRecordKey(record.scope, record.cache_instance_id, record.seq);
            if (state.recordKeys.has(key)) {
                const merged = mergeDuplicateInteraction(record, key, delivery);
                // catch_up_count 表示服务端实际发送的消息数，重复记录也必须计入，
                // 否则从持久化快照恢复后重连会永久停留在“补发中”。
                if (delivery === 'catch_up') state.catchUpReceived += 1;
                completeCatchUpIfReady();
                return merged;
            }

            const normalizedRecord = Object.assign({}, record, {
                _key: key,
                _delivery: delivery,
            });
            persistInteractionRecord(normalizedRecord);
            state.recordKeys.add(key);
            setCursor(record.scope, {
                cacheInstanceId: record.cache_instance_id,
                seq: record.seq,
            });
            registerRecordAttachments(normalizedRecord);

            if (delivery === 'catch_up') {
                state.catchUpReceived += 1;
            }
            enqueuePendingRecord(normalizedRecord);
            const scope = scopeName(record.scope);
            if (!state.allowBufferOverflow
                && (state.pendingQueues[queueKey(scope)].length > config.maxPendingRecords
                    || totalRecordCount(scope) > config.maxTotalRecords)) {
                trimTotalCapacity(scope);
                addWarning('本地实时缓冲已满，较早记录已淘汰', { kind: 'buffer_overflow' });
            }
            emit('recordReceived', { record: normalizedRecord, pendingCount: pendingCount() });
            emit('recordsChanged', { reason: 'receive', pendingCount: pendingCount() });
            scheduleMerge();

            completeCatchUpIfReady();
            return true;
        }

        function validateAttachmentHeader(header) {
            if (!header || header.type !== 'attachment') return '附件类型非法';
            if (numberId(header.project_id) !== projectId) return '附件项目不匹配';
            if (!SCOPE_VALUES.has(header.scope)) return '附件 scope 非法';
            if (header.scope === 'protocol' && numberId(header.protocol_id) !== protocolId) return '附件协议项不匹配';
            if (header.scope === 'project' && Number(header.protocol_id) !== 0) return '项目附件 protocol_id 必须为 0';
            if (numberId(header.cache_instance_id) <= 0 || positiveInteger(header.record_seq) == null) return '附件游标非法';
            if (header.attachment_id == null || String(header.attachment_id) === '') return '附件 ID 为空';
            return '';
        }

        function receiveAttachment(data) {
            const parsed = parseAttachmentFrame(data);
            if (parsed.error) {
                addWarning(parsed.error, { kind: 'invalid_attachment' });
                return false;
            }
            const headerError = validateAttachmentHeader(parsed.header);
            if (headerError) {
                addWarning(headerError, { kind: 'invalid_attachment' });
                return false;
            }
            const declaredSize = positiveInteger(parsed.header.captured_size);
            if (declaredSize == null || declaredSize !== parsed.payload.byteLength) {
                addWarning('附件长度与 Header 不一致，已标记为损坏', { kind: 'corrupt_attachment' });
                return false;
            }

            const key = attachmentKey(
                parsed.header.scope,
                parsed.header.cache_instance_id,
                parsed.header.record_seq,
                String(parsed.header.attachment_id),
            );
            const existingPayload = state.attachmentPayloads.get(key);
            if (existingPayload) {
                const sameSize = Number(existingPayload.capturedSize) === parsed.payload.byteLength;
                const incomingSha1 = String(parsed.header.sha1 || '');
                const sameSha1 = !existingPayload.sha1 || !incomingSha1 || existingPayload.sha1 === incomingSha1;
                if (!sameSize || !sameSha1) {
                    addWarning('重复附件内容冲突，保留已存在 payload', {
                        kind: 'duplicate_attachment_conflict',
                        key,
                    });
                    return false;
                }
                return true;
            }
            const pending = state.pendingAttachmentRefs.get(key);
            if (!pending) {
                addWarning('附件未找到对应的交互记录，已忽略', { kind: 'orphan_attachment', key });
                return false;
            }
            const payload = {
                bytes: new Uint8Array(parsed.payload),
                sha1: String(parsed.header.sha1 || ''),
                capturedSize: parsed.payload.byteLength,
                valid: true,
                objectUrl: '',
            };
            if (workspaceIdentity.enabled && persistenceAdapter) {
                queuePersistence('putAttachmentBundle', {
                    snapshotKey: workspaceIdentity.snapshotKey,
                    recordKey: pending.recordKey,
                    recordStorageKey: `${workspaceIdentity.snapshotKey}:${pending.recordKey}`,
                    attachment: {
                        attachmentStorageKey: `${workspaceIdentity.snapshotKey}:${key}:${String(parsed.header.attachment_id)}`,
                        snapshotKey: workspaceIdentity.snapshotKey,
                        recordKey: pending.recordKey,
                        attachmentKey: key,
                        side: pending.ref.side || '',
                        kind: pending.ref.kind || '',
                        sha1: payload.sha1,
                        size: parsed.payload.byteLength,
                        mediaType: attachmentKindMime(pending.ref.kind),
                        payload: payload.bytes,
                        createdAt: now(),
                    },
                }, result => {
                    const entry = state.persistedRecords.get(pending.recordKey);
                    if (!entry || !result || !['stored', 'hit'].includes(result.status)) return;
                    entry.storedAttachmentKeys = Array.isArray(result.storedAttachmentKeys)
                        ? result.storedAttachmentKeys.slice()
                        : entry.storedAttachmentKeys;
                    entry.completionState = result.completionState || entry.completionState;
                    entry.updatedAt = now();
                    persistDurableCursor(entry.scope);
                });
            }
            state.attachmentPayloads.set(key, payload);
            bindAttachmentPayload(key, payload);
            return true;
        }

        function applyReadyCursor(scope, info, shouldInitialize) {
            if (!info || typeof info !== 'object' || numberId(info.cache_instance_id) <= 0) return;
            const current = getCursor(scope);
            const cacheInstanceChanged = current
                && current.cacheInstanceId !== numberId(info.cache_instance_id);
            if (info.cursor_reset === true || cacheInstanceChanged) clearScope(scope);
            if (shouldInitialize || !current || current.cacheInstanceId !== numberId(info.cache_instance_id)) {
                setCursor(scope, {
                    cacheInstanceId: info.cache_instance_id,
                    seq: positiveInteger(info.last_seq) == null ? 0 : info.last_seq,
                });
                const durableName = durableCursorName(scope);
                if (shouldInitialize && !state[durableName]) {
                    state[durableName] = cloneCursorState(getCursor(scope));
                }
            }
            if (info.catch_up_gap === true) {
                addWarning('最近缓存窗口存在缺口，部分交互无法补回', {
                    kind: 'catch_up_gap',
                    scope,
                });
            }
        }

        function handleLiveReady(message, socket) {
            if (socket && state.socket !== socket) return;
            if (!message || numberId(message.project_id) !== projectId
                || numberId(message.protocol_id) !== protocolId
                || numberId(message.session_id) <= 0) {
                addWarning('live_ready 的项目、协议项或 session_id 不匹配', { kind: 'invalid_live_ready' });
                terminateSocket('invalid live_ready', socket);
                return;
            }

            const sessionId = numberId(message.session_id);
            const boundSessionId = socket ? socketSessionIds.get(socket) : 0;
            if (boundSessionId && boundSessionId !== sessionId) {
                addWarning('同一 WebSocket 的 session_id 发生变化', {
                    kind: 'session_changed',
                    previousSessionId: boundSessionId,
                    sessionId,
                });
                terminateSocket('session changed', socket);
                return;
            }
            if (socket) socketSessionIds.set(socket, sessionId);

            const trigger = message.trigger === 'resume' ? 'resume' : 'open';
            const hadProtocolCursor = Boolean(state.protocolCursor);
            const hadProjectCursor = Boolean(state.projectCursor);
            state.sessionId = sessionId;
            state.socketSessionId = sessionId;
            if (trigger === 'open') {
                state.acceptedClientSeq = 0;
                state.nextClientSeq = 1;
            }

            applyReadyCursor('protocol', message.protocol_cache_info, !hadProtocolCursor);
            applyReadyCursor('project', message.project_cache_info, !hadProjectCursor);

            const protocolCount = positiveInteger(message.protocol_cache_info && message.protocol_cache_info.catch_up_count) || 0;
            const projectCount = positiveInteger(message.project_cache_info && message.project_cache_info.catch_up_count) || 0;
            state.catchUpExpected = protocolCount + projectCount;
            state.catchUpReceived = 0;
            emit('liveReady', {
                message,
                trigger,
                protocolCursor: cloneCursorState(state.protocolCursor),
                projectCursor: cloneCursorState(state.projectCursor),
            });

            if (trigger === 'resume') {
                setConnectionState(STATES.CATCHING_UP, { reason: 'resume_live_ready' });
            } else if (state.catchUpExpected === 0) {
                state.reconnectAttempt = 0;
                setConnectionState(STATES.ACTIVE, { reason: 'open_live_ready' });
            } else {
                setConnectionState(STATES.CATCHING_UP, { reason: 'open_live_ready' });
            }
            restorePausedConnectionIfReady();
        }

        function restorePausedConnectionIfReady() {
            if (!state.restoreConnectionPaused || state.connectionState !== STATES.ACTIVE
                || state.pendingCommand || !state.sessionId) return;
            state.restoreConnectionPaused = false;
            pause();
        }

        function clearCommandTimer() {
            if (state.commandTimer) clearTimer(state.commandTimer);
            state.commandTimer = null;
        }

        function clearReconnectTimer() {
            if (state.reconnectTimer) clearTimer(state.reconnectTimer);
            state.reconnectTimer = null;
        }

        function clearMergeTimer() {
            state.mergeGeneration += 1;
            if (state.mergeTimer) clearTimer(state.mergeTimer);
            state.mergeTimer = null;
        }

        function scheduleReconnect(reason) {
            if (destroyed || !state.desiredConnected || intentionalClose || state.reconnectTimer) return;
            if (state.reconnectAttempt >= config.reconnectDelaysMs.length) {
                setConnectionState(STATES.ERROR, { reason: 'reconnect_exhausted' });
                addWarning('连续自动重连失败，等待手动重新连接', { kind: 'reconnect_exhausted' });
                return;
            }
            const delay = config.reconnectDelaysMs[state.reconnectAttempt];
            state.reconnectAttempt += 1;
            setConnectionState(STATES.RECONNECT_WAIT, { delayMs: delay, reason });
            state.reconnectTimer = setTimer(() => {
                state.reconnectTimer = null;
                if (state.desiredConnected && !destroyed) connect(false);
            }, delay);
        }

        function isNormalShutdown(event) {
            const reason = String(event && (event.reason || '')).toLowerCase();
            return Number(event && event.code) === 1000
                && (reason.includes('shutdown') || reason.includes('normal'));
        }

        function terminateSocket(reason, expectedSocket) {
            const socket = state.socket;
            if (!socket || (expectedSocket && socket !== expectedSocket)) return;
            try {
                if (typeof socket.close === 'function') socket.close(1011, String(reason || 'connection error'));
            } catch (error) {
                handleSocketClose({ code: 1011, reason: String(reason || '') }, socket);
            }
        }

        function handleSocketClose(event = {}, socket) {
            // 重连期间旧 WebSocket 可能晚于新连接触发 close；旧连接不能
            // 清空新连接的 state，也不能覆盖新的补发状态。
            if (socket && state.socket !== socket) return;
            clearCommandTimer();
            socketSessionIds.delete(socket);
            const wasIntentional = intentionalClose || !state.desiredConnected;
            state.socket = null;
            state.sessionId = 0;
            state.socketSessionId = 0;
            state.pendingCommand = null;
            queryInFlight = false;
            emit('closed', event);

            if (destroyed || wasIntentional || isNormalShutdown(event)) {
                if (isNormalShutdown(event)) state.desiredConnected = false;
                setConnectionState(STATES.DISCONNECTED, { reason: event.reason || 'closed' });
                return;
            }
            scheduleReconnect(event.reason || 'socket_closed');
        }

        function handleSocketError(event, socket) {
            if (state.socket !== socket) return;
            emit('socketError', event);
            if (state.socket && state.connectionState === STATES.CONNECTING) {
                terminateSocket('socket error', socket);
            }
        }

        function handleSocketOpen(socket) {
            if (state.socket !== socket) return;
            emit('socketOpen', { url: state.socket && state.socket.url });
            setConnectionState(STATES.CONNECTING, { reason: 'socket_open' });
        }

        function handleTextMessage(text, socket) {
            if (socket && state.socket !== socket) return;
            const message = typeof text === 'string' ? safeJsonParse(text) : null;
            if (!message || typeof message.type !== 'string') {
                addWarning('实时消息不是有效 JSON', { kind: 'invalid_message' });
                return;
            }
            if (message.type !== 'live_ready' && socket) {
                const boundSessionId = socketSessionIds.get(socket);
                if (!boundSessionId || state.sessionId !== boundSessionId) return;
                if (message.session_id != null && numberId(message.session_id) !== boundSessionId) {
                    addWarning('实时消息 session_id 与当前连接不匹配', {
                        kind: 'stale_session_message',
                        expectedSessionId: boundSessionId,
                        sessionId: message.session_id,
                    });
                    terminateSocket('stale session message', socket);
                    return;
                }
            }
            switch (message.type) {
                case 'live_ready':
                    handleLiveReady(message, socket);
                    break;
                case 'interaction':
                    receiveInteraction(message);
                    break;
                case 'state':
                    handleCommandAck(message);
                    break;
                case 'error':
                    handleBusinessError(message);
                    break;
                default:
                    addWarning(`未知实时消息类型：${message.type}`, { kind: 'unknown_message' });
            }
        }

        function handleMessage(data, socket) {
            if (typeof data === 'string') {
                handleTextMessage(data, socket);
                return;
            }
            binaryChain = binaryChain.then(async () => {
                if (socket && (state.socket !== socket || state.sessionId !== socketSessionIds.get(socket))) return;
                if (data instanceof Blob) {
                    receiveAttachment(await data.arrayBuffer());
                } else {
                    receiveAttachment(data);
                }
            }).catch(error => addWarning(error.message || '附件帧处理失败', { kind: 'invalid_attachment' }));
        }

        function assignSocketHandlers(socket) {
            socket.binaryType = 'arraybuffer';
            socket.onopen = () => handleSocketOpen(socket);
            socket.onmessage = event => {
                if (state.socket !== socket) return;
                handleMessage(event && event.data, socket);
            };
            socket.onerror = event => handleSocketError(event, socket);
            socket.onclose = event => handleSocketClose(event, socket);
        }

        function connect(manual = true) {
            if (destroyed) return false;
            if (manual) {
                state.desiredConnected = true;
                state.reconnectAttempt = 0;
            }
            intentionalClose = false;
            clearReconnectTimer();
            if (state.socket) return true;

            if (!projectId || !protocolId) {
                setConnectionState(STATES.ERROR, { reason: 'invalid_identity' });
                addWarning('缺少项目 ID 或协议项 ID，无法建立实时连接', { kind: 'invalid_identity' });
                return false;
            }

            const url = buildWebSocketUrl({
                protocolId,
                apiBaseUrl: options.apiBaseUrl != null
                    ? options.apiBaseUrl
                    : (KitProxy.config && KitProxy.config.apiBaseUrl),
                includeProjectNotice: options.includeProjectNotice !== false,
                protocolCursor: state.protocolDurableCursor || state.protocolCursor,
                projectCursor: state.projectDurableCursor || state.projectCursor,
            });
            setConnectionState(STATES.CONNECTING, { url, reason: 'connect' });
            // client_seq 属于单条 WebSocket session；异常重连后必须从 1 重新开始。
            state.sessionId = 0;
            state.socketSessionId = 0;
            state.acceptedClientSeq = 0;
            state.nextClientSeq = 1;
            state.pendingCommand = null;

            try {
                const factory = options.transportFactory
                    || (KitProxy.config && KitProxy.config.apiMode === 'mock'
                        ? KitProxy.protocolInteractionLive.createMockTransport
                        : null);
                state.socket = factory
                    ? factory(url, {
                        client: api,
                        projectId,
                        protocolId,
                        protocolType: state.protocolType,
                    })
                    : new global.WebSocket(url);
                if (!state.socket) throw new Error('WebSocket transport 创建失败');
                socketSessionIds.delete(state.socket);
                assignSocketHandlers(state.socket);
                return true;
            } catch (error) {
                socketSessionIds.delete(state.socket);
                state.socket = null;
                state.socketSessionId = 0;
                addWarning(error.message || 'WebSocket 创建失败', { kind: 'socket_create_error' });
                scheduleReconnect('socket_create_error');
                return false;
            }
        }

        function disconnect() {
            state.desiredConnected = false;
            intentionalClose = true;
            clearReconnectTimer();
            clearCommandTimer();
            if (!state.socket) {
                setConnectionState(STATES.DISCONNECTED, { reason: 'user_disconnect' });
                return;
            }
            setConnectionState(STATES.DISCONNECTING, { reason: 'user_disconnect' });
            try {
                state.socket.close(1000, 'client disconnect');
            } catch (error) {
                handleSocketClose({ code: 1000, reason: 'client disconnect' }, state.socket);
            }
        }

        function sendCommand(command) {
            if (!state.socket || !state.sessionId) {
                addWarning('实时连接尚未完成初始化', { kind: 'command_unavailable' });
                return false;
            }
            if (state.pendingCommand) {
                addWarning('已有实时命令正在等待确认', { kind: 'command_in_flight' });
                return false;
            }
            const clientSeq = state.nextClientSeq;
            state.nextClientSeq += 1;
            const message = {
                type: 'command',
                command,
                session_id: state.sessionId,
                client_seq: clientSeq,
                timestamp: now(),
            };
            try {
                state.socket.send(JSON.stringify(message));
            } catch (error) {
                addWarning(error.message || '实时命令发送失败', { kind: 'command_send_error' });
                terminateSocket('command send failed');
                return false;
            }
            state.pendingCommand = { command, clientSeq };
            state.commandTimer = setTimer(() => {
                state.commandTimer = null;
                if (!state.pendingCommand || state.pendingCommand.clientSeq !== clientSeq) return;
                state.pendingCommand = null;
                addWarning('实时命令确认超时，正在重建连接', { kind: 'command_timeout' });
                terminateSocket('command timeout');
            }, config.commandTimeoutMs);
            emit('commandSent', message);
            return true;
        }

        function pause() {
            if (state.connectionState !== STATES.ACTIVE) return false;
            setConnectionState(STATES.PAUSING, { reason: 'pause_requested' });
            if (!sendCommand('pause')) {
                setConnectionState(STATES.ACTIVE, { reason: 'pause_send_failed' });
                return false;
            }
            return true;
        }

        function resume() {
            if (state.connectionState !== STATES.PAUSED) return false;
            setConnectionState(STATES.RESUMING, { reason: 'resume_requested' });
            if (!sendCommand('resume')) {
                setConnectionState(STATES.PAUSED, { reason: 'resume_send_failed' });
                return false;
            }
            return true;
        }

        function queryState() {
            if (queryInFlight || state.pendingCommand) return false;
            queryInFlight = true;
            if (!sendCommand('query_state')) {
                queryInFlight = false;
                return false;
            }
            return true;
        }

        function stateFromWire(value) {
            const map = {
                init: STATES.CONNECTING,
                catching_up: STATES.CATCHING_UP,
                active: STATES.ACTIVE,
                paused: STATES.PAUSED,
                closed: STATES.DISCONNECTED,
            };
            return map[String(value || '').toLowerCase()] || null;
        }

        function applyAckCursors(message) {
            const protocolCursor = cursorFromWire(message.protocol_cursor);
            const projectCursor = cursorFromWire(message.project_cursor);
            if (protocolCursor) setCursor('protocol', protocolCursor);
            if (projectCursor) setCursor('project', projectCursor);
        }

        function handleCommandAck(message) {
            const pending = state.pendingCommand;
            const ackSeq = positiveInteger(message && message.client_seq);
            if (!pending || ackSeq !== pending.clientSeq) {
                addWarning('忽略与当前命令不匹配的 ACK', { kind: 'stale_ack', message });
                return;
            }
            clearCommandTimer();
            state.pendingCommand = null;
            const command = pending.command;
            if (message.ok !== true) {
                queryInFlight = false;
                addWarning(message.error_message || '实时命令执行失败', { kind: 'command_rejected', message });
                if (command === 'pause') setConnectionState(STATES.ACTIVE, { reason: 'pause_rejected' });
                if (command === 'resume') setConnectionState(STATES.PAUSED, { reason: 'resume_rejected' });
                if (command === 'query_state') terminateSocket('query_state rejected');
                emit('commandAck', message);
                return;
            }

            state.acceptedClientSeq = positiveInteger(message.accepted_seq) || ackSeq;
            applyAckCursors(message);
            const wireState = stateFromWire(message.state);
            if (command === 'pause') setConnectionState(STATES.PAUSED, { reason: 'pause_ack' });
            else if (command === 'resume') setConnectionState(STATES.ACTIVE, { reason: 'resume_ack' });
            else if (wireState && wireState !== STATES.CONNECTING) setConnectionState(wireState, { reason: 'query_state_ack' });
            queryInFlight = false;
            emit('commandAck', message);
        }

        function handleBusinessError(message) {
            addWarning(`实时连接业务错误：${message.code || 'unknown'}`, {
                kind: 'business_error',
                message,
            });
            if (message.code === 'bad_message' && !queryInFlight && !state.pendingCommand) {
                queryState();
            }
            emit('businessError', message);
        }

        function selectRecord(recordOrKey) {
            const key = typeof recordOrKey === 'string' ? recordOrKey : recordOrKey && recordOrKey._key;
            if (!key || !findRecord(key)) return false;
            state.selectedRecordKey = key;
            emit('selectionChanged', { record: findRecord(key) });
            return true;
        }

        function releaseAllResources() {
            state.visibleRecords.forEach(clearRecordResources);
            state.pendingQueues.protocol.forEach(clearRecordResources);
            state.pendingQueues.notice.forEach(clearRecordResources);
            state.pendingQueues.reflow.forEach(clearRecordResources);
            state.attachmentPayloads.forEach(payload => {
                if (payload.objectUrl && global.URL && global.URL.revokeObjectURL) {
                    global.URL.revokeObjectURL(payload.objectUrl);
                }
            });
            state.visibleRecords = [];
            state.pendingQueues.protocol = [];
            state.pendingQueues.notice = [];
            state.pendingQueues.reflow = [];
            state.pendingRecords = [];
            state.recordKeys.clear();
            state.attachmentPayloads.clear();
            state.pendingAttachmentRefs.clear();
            state.selectedRecordKey = null;
        }

        /**
         * 清空当前浏览器侧实时记录，保留连接、游标和服务端缓存状态。
         * 后续新到达的交互仍会继续进入列表。
         */
        function clearRecords() {
            releaseAllResources();
            clearPersistedRecordEntries();
            state.uiState.readRecordKeys = [];
            const snapshot = persistenceSnapshot();
            persistScopeReset('protocol', snapshot);
            persistScopeReset('project', snapshot);
            emit('recordsReset', { scope: 'all', reason: 'local_clear' });
            emit('recordsChanged', { reason: 'local_clear', pendingCount: 0 });
            schedulePersistenceSnapshot(true);
        }

        function getAttachment(recordOrKey, refOrId) {
            const record = typeof recordOrKey === 'string' ? findRecord(recordOrKey) : recordOrKey;
            if (!record) return null;
            const attachmentId = typeof refOrId === 'string' ? refOrId : refOrId && refOrId.attachment_id;
            if (attachmentId == null) return null;
            return state.attachmentPayloads.get(attachmentKey(
                record.scope,
                record.cache_instance_id,
                record.seq,
                String(attachmentId),
            )) || null;
        }

        function destroy() {
            if (destroyed) return;
            destroyed = true;
            state.desiredConnected = false;
            intentionalClose = true;
            clearReconnectTimer();
            clearCommandTimer();
            clearMergeTimer();
            clearBufferOverflowTimer();
            if (state.socket) {
                try { state.socket.close(1000, 'page cleanup'); } catch (error) { /* socket already closed */ }
                socketSessionIds.delete(state.socket);
            }
            state.socket = null;
            state.socketSessionId = 0;
            releaseAllResources();
            if (global.removeEventListener) {
                global.removeEventListener('online', handleOnline);
                global.removeEventListener('offline', handleOffline);
            }
            setConnectionState(STATES.DISCONNECTED, { reason: 'destroy', force: true });
            emit('destroyed', {});
            listeners.clear();
        }

        function handleOnline() {
            if (state.desiredConnected && state.connectionState === STATES.RECONNECT_WAIT) {
                clearReconnectTimer();
                connect(false);
            }
        }

        function handleOffline() {
            if (!state.desiredConnected) return;
            clearReconnectTimer();
            if (state.connectionState === STATES.RECONNECT_WAIT) {
                setConnectionState(STATES.RECONNECT_WAIT, { reason: 'offline', force: true });
            }
        }

        const api = {
            STATES,
            state,
            on,
            connect,
            disconnect,
            pause,
            resume,
            queryState,
            selectRecord,
            setRecordOrder,
            setMergeSpeed,
            pauseMerge,
            resumeMerge,
            setBufferLimits,
            clearRecords,
            getAttachment,
            getRecord: findRecord,
            hydrateWorkspace,
            getRestoreMarker() {
                return persistence && typeof persistence.readValidRestoreMarker === 'function'
                    ? persistence.readValidRestoreMarker(
                        workspaceIdentity,
                        options.sessionStorage || global.sessionStorage,
                        now,
                    )
                    : null;
            },
            updatePersistenceUiState,
            flushPersistence() {
                return (async () => {
                    if (persistenceSnapshotTimer) {
                        clearTimer(persistenceSnapshotTimer);
                        persistenceSnapshotTimer = null;
                        persistenceQueueSnapshot();
                    }
                    let observed = null;
                    do {
                        observed = persistenceChain;
                        await observed;
                    } while (observed !== persistenceChain);
                    return persistenceChain;
                })();
            },
            getPersistenceState() {
                return {
                    status: state.persistenceStatus,
                    snapshotKey: state.persistenceSnapshotKey,
                    enabled: Boolean(workspaceIdentity.enabled && persistenceAdapter),
                };
            },
            getState() {
                return Object.assign({}, state, {
                    visibleRecords: state.visibleRecords.slice(),
                    pendingRecords: state.pendingRecords.slice(),
                    pendingQueues: {
                        protocol: state.pendingQueues.protocol.slice(),
                        notice: state.pendingQueues.notice.slice(),
                        reflow: state.pendingQueues.reflow.slice(),
                    },
                    maxVisibleRecords: config.maxVisibleRecords,
                    maxPendingRecords: config.maxPendingRecords,
                    maxTotalRecords: config.maxTotalRecords,
                    mergeSpeed: state.mergeSpeed,
                    mergeIntervalMs: config.mergeIntervalMs,
                    mergePaused: state.mergePaused,
                    allowBufferOverflow: state.allowBufferOverflow,
                    protocolCursor: cloneCursorState(state.protocolCursor),
                    projectCursor: cloneCursorState(state.projectCursor),
                    protocolDurableCursor: cloneCursorState(state.protocolDurableCursor),
                    projectDurableCursor: cloneCursorState(state.projectDurableCursor),
                    uiState: Object.assign({}, state.uiState, {
                        readRecordKeys: Array.isArray(state.uiState.readRecordKeys)
                            ? state.uiState.readRecordKeys.slice()
                            : [],
                    }),
                    socketSessionId: state.socketSessionId,
                });
            },
            receiveText: handleTextMessage,
            receiveBinary: receiveAttachment,
            destroy,
            buildUrl() {
                return buildWebSocketUrl({
                    protocolId,
                    apiBaseUrl: options.apiBaseUrl != null
                        ? options.apiBaseUrl
                        : (KitProxy.config && KitProxy.config.apiBaseUrl),
                    includeProjectNotice: options.includeProjectNotice !== false,
                    protocolCursor: state.protocolDurableCursor || state.protocolCursor,
                    projectCursor: state.projectDurableCursor || state.projectCursor,
                });
            },
        };

        if (global.addEventListener) {
            global.addEventListener('online', handleOnline);
            global.addEventListener('offline', handleOffline);
        }

        return api;
    }

    let mockSessionId = 9000;
    const mockRuntime = new Map();

    function mockRecord(projectId, protocolId, scope, seq, overrides = {}) {
        const isNotice = scope === 'project';
        return Object.assign({
            seq,
            scope,
            project_id: projectId,
            protocol_id: isNotice ? 0 : protocolId,
            cache_instance_id: isNotice ? 2002 : 1001,
            protocol_type: isNotice ? 'http' : 'http',
            time_ms: Date.now(),
            peer_addr: isNotice ? '127.0.0.1:50000' : '127.0.0.1:50100',
            result: isNotice ? 'route_not_found' : 'matched',
            error_message: isNotice ? '未找到可处理的路由' : '',
            request: {
                meta: isNotice ? { method: 'GET', path: '/notice' } : { method: 'GET', path: '/api/health' },
                head_text: isNotice ? 'GET /notice HTTP/1.1' : 'GET /api/health HTTP/1.1',
                body: {
                    kind: 'empty', expect_kind: 'empty', size: 0, captured_size: 0,
                    truncated: false, sha1: '', text: '', error_message: '', attachments: [],
                },
            },
            response: {
                meta: { status_code: isNotice ? 404 : 200 },
                head_text: isNotice ? 'HTTP/1.1 404 Not Found' : 'HTTP/1.1 200 OK',
                body: {
                    kind: isNotice ? 'text' : 'json',
                    expect_kind: isNotice ? 'empty' : 'json',
                    size: isNotice ? 9 : 12,
                    captured_size: isNotice ? 9 : 12,
                    truncated: false, sha1: '',
                    text: isNotice ? 'not found' : '{"ok":true}',
                    error_message: '', attachments: [],
                },
            },
        }, overrides);
    }

    function isCustomTcpProtocolType(protocolType) {
        const normalized = String(protocolType || '').trim().toLowerCase();
        return Number(protocolType) === 2
            || normalized === 'tcp'
            || normalized === 'custom_tcp'
            || normalized === 'custom-tcp';
    }

    function mockTcpSide(functionCode, headerText, bodyText) {
        const body = String(bodyText || '').trim();
        const bodySize = body ? body.split(/\s+/).length : 0;
        const header = String(headerText || '').trim();
        const rawHex = [header, body].filter(Boolean).join(' ');
        return {
            meta: {
                function_code: functionCode,
                header_size: 8,
                body_size: bodySize,
            },
            head_text: header,
            body: {
                kind: 'binary',
                expect_kind: 'binary',
                size: bodySize,
                captured_size: bodySize,
                truncated: false,
                sha1: '',
                text: body,
                error_message: '',
                attachments: [],
            },
            raw_packet: {
                raw_hex: rawHex,
                attachments: [],
            },
        };
    }

    function mockTcpRecord(projectId, protocolId, scope, seq, overrides = {}) {
        const isNotice = scope === 'project';
        const requestHeader = isNotice
            ? '48 39 30 30 30 00 00 00'
            : '48 31 30 30 30 00 00 04';
        const responseHeader = isNotice
            ? '48 46 46 46 46 00 00 00'
            : '48 31 30 38 30 00 00 02';
        return Object.assign({
            seq,
            scope,
            project_id: projectId,
            protocol_id: isNotice ? 0 : protocolId,
            cache_instance_id: isNotice ? 2002 : 1001,
            protocol_type: 'custom_tcp',
            time_ms: Date.now(),
            peer_addr: isNotice ? '127.0.0.1:50000' : '127.0.0.1:50100',
            result: isNotice ? 'route_not_found' : 'matched',
            error_message: isNotice ? '未找到可处理的路由' : '',
            request: mockTcpSide(
                isNotice ? 'H9000' : 'H1000',
                requestHeader,
                isNotice ? '' : '01 02 03 04',
            ),
            response: mockTcpSide(
                isNotice ? 'HFFFF' : 'H1080',
                responseHeader,
                isNotice ? '' : '00 01',
            ),
        }, overrides);
    }

    function buildMockAttachmentFrame(header, bytes) {
        const headerBytes = new global.TextEncoder().encode(JSON.stringify(header));
        const payload = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
        const frame = new Uint8Array(4 + headerBytes.length + payload.length);
        new DataView(frame.buffer).setUint32(0, headerBytes.length, false);
        frame.set(headerBytes, 4);
        frame.set(payload, 4 + headerBytes.length);
        return frame.buffer;
    }

    /**
     * Mock transport 只模拟 WebSocket 事件和当前后端消息组，不把 Mock 分支散落到抽屉 UI。
     * @param {string} url
     * @param {{projectId: number, protocolId: number, protocolType?: string|number}} context
     * @returns {any}
     */
    function createMockTransport(url, context) {
        const projectId = numberId(context.projectId);
        const protocolId = numberId(context.protocolId);
        const useTcpRecords = isCustomTcpProtocolType(context.protocolType);
        const runtimeKey = `${projectId}:${protocolId}`;
        const runtime = mockRuntime.get(runtimeKey) || {
            protocolSeq: 0,
            projectSeq: 0,
            pausedRecords: [],
            liveIntervalMs: 2500,
            resetNextResume: false,
            gapNextReady: false,
            failNextConnection: false,
        };
        if (!Number.isFinite(Number(runtime.liveIntervalMs)) || Number(runtime.liveIntervalMs) <= 0) {
            runtime.liveIntervalMs = 2500;
        }
        mockRuntime.set(runtimeKey, runtime);
        const buildRecord = (scope, seq, overrides = {}) => useTcpRecords
            ? mockTcpRecord(projectId, protocolId, scope, seq, overrides)
            : mockRecord(projectId, protocolId, scope, seq, overrides);
        const failThisConnection = runtime.failNextConnection === true;
        runtime.failNextConnection = false;

        const timers = new Set();
        const socket = {
            url,
            readyState: 0,
            binaryType: 'arraybuffer',
            sent: [],
            closeCalls: [],
            _closed: false,
            _paused: false,
            _sessionId: ++mockSessionId,
            _schedule(callback, delay = 0) {
                const timer = global.setTimeout(() => {
                    timers.delete(timer);
                    if (!socket._closed) callback();
                }, delay);
                timers.add(timer);
            },
            _emitText(message) {
                if (socket.onmessage) socket.onmessage({ data: JSON.stringify(message) });
            },
            _emitRecord(record, delivery) {
                socket._emitText({ type: 'interaction', delivery, record });
            },
            _cursor(scope) {
                return {
                    cache_instance_id: scope === 'project' ? 2002 : 1001,
                    seq: scope === 'project' ? runtime.projectSeq : runtime.protocolSeq,
                };
            },
            _ready(trigger, catchUpCount = 0) {
                const reset = trigger === 'resume' && runtime.resetNextResume;
                const gap = trigger === 'resume' && runtime.gapNextReady;
                if (trigger === 'resume') {
                    runtime.resetNextResume = false;
                    runtime.gapNextReady = false;
                }
                socket._emitText({
                    type: 'live_ready',
                    trigger,
                    project_id: projectId,
                    protocol_id: protocolId,
                    session_id: socket._sessionId,
                    protocol_cache_info: {
                        cache_instance_id: 1001,
                        last_seq: runtime.protocolSeq,
                        live_start_seq: runtime.protocolSeq + 1,
                        catch_up_count: catchUpCount,
                        catch_up_gap: gap,
                        cursor_reset: reset,
                    },
                    project_cache_info: {
                        cache_instance_id: 2002,
                        last_seq: runtime.projectSeq,
                        live_start_seq: runtime.projectSeq + 1,
                        catch_up_count: 0,
                        catch_up_gap: false,
                        cursor_reset: false,
                    },
                    timestamp: Date.now(),
                });
            },
            _sendInitialRecords() {
                if (useTcpRecords) {
                    runtime.protocolSeq += 1;
                    socket._emitRecord(buildRecord('protocol', runtime.protocolSeq), 'live');

                    runtime.protocolSeq += 1;
                    socket._emitRecord(buildRecord('protocol', runtime.protocolSeq, {
                        result: 'request_mismatch',
                        error_message: '请求功能码与协议项期望不一致',
                        request: mockTcpSide(
                            'H9999',
                            '48 39 39 39 39 00 00 04',
                            '01 02 03 04',
                        ),
                    }), 'live');

                    runtime.protocolSeq += 1;
                    socket._emitRecord(buildRecord('protocol', runtime.protocolSeq, {
                        request: mockTcpSide(
                            'H1001',
                            '48 31 30 30 31 00 00 02',
                            '0A 0B',
                        ),
                        response: mockTcpSide(
                            'H1081',
                            '48 31 30 38 31 00 00 02',
                            '00 02',
                        ),
                    }), 'live');
                } else {
                    runtime.protocolSeq += 1;
                    const imageRecordSeq = runtime.protocolSeq;
                    const httpRecord = buildRecord('protocol', runtime.protocolSeq, {
                        response: {
                            meta: { status_code: 200 },
                            head_text: 'HTTP/1.1 200 OK',
                            body: {
                                kind: 'image', expect_kind: 'image', size: 3, captured_size: 3,
                                truncated: false, sha1: 'mock-image-sha1', text: '', error_message: '',
                                attachments: [{ attachment_id: 'mock-image', side: 'response', flag: 'response.body', kind: 'image', size: 3, captured_size: 3, truncated: false, binary_available: true, sha1: 'mock-image-sha1' }],
                            },
                        },
                    });
                    socket._emitRecord(httpRecord, 'live');
                    socket._schedule(() => socket.onmessage && socket.onmessage({ data: buildMockAttachmentFrame({
                        type: 'attachment', scope: 'protocol', project_id: projectId, protocol_id: protocolId,
                        cache_instance_id: 1001, record_seq: imageRecordSeq, attachment_id: 'mock-image',
                        captured_size: 3, sha1: 'mock-image-sha1',
                    }, new Uint8Array([137, 80, 78])) }), 10);

                    runtime.protocolSeq += 1;
                    socket._emitRecord(buildRecord('protocol', runtime.protocolSeq, {
                        result: 'request_mismatch',
                        error_message: '请求 Body 与协议项期望不一致',
                        request: {
                            meta: { method: 'POST', path: '/api/check' },
                            head_text: 'POST /api/check HTTP/1.1',
                            body: {
                                kind: 'text', expect_kind: 'json', size: 7, captured_size: 7,
                                truncated: false, sha1: '', text: 'invalid',
                                error_message: 'JSON 解析失败', attachments: [],
                            },
                        },
                    }), 'live');

                    runtime.protocolSeq += 1;
                    socket._emitRecord(buildRecord('protocol', runtime.protocolSeq, {
                        protocol_type: 'custom_tcp',
                        request: {
                            meta: { function_code: 'H1000', header_size: 8, body_size: 4 },
                            head_text: '48 31 30 30 30 00 00 04',
                            body: {
                                kind: 'binary', expect_kind: 'binary', size: 4, captured_size: 4,
                                truncated: false, sha1: 'mock-tcp-request', text: '01 02 03 04',
                                error_message: '', attachments: [],
                            },
                            raw_packet: { raw_hex: '48 31 30 30 30 00 00 04 01 02 03 04', attachments: [] },
                        },
                        response: {
                            meta: { function_code: 'H1080', header_size: 8, body_size: 2 },
                            head_text: '48 31 30 38 30 00 00 02',
                            body: {
                                kind: 'binary', expect_kind: 'binary', size: 2, captured_size: 2,
                                truncated: false, sha1: 'mock-tcp-response', text: '00 01',
                                error_message: '', attachments: [],
                            },
                            raw_packet: { raw_hex: '48 31 30 38 30 00 00 02 00 01', attachments: [] },
                        },
                    }), 'live');
                }

                runtime.projectSeq += 1;
                socket._schedule(() => socket._emitRecord(
                    buildRecord('project', runtime.projectSeq, {
                        error_message: '项目级 Notice：没有协议项可以处理该请求',
                    }),
                    'live',
                ), 40);
            },
            _scheduleLiveRecord(delay = runtime.liveIntervalMs) {
                if (runtime.liveEnabled === false) return;
                const interval = Math.max(1, Number(delay) || 2500);
                socket._schedule(() => {
                    if (socket._closed) return;
                    runtime.protocolSeq += 1;
                    const record = useTcpRecords
                        ? buildRecord('protocol', runtime.protocolSeq)
                        : buildRecord('protocol', runtime.protocolSeq, {
                            request: {
                                meta: { method: 'GET', path: `/api/live/${runtime.protocolSeq}` },
                                head_text: `GET /api/live/${runtime.protocolSeq} HTTP/1.1`,
                                body: {
                                    kind: 'empty', expect_kind: 'empty', size: 0, captured_size: 0,
                                    truncated: false, sha1: '', text: '', error_message: '', attachments: [],
                                },
                            },
                            response: {
                                meta: { status_code: 200 },
                                head_text: 'HTTP/1.1 200 OK',
                                body: {
                                    kind: 'json', expect_kind: 'json', size: 20, captured_size: 20,
                                    truncated: false, sha1: '', text: JSON.stringify({ ok: true, seq: runtime.protocolSeq }),
                                    error_message: '', attachments: [],
                                },
                            },
                        });
                    if (socket._paused) runtime.pausedRecords.push(record);
                    else socket._emitRecord(record, 'live');
                    socket._scheduleLiveRecord(interval);
                }, interval);
            },
            send(payload) {
                const command = safeJsonParse(payload);
                socket.sent.push(command);
                if (!command) return;
                const cursorPayload = {
                    protocol_cursor: socket._cursor('protocol'),
                    project_cursor: socket._cursor('project'),
                    client_seq: command.client_seq,
                    accepted_seq: command.client_seq,
                    timestamp: Date.now(),
                };
                if (command.command === 'pause') {
                    socket._paused = true;
                    socket._schedule(() => socket._emitText(Object.assign({
                        type: 'state', command: 'pause', ok: true, state: 'paused', error_message: '',
                    }, cursorPayload)));
                    socket._schedule(() => {
                        runtime.protocolSeq += 1;
                        runtime.pausedRecords.push(buildRecord('protocol', runtime.protocolSeq, {
                            error_message: '暂停期间收到的待补发交互',
                            result: 'request_mismatch',
                        }));
                    }, 30);
                    return;
                }
                if (command.command === 'resume') {
                    socket._paused = false;
                    const pending = runtime.pausedRecords.splice(0);
                    socket._schedule(() => socket._ready('resume', pending.length), 0);
                    pending.forEach((record, index) => socket._schedule(() => socket._emitRecord(record, 'catch_up'), 10 + index * 10));
                    socket._schedule(() => socket._emitText(Object.assign({
                        type: 'state', command: 'resume', ok: true, state: 'active', error_message: '',
                    }, cursorPayload)), 20 + pending.length * 10);
                    return;
                }
                if (command.command === 'query_state') {
                    socket._schedule(() => socket._emitText(Object.assign({
                        type: 'state', command: 'query_state', ok: true,
                        state: socket._paused ? 'paused' : 'active', error_message: '',
                    }, cursorPayload)));
                }
            },
            close(code = 1000, reason = 'client disconnect') {
                if (socket._closed) return;
                socket._closed = true;
                socket.readyState = 3;
                socket.closeCalls.push({ code, reason });
                timers.forEach(timer => global.clearTimeout(timer));
                timers.clear();
                if (socket.onclose) socket.onclose({ code, reason });
            },
            fail(reason = 'mock network error') {
                socket.close(1006, reason);
            },
        };

        socket._schedule(() => {
            socket.readyState = 1;
            if (socket.onopen) socket.onopen();
            socket._ready('open', 0);
            socket._schedule(() => {
                socket._sendInitialRecords();
                socket._scheduleLiveRecord();
            }, 20);
            if (failThisConnection) socket._schedule(() => socket.fail('mock network error'), 80);
        }, 0);
        return socket;
    }

    function destroyAll() {
        const clients = KitProxy.protocolInteractionLive.clients || new Set();
        clients.forEach(client => client.destroy());
        clients.clear();
    }

    function setMockScenario(projectId, protocolId, patch = {}) {
        const key = `${numberId(projectId)}:${numberId(protocolId)}`;
        const runtime = mockRuntime.get(key) || {
            protocolSeq: 0,
            projectSeq: 0,
            pausedRecords: [],
            liveIntervalMs: 2500,
            resetNextResume: false,
            gapNextReady: false,
            failNextConnection: false,
        };
        Object.assign(runtime, patch || {});
        mockRuntime.set(key, runtime);
        return Object.assign({}, runtime, { pausedRecords: runtime.pausedRecords.slice() });
    }

    KitProxy.protocolInteractionLive = KitProxy.protocolInteractionLive || {};
    KitProxy.protocolInteractionLive.STATES = STATES;
    KitProxy.protocolInteractionLive.DEFAULT_OPTIONS = DEFAULT_OPTIONS;
    KitProxy.protocolInteractionLive.buildWebSocketUrl = buildWebSocketUrl;
    KitProxy.protocolInteractionLive.buildRecordKey = buildRecordKey;
    KitProxy.protocolInteractionLive.attachmentKey = attachmentKey;
    KitProxy.protocolInteractionLive.parseAttachmentFrame = parseAttachmentFrame;
    KitProxy.protocolInteractionLive.createMockTransport = createMockTransport;
    KitProxy.protocolInteractionLive.setMockScenario = setMockScenario;
    KitProxy.protocolInteractionLive.createClient = function createTrackedClient(options) {
        const client = createClient(options);
        const clients = KitProxy.protocolInteractionLive.clients || new Set();
        clients.add(client);
        KitProxy.protocolInteractionLive.clients = clients;
        client.on('destroyed', () => clients.delete(client));
        return client;
    };
    KitProxy.protocolInteractionLive.destroyAll = destroyAll;

    if (global.addEventListener) {
        global.addEventListener('pagehide', destroyAll);
    }
})(typeof window !== 'undefined' ? window : globalThis);

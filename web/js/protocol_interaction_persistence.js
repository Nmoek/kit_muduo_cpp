(function initProtocolInteractionPersistence(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});

    const DB_NAME = 'kit_protocol_interaction_workspace';
    const DB_VERSION = 1;
    const SCHEMA_VERSION = 1;
    const RESTORE_MARKER_KEY = 'kit_protocol_interaction_restore_marker';
    const STORE_NAMES = Object.freeze({
        snapshots: 'snapshots',
        records: 'records',
        attachments: 'attachments',
    });
    const INDEX_NAMES = Object.freeze({
        snapshotUserBackend: 'userBackend',
        snapshotExpiresAt: 'expiresAt',
        recordSnapshot: 'snapshotKey',
        recordScopeCursor: 'scopeCursor',
        attachmentSnapshot: 'snapshotKey',
        attachmentRecord: 'recordKey',
    });

    const RESULT_STATUS = Object.freeze({
        STORED: 'stored',
        HIT: 'hit',
        MISS: 'miss',
        DELETED: 'deleted',
        DISABLED: 'disabled',
        ERROR: 'error',
    });

    function numberId(value) {
        const result = Number(value);
        return Number.isInteger(result) && result > 0 ? result : 0;
    }

    function text(value) {
        return String(value == null ? '' : value);
    }

    function normalizeBackendIdentity(apiBaseUrl, location) {
        const base = text(apiBaseUrl || (location && location.origin) || '').trim();
        if (!base) return '';
        try {
            const url = new global.URL(base, location && location.origin ? location.origin : undefined);
            url.hash = '';
            url.search = '';
            url.pathname = url.pathname.replace(/\/+$/, '') || '/';
            return url.toString().replace(/\/$/, '');
        } catch (error) {
            return base.replace(/\/+$/, '');
        }
    }

    function stableSnapshotKey(identity = {}) {
        const values = [
            Number(identity.schemaVersion || SCHEMA_VERSION),
            text(identity.backendIdentity),
            numberId(identity.userId),
            text(identity.tabSessionId),
            numberId(identity.projectId),
            numberId(identity.protocolId),
        ];
        if (!values[1] || !values[2] || !values[3] || !values[4] || !values[5]) return '';
        return `kit-interaction:${JSON.stringify(values)}`;
    }

    function userBackendKey(backendIdentity, userId) {
        return [text(backendIdentity), numberId(userId)];
    }

    function createTabSessionId(storage, random = Math.random, now = Date.now) {
        const target = storage || global.sessionStorage;
        const key = 'kit_protocol_interaction_tab_session_id';
        try {
            const current = target && target.getItem(key);
            if (current) return current;
            const id = `${now().toString(36)}-${Math.floor(random() * 0x100000000).toString(36)}`;
            if (target) target.setItem(key, id);
            return id;
        } catch (error) {
            return '';
        }
    }

    function reliableUserId(user, explicitUserId) {
        const candidate = explicitUserId != null
            ? explicitUserId
            : user && (user.id != null ? user.id : user.user_id);
        return numberId(candidate);
    }

    /**
     * 创建实时工作区的隔离身份。未拿到可靠用户 ID 时返回 disabled，禁止生成匿名共享 key。
     * @param {{apiBaseUrl?: string, location?: Location, user?: any, userId?: number|string,
     *          tabSessionId?: string, sessionStorage?: Storage, projectId?: number|string,
     *          protocolId?: number|string}=} options
     * @returns {{enabled: boolean, reason?: string, schemaVersion: number, backendIdentity: string,
     *            userId: number, tabSessionId: string, projectId: number, protocolId: number,
     *            snapshotKey: string}}
     */
    function createWorkspaceIdentity(options = {}) {
        const backendIdentity = normalizeBackendIdentity(
            options.apiBaseUrl,
            options.location || global.location,
        );
        const userId = reliableUserId(options.user, options.userId);
        const tabSessionId = text(options.tabSessionId || createTabSessionId(
            options.sessionStorage || global.sessionStorage,
        ));
        const identity = {
            schemaVersion: SCHEMA_VERSION,
            backendIdentity,
            userId,
            tabSessionId,
            projectId: numberId(options.projectId),
            protocolId: numberId(options.protocolId),
        };
        const snapshotKey = stableSnapshotKey(identity);
        if (!backendIdentity || !userId || !tabSessionId || !identity.projectId || !identity.protocolId) {
            return Object.assign(identity, { enabled: false, reason: 'identity_unavailable', snapshotKey: '' });
        }
        return Object.assign(identity, { enabled: true, snapshotKey });
    }

    function normalizeRestoreMarker(marker) {
        if (!marker || typeof marker !== 'object') return null;
        return {
            schemaVersion: Number(marker.schemaVersion || SCHEMA_VERSION),
            snapshotKey: text(marker.snapshotKey),
            backendIdentity: text(marker.backendIdentity),
            userId: numberId(marker.userId),
            tabSessionId: text(marker.tabSessionId),
            projectId: numberId(marker.projectId),
            protocolId: numberId(marker.protocolId),
            drawerOpen: marker.drawerOpen === true,
            updatedAt: Number(marker.updatedAt) || 0,
            expiresAt: Number(marker.expiresAt) || 0,
        };
    }

    function isRestoreMarkerValid(marker, identity, now = Date.now()) {
        const value = normalizeRestoreMarker(marker);
        if (!value || value.schemaVersion !== SCHEMA_VERSION || !value.drawerOpen
            || value.expiresAt <= Number(now)) return false;
        return value.snapshotKey === identity.snapshotKey
            && value.backendIdentity === identity.backendIdentity
            && value.userId === identity.userId
            && value.tabSessionId === identity.tabSessionId
            && value.projectId === identity.projectId
            && value.protocolId === identity.protocolId;
    }

    function readValidRestoreMarker(identity, storage = global.sessionStorage, now = Date.now()) {
        const marker = readRestoreMarker(storage);
        const timestamp = typeof now === 'function' ? now() : now;
        return isRestoreMarkerValid(marker, identity, timestamp) ? marker : null;
    }

    function readRestoreMarker(storage = global.sessionStorage) {
        try {
            const raw = storage && storage.getItem(RESTORE_MARKER_KEY);
            return raw ? normalizeRestoreMarker(JSON.parse(raw)) : null;
        } catch (error) {
            return null;
        }
    }

    function writeRestoreMarker(identity, uiState = {}, storage = global.sessionStorage, now = Date.now) {
        if (!identity || identity.enabled !== true || !storage) return false;
        const timestamp = Number(now()) || Date.now();
        const marker = normalizeRestoreMarker({
            schemaVersion: SCHEMA_VERSION,
            snapshotKey: identity.snapshotKey,
            backendIdentity: identity.backendIdentity,
            userId: identity.userId,
            tabSessionId: identity.tabSessionId,
            projectId: identity.projectId,
            protocolId: identity.protocolId,
            drawerOpen: uiState.drawerOpen === true,
            updatedAt: timestamp,
            expiresAt: timestamp + 24 * 60 * 60 * 1000,
        });
        try {
            storage.setItem(RESTORE_MARKER_KEY, JSON.stringify(marker));
            return true;
        } catch (error) {
            return false;
        }
    }

    function clearRestoreMarker(storage = global.sessionStorage) {
        try {
            if (storage) storage.removeItem(RESTORE_MARKER_KEY);
            return true;
        } catch (error) {
            return false;
        }
    }

    function cloneValue(value, seen = new Map()) {
        if (value == null || typeof value !== 'object') return value;
        if (seen.has(value)) return seen.get(value);
        if (value instanceof Date) return new Date(value.getTime());
        if (typeof global.Blob !== 'undefined' && value instanceof global.Blob) return value.slice(0, value.size, value.type);
        if (value instanceof ArrayBuffer) return value.slice(0);
        if (ArrayBuffer.isView(value)) {
            return new value.constructor(value);
        }
        if (Array.isArray(value)) {
            const result = [];
            seen.set(value, result);
            value.forEach(item => result.push(cloneValue(item, seen)));
            return result;
        }
        if (value instanceof Set) {
            const result = new Set();
            seen.set(value, result);
            value.forEach(item => result.add(cloneValue(item, seen)));
            return result;
        }
        if (value instanceof Map) {
            const result = new Map();
            seen.set(value, result);
            value.forEach((item, key) => result.set(cloneValue(key, seen), cloneValue(item, seen)));
            return result;
        }
        const result = {};
        seen.set(value, result);
        Object.keys(value).forEach(key => {
            result[key] = cloneValue(value[key], seen);
        });
        return result;
    }

    function ok(status, extra = {}) {
        return Object.assign({ ok: status !== RESULT_STATUS.ERROR, status }, extra);
    }

    function disabledResult() {
        return ok(RESULT_STATUS.DISABLED, { persisted: false });
    }

    function failedResult(error) {
        return ok(RESULT_STATUS.ERROR, { persisted: false, error });
    }

    function normalizeSnapshot(snapshot) {
        if (!snapshot || typeof snapshot !== 'object') return null;
        const result = Object.assign({}, snapshot);
        result.schemaVersion = Number(result.schemaVersion || SCHEMA_VERSION);
        result.snapshotKey = text(result.snapshotKey);
        result.backendIdentity = text(result.backendIdentity);
        result.userId = numberId(result.userId);
        result.tabSessionId = text(result.tabSessionId);
        result.projectId = numberId(result.projectId);
        result.protocolId = numberId(result.protocolId);
        result.visibleKeys = Array.isArray(result.visibleKeys) ? result.visibleKeys.slice() : [];
        result.protocolPendingKeys = Array.isArray(result.protocolPendingKeys) ? result.protocolPendingKeys.slice() : [];
        result.noticePendingKeys = Array.isArray(result.noticePendingKeys) ? result.noticePendingKeys.slice() : [];
        result.reflowKeys = Array.isArray(result.reflowKeys) ? result.reflowKeys.slice() : [];
        result.readRecordKeys = Array.isArray(result.readRecordKeys) ? result.readRecordKeys.slice() : [];
        result.uiState = result.uiState && typeof result.uiState === 'object'
            ? Object.assign({}, result.uiState)
            : {};
        result.connectionIntent = result.connectionIntent && typeof result.connectionIntent === 'object'
            ? Object.assign({}, result.connectionIntent)
            : {};
        return result;
    }

    function validateSnapshot(snapshot) {
        const value = normalizeSnapshot(snapshot);
        if (!value || value.schemaVersion !== SCHEMA_VERSION || !value.snapshotKey
            || !value.backendIdentity || !value.userId || !value.tabSessionId
            || !value.projectId || !value.protocolId) {
            return { valid: false, snapshot: value, reason: 'invalid_snapshot_identity' };
        }
        const expectedKey = stableSnapshotKey(value);
        if (expectedKey !== value.snapshotKey) {
            return { valid: false, snapshot: value, reason: 'snapshot_key_mismatch' };
        }
        return { valid: true, snapshot: value };
    }

    function scopeMatches(record, scope) {
        return record && (scope === 'project' ? record.scope === 'project' : record.scope === 'protocol');
    }

    function createMemoryPersistenceAdapter(options = {}) {
        const snapshots = new Map();
        const records = new Map();
        const attachments = new Map();
        const failure = options.failure;
        const disabled = options.disabled === true;

        function maybeFail(operation) {
            if (disabled) return disabledResult();
            if (!failure) return null;
            try {
                const error = typeof failure === 'function' ? failure(operation) : failure;
                if (!error) return null;
                return failedResult(error instanceof Error ? error : new Error(text(error)));
            } catch (error) {
                return failedResult(error);
            }
        }

        function recordsFor(snapshotKey) {
            return Array.from(records.values())
                .filter(record => record.snapshotKey === snapshotKey)
                .map(record => cloneValue(record));
        }

        function attachmentsFor(snapshotKey) {
            return Array.from(attachments.values())
                .filter(attachment => attachment.snapshotKey === snapshotKey)
                .map(attachment => cloneValue(attachment));
        }

        function deleteSnapshotData(snapshotKey) {
            snapshots.delete(snapshotKey);
            Array.from(records.values()).forEach(record => {
                if (record.snapshotKey === snapshotKey) records.delete(record.recordStorageKey);
            });
            Array.from(attachments.values()).forEach(attachment => {
                if (attachment.snapshotKey === snapshotKey) attachments.delete(attachment.attachmentStorageKey);
            });
        }

        async function loadWorkspace(snapshotKey) {
            const failed = maybeFail('loadWorkspace');
            if (failed) return failed;
            const snapshot = snapshots.get(text(snapshotKey));
            if (!snapshot) return ok(RESULT_STATUS.MISS, { workspace: null });
            return ok(RESULT_STATUS.HIT, {
                workspace: {
                    snapshot: cloneValue(snapshot),
                    records: recordsFor(text(snapshotKey)),
                    attachments: attachmentsFor(text(snapshotKey)),
                },
            });
        }

        async function saveSnapshot(snapshot) {
            const failed = maybeFail('saveSnapshot');
            if (failed) return failed;
            const value = normalizeSnapshot(snapshot);
            snapshots.set(value.snapshotKey, cloneValue(value));
            return ok(RESULT_STATUS.STORED, { persisted: true });
        }

        async function putRecordBundle(input = {}) {
            const failed = maybeFail('putRecordBundle');
            if (failed) return failed;
            const snapshot = input.snapshot ? normalizeSnapshot(input.snapshot) : null;
            const record = input.record && cloneValue(input.record);
            if (snapshot) snapshots.set(snapshot.snapshotKey, cloneValue(snapshot));
            if (record && record.recordStorageKey) records.set(record.recordStorageKey, record);
            (Array.isArray(input.attachments) ? input.attachments : []).forEach(attachment => {
                if (attachment && attachment.attachmentStorageKey) {
                    attachments.set(attachment.attachmentStorageKey, cloneValue(attachment));
                }
            });
            return ok(RESULT_STATUS.STORED, { persisted: true });
        }

        async function putAttachmentBundle(input = {}) {
            const failed = maybeFail('putAttachmentBundle');
            if (failed) return failed;
            const snapshotKey = text(input.snapshotKey);
            const recordStorageKey = text(input.recordStorageKey || `${snapshotKey}:${text(input.recordKey)}`);
            const attachment = input.attachment && cloneValue(input.attachment);
            const currentRecord = records.get(recordStorageKey);
            if (!currentRecord || !attachment || !attachment.attachmentStorageKey) {
                return ok(RESULT_STATUS.MISS, { persisted: false });
            }
            const currentAttachment = attachments.get(attachment.attachmentStorageKey);
            if (currentAttachment && (Number(currentAttachment.size || currentAttachment.capturedSize)
                !== Number(attachment.size || attachment.capturedSize)
                || (currentAttachment.sha1 && attachment.sha1 && currentAttachment.sha1 !== attachment.sha1))) {
                return ok(RESULT_STATUS.ERROR, { persisted: false, conflict: true });
            }
            const storedKeys = new Set(Array.isArray(currentRecord.storedAttachmentKeys)
                ? currentRecord.storedAttachmentKeys : []);
            storedKeys.add(text(attachment.attachmentKey));
            const expectedKeys = new Set(Array.isArray(currentRecord.expectedAttachmentKeys)
                ? currentRecord.expectedAttachmentKeys : []);
            const complete = Array.from(expectedKeys).every(key => storedKeys.has(key));
            const nextRecord = cloneValue(Object.assign({}, currentRecord, {
                storedAttachmentKeys: Array.from(storedKeys),
                completionState: complete ? 'complete' : 'incomplete',
                updatedAt: Date.now(),
            }));
            records.set(recordStorageKey, nextRecord);
            if (!currentAttachment) attachments.set(attachment.attachmentStorageKey, attachment);
            return ok(currentAttachment ? RESULT_STATUS.HIT : RESULT_STATUS.STORED, {
                persisted: true,
                duplicate: Boolean(currentAttachment),
                completionState: nextRecord.completionState,
                storedAttachmentKeys: nextRecord.storedAttachmentKeys,
            });
        }

        async function clearScope(input = {}) {
            const failed = maybeFail('clearScope');
            if (failed) return failed;
            const snapshotKey = text(input.snapshotKey);
            const scope = input.scope === 'project' ? 'project' : 'protocol';
            Array.from(records.values()).forEach(record => {
                if (record.snapshotKey === snapshotKey && scopeMatches(record, scope)) {
                    records.delete(record.recordStorageKey);
                    Array.from(attachments.values()).forEach(attachment => {
                        if (attachment.snapshotKey === snapshotKey && attachment.recordKey === record.recordKey) {
                            attachments.delete(attachment.attachmentStorageKey);
                        }
                    });
                }
            });
            if (input.snapshot) snapshots.set(snapshotKey, cloneValue(normalizeSnapshot(input.snapshot)));
            return ok(RESULT_STATUS.DELETED, { persisted: true });
        }

        async function deleteSnapshot(snapshotKey) {
            const failed = maybeFail('deleteSnapshot');
            if (failed) return failed;
            deleteSnapshotData(text(snapshotKey));
            return ok(RESULT_STATUS.DELETED, { persisted: true });
        }

        async function deleteUser(input = {}) {
            const failed = maybeFail('deleteUser');
            if (failed) return failed;
            const backendIdentity = text(input.backendIdentity);
            const userId = numberId(input.userId);
            Array.from(snapshots.values()).forEach(snapshot => {
                if (snapshot.backendIdentity === backendIdentity && snapshot.userId === userId) {
                    deleteSnapshotData(snapshot.snapshotKey);
                }
            });
            return ok(RESULT_STATUS.DELETED, { persisted: true });
        }

        async function purgeExpired(now = Date.now()) {
            const failed = maybeFail('purgeExpired');
            if (failed) return failed;
            const cutoff = Number(now) || Date.now();
            Array.from(snapshots.values()).forEach(snapshot => {
                if (Number(snapshot.expiresAt) > 0 && Number(snapshot.expiresAt) <= cutoff) {
                    deleteSnapshotData(snapshot.snapshotKey);
                }
            });
            return ok(RESULT_STATUS.DELETED, { persisted: true });
        }

        return {
            kind: 'memory',
            available: !disabled,
            loadWorkspace,
            saveSnapshot,
            putRecordBundle,
            putAttachmentBundle,
            clearScope,
            deleteScope: clearScope,
            deleteSnapshot,
            deleteUser,
            purgeExpired,
            close: async () => ok(RESULT_STATUS.DELETED, { persisted: false }),
            _stores: { snapshots, records, attachments },
        };
    }

    function requestResult(request) {
        return new Promise((resolve, reject) => {
            request.onsuccess = () => resolve(request.result);
            request.onerror = () => reject(request.error || new Error('IndexedDB 请求失败'));
        });
    }

    function transactionResult(transaction) {
        return new Promise((resolve, reject) => {
            transaction.oncomplete = () => resolve();
            transaction.onerror = () => reject(transaction.error || new Error('IndexedDB 事务失败'));
            transaction.onabort = () => reject(transaction.error || new Error('IndexedDB 事务已中止'));
        });
    }

    function requestError(request, transaction) {
        request.onerror = () => {
            try {
                transaction.abort();
            } catch (error) {
                // The transaction may already have failed; its completion promise reports the cause.
            }
        };
    }

    function createIndexedDbPersistenceAdapter(options = {}) {
        const indexedDb = options.indexedDB || global.indexedDB;
        if (!indexedDb) return createMemoryPersistenceAdapter({ disabled: true });
        const dbName = options.dbName || DB_NAME;
        const dbVersion = options.dbVersion || DB_VERSION;
        let dbPromise = null;

        function open() {
            if (dbPromise) return dbPromise;
            dbPromise = new Promise((resolve, reject) => {
                let request;
                try {
                    request = indexedDb.open(dbName, dbVersion);
                } catch (error) {
                    reject(error);
                    return;
                }
                request.onupgradeneeded = event => {
                    const database = event.target.result;
                    const snapshotStore = database.objectStoreNames.contains(STORE_NAMES.snapshots)
                        ? event.target.transaction.objectStore(STORE_NAMES.snapshots)
                        : database.createObjectStore(STORE_NAMES.snapshots, { keyPath: 'snapshotKey' });
                    if (!snapshotStore.indexNames.contains(INDEX_NAMES.snapshotUserBackend)) {
                        snapshotStore.createIndex(INDEX_NAMES.snapshotUserBackend, ['backendIdentity', 'userId'], { unique: false });
                    }
                    if (!snapshotStore.indexNames.contains(INDEX_NAMES.snapshotExpiresAt)) {
                        snapshotStore.createIndex(INDEX_NAMES.snapshotExpiresAt, 'expiresAt', { unique: false });
                    }

                    const recordStore = database.objectStoreNames.contains(STORE_NAMES.records)
                        ? event.target.transaction.objectStore(STORE_NAMES.records)
                        : database.createObjectStore(STORE_NAMES.records, { keyPath: 'recordStorageKey' });
                    if (!recordStore.indexNames.contains(INDEX_NAMES.recordSnapshot)) {
                        recordStore.createIndex(INDEX_NAMES.recordSnapshot, 'snapshotKey', { unique: false });
                    }
                    if (!recordStore.indexNames.contains(INDEX_NAMES.recordScopeCursor)) {
                        recordStore.createIndex(INDEX_NAMES.recordScopeCursor, ['snapshotKey', 'scope', 'cacheInstanceId', 'seq'], { unique: false });
                    }

                    const attachmentStore = database.objectStoreNames.contains(STORE_NAMES.attachments)
                        ? event.target.transaction.objectStore(STORE_NAMES.attachments)
                        : database.createObjectStore(STORE_NAMES.attachments, { keyPath: 'attachmentStorageKey' });
                    if (!attachmentStore.indexNames.contains(INDEX_NAMES.attachmentSnapshot)) {
                        attachmentStore.createIndex(INDEX_NAMES.attachmentSnapshot, 'snapshotKey', { unique: false });
                    }
                    if (!attachmentStore.indexNames.contains(INDEX_NAMES.attachmentRecord)) {
                        attachmentStore.createIndex(INDEX_NAMES.attachmentRecord, ['snapshotKey', 'recordKey'], { unique: false });
                    }
                };
                request.onsuccess = () => {
                    const database = request.result;
                    database.onversionchange = () => database.close();
                    resolve(database);
                };
                request.onerror = () => reject(request.error || new Error('IndexedDB 打开失败'));
                request.onblocked = () => reject(new Error('IndexedDB 升级被其他页面阻塞'));
            });
            return dbPromise;
        }

        async function withStores(mode, callback) {
            const database = await open();
            const transaction = database.transaction(Object.values(STORE_NAMES), mode);
            const completed = transactionResult(transaction);
            const stores = {
                snapshots: transaction.objectStore(STORE_NAMES.snapshots),
                records: transaction.objectStore(STORE_NAMES.records),
                attachments: transaction.objectStore(STORE_NAMES.attachments),
            };
            let value;
            try {
                value = callback(stores, transaction);
            } catch (error) {
                try {
                    transaction.abort();
                } catch (abortError) {
                    // The completion promise retains the original transaction failure.
                }
                await completed.catch(() => {});
                throw error;
            }
            await completed;
            return value;
        }

        async function loadWorkspace(snapshotKey) {
            try {
                const value = await withStores('readonly', stores => {
                    const snapshotRequest = stores.snapshots.get(text(snapshotKey));
                    const recordsRequest = stores.records.index(INDEX_NAMES.recordSnapshot).getAll(text(snapshotKey));
                    const attachmentsRequest = stores.attachments.index(INDEX_NAMES.attachmentSnapshot).getAll(text(snapshotKey));
                    return Promise.all([
                        requestResult(snapshotRequest),
                        requestResult(recordsRequest),
                        requestResult(attachmentsRequest),
                    ]).then(([snapshot, records, attachments]) => snapshot
                        ? { snapshot, records, attachments }
                        : null);
                });
                return value ? ok(RESULT_STATUS.HIT, { workspace: value }) : ok(RESULT_STATUS.MISS, { workspace: null });
            } catch (error) {
                return failedResult(error);
            }
        }

        async function saveSnapshot(snapshot) {
            try {
                await withStores('readwrite', stores => {
                    stores.snapshots.put(normalizeSnapshot(snapshot));
                    return null;
                });
                return ok(RESULT_STATUS.STORED, { persisted: true });
            } catch (error) {
                return failedResult(error);
            }
        }

        async function putRecordBundle(input = {}) {
            try {
                await withStores('readwrite', stores => {
                    if (input.snapshot) stores.snapshots.put(normalizeSnapshot(input.snapshot));
                    if (input.record) stores.records.put(input.record);
                    (Array.isArray(input.attachments) ? input.attachments : []).forEach(attachment => stores.attachments.put(attachment));
                    return null;
                });
                return ok(RESULT_STATUS.STORED, { persisted: true });
            } catch (error) {
                return failedResult(error);
            }
        }

        async function putAttachmentBundle(input = {}) {
            try {
                const result = { status: RESULT_STATUS.MISS, persisted: false };
                await withStores('readwrite', (stores, transaction) => {
                    const snapshotKey = text(input.snapshotKey);
                    const recordStorageKey = text(input.recordStorageKey || `${snapshotKey}:${text(input.recordKey)}`);
                    const attachment = input.attachment;
                    const recordRequest = stores.records.get(recordStorageKey);
                    const attachmentRequest = attachment
                        ? stores.attachments.get(attachment.attachmentStorageKey)
                        : null;
                    let currentRecord = null;
                    let currentAttachment = null;
                    let recordReady = false;
                    let attachmentReady = !attachmentRequest;
                    const apply = () => {
                        if (!recordReady || !attachmentReady) return;
                        if (!currentRecord || !attachment || !attachment.attachmentStorageKey) return;
                        const currentSize = Number(currentAttachment && (currentAttachment.size || currentAttachment.capturedSize));
                        const nextSize = Number(attachment.size || attachment.capturedSize);
                        if (currentAttachment && (currentSize !== nextSize
                            || (currentAttachment.sha1 && attachment.sha1 && currentAttachment.sha1 !== attachment.sha1))) {
                            result.status = RESULT_STATUS.ERROR;
                            result.conflict = true;
                            return;
                        }
                        const storedKeys = new Set(Array.isArray(currentRecord.storedAttachmentKeys)
                            ? currentRecord.storedAttachmentKeys : []);
                        storedKeys.add(text(attachment.attachmentKey));
                        const expectedKeys = new Set(Array.isArray(currentRecord.expectedAttachmentKeys)
                            ? currentRecord.expectedAttachmentKeys : []);
                        const complete = Array.from(expectedKeys).every(key => storedKeys.has(key));
                        stores.records.put(Object.assign({}, currentRecord, {
                            storedAttachmentKeys: Array.from(storedKeys),
                            completionState: complete ? 'complete' : 'incomplete',
                            updatedAt: Date.now(),
                        }));
                        if (!currentAttachment) stores.attachments.put(attachment);
                        result.status = currentAttachment ? RESULT_STATUS.HIT : RESULT_STATUS.STORED;
                        result.persisted = true;
                        result.duplicate = Boolean(currentAttachment);
                        result.completionState = complete ? 'complete' : 'incomplete';
                        result.storedAttachmentKeys = Array.from(storedKeys);
                    };
                    recordRequest.onsuccess = () => {
                        currentRecord = recordRequest.result || null;
                        recordReady = true;
                        apply();
                    };
                    if (attachmentRequest) {
                        attachmentRequest.onsuccess = () => {
                            currentAttachment = attachmentRequest.result || null;
                            attachmentReady = true;
                            apply();
                        };
                    }
                    requestError(recordRequest, transaction);
                    if (attachmentRequest) requestError(attachmentRequest, transaction);
                    return result;
                });
                return result;
            } catch (error) {
                return failedResult(error);
            }
        }

        async function clearScope(input = {}) {
            try {
                await withStores('readwrite', (stores, transaction) => {
                    const snapshotKey = text(input.snapshotKey);
                    const scope = input.scope === 'project' ? 'project' : 'protocol';
                    const recordsRequest = stores.records.index(INDEX_NAMES.recordSnapshot).getAll(snapshotKey);
                    const attachmentsRequest = stores.attachments.index(INDEX_NAMES.attachmentSnapshot).getAll(snapshotKey);
                    let records = null;
                    let attachments = null;
                    const apply = () => {
                        if (!records || !attachments) return;
                        const deletedRecords = records.filter(record => scopeMatches(record, scope));
                        deletedRecords.forEach(record => stores.records.delete(record.recordStorageKey));
                        const deletedRecordKeys = new Set(deletedRecords.map(record => record.recordKey));
                        attachments.forEach(attachment => {
                            if (deletedRecordKeys.has(attachment.recordKey)) {
                                stores.attachments.delete(attachment.attachmentStorageKey);
                            }
                        });
                        if (input.snapshot) stores.snapshots.put(normalizeSnapshot(input.snapshot));
                    };
                    recordsRequest.onsuccess = () => {
                        records = recordsRequest.result || [];
                        apply();
                    };
                    attachmentsRequest.onsuccess = () => {
                        attachments = attachmentsRequest.result || [];
                        apply();
                    };
                    requestError(recordsRequest, transaction);
                    requestError(attachmentsRequest, transaction);
                    return null;
                });
                return ok(RESULT_STATUS.DELETED, { persisted: true });
            } catch (error) {
                return failedResult(error);
            }
        }

        async function deleteSnapshot(snapshotKey) {
            try {
                await withStores('readwrite', (stores, transaction) => {
                    const key = text(snapshotKey);
                    stores.snapshots.delete(key);
                    const recordsRequest = stores.records.index(INDEX_NAMES.recordSnapshot).getAll(key);
                    const attachmentsRequest = stores.attachments.index(INDEX_NAMES.attachmentSnapshot).getAll(key);
                    recordsRequest.onsuccess = () => (recordsRequest.result || [])
                        .forEach(record => stores.records.delete(record.recordStorageKey));
                    attachmentsRequest.onsuccess = () => (attachmentsRequest.result || [])
                        .forEach(attachment => stores.attachments.delete(attachment.attachmentStorageKey));
                    requestError(recordsRequest, transaction);
                    requestError(attachmentsRequest, transaction);
                    return null;
                });
                return ok(RESULT_STATUS.DELETED, { persisted: true });
            } catch (error) {
                return failedResult(error);
            }
        }

        async function deleteUser(input = {}) {
            try {
                await withStores('readwrite', (stores, transaction) => {
                    const key = userBackendKey(input.backendIdentity, input.userId);
                    const snapshotsRequest = stores.snapshots.index(INDEX_NAMES.snapshotUserBackend).getAll(key);
                    snapshotsRequest.onsuccess = () => {
                        const snapshots = snapshotsRequest.result || [];
                        const snapshotKeys = new Set(snapshots.map(snapshot => snapshot.snapshotKey));
                        snapshots.forEach(snapshot => stores.snapshots.delete(snapshot.snapshotKey));
                        const recordsRequest = stores.records.getAll();
                        const attachmentsRequest = stores.attachments.getAll();
                        recordsRequest.onsuccess = () => (recordsRequest.result || [])
                            .filter(record => snapshotKeys.has(record.snapshotKey))
                            .forEach(record => stores.records.delete(record.recordStorageKey));
                        attachmentsRequest.onsuccess = () => (attachmentsRequest.result || [])
                            .filter(attachment => snapshotKeys.has(attachment.snapshotKey))
                            .forEach(attachment => stores.attachments.delete(attachment.attachmentStorageKey));
                        requestError(recordsRequest, transaction);
                        requestError(attachmentsRequest, transaction);
                    };
                    requestError(snapshotsRequest, transaction);
                    return null;
                });
                return ok(RESULT_STATUS.DELETED, { persisted: true });
            } catch (error) {
                return failedResult(error);
            }
        }

        async function purgeExpired(now = Date.now()) {
            try {
                await withStores('readwrite', (stores, transaction) => {
                    const snapshotsRequest = stores.snapshots.getAll();
                    snapshotsRequest.onsuccess = () => {
                        const expiredKeys = new Set((snapshotsRequest.result || [])
                            .filter(snapshot => Number(snapshot.expiresAt) > 0 && Number(snapshot.expiresAt) <= Number(now))
                            .map(snapshot => snapshot.snapshotKey));
                        expiredKeys.forEach(key => stores.snapshots.delete(key));
                        const recordsRequest = stores.records.getAll();
                        const attachmentsRequest = stores.attachments.getAll();
                        recordsRequest.onsuccess = () => (recordsRequest.result || [])
                            .filter(record => expiredKeys.has(record.snapshotKey))
                            .forEach(record => stores.records.delete(record.recordStorageKey));
                        attachmentsRequest.onsuccess = () => (attachmentsRequest.result || [])
                            .filter(attachment => expiredKeys.has(attachment.snapshotKey))
                            .forEach(attachment => stores.attachments.delete(attachment.attachmentStorageKey));
                        requestError(recordsRequest, transaction);
                        requestError(attachmentsRequest, transaction);
                    };
                    requestError(snapshotsRequest, transaction);
                    return null;
                });
                return ok(RESULT_STATUS.DELETED, { persisted: true });
            } catch (error) {
                return failedResult(error);
            }
        }

        return {
            kind: 'indexeddb',
            available: true,
            loadWorkspace,
            saveSnapshot,
            putRecordBundle,
            putAttachmentBundle,
            clearScope,
            deleteScope: clearScope,
            deleteSnapshot,
            deleteUser,
            purgeExpired,
            close: async () => {
                if (dbPromise) {
                    const database = await dbPromise.catch(() => null);
                    if (database) database.close();
                    dbPromise = null;
                }
                return ok(RESULT_STATUS.DELETED, { persisted: false });
            },
        };
    }

    function createDisabledPersistenceAdapter() {
        return createMemoryPersistenceAdapter({ disabled: true });
    }

    function createPersistenceAdapter(options = {}) {
        if (options.adapter) return options.adapter;
        if (options.forceMemory) return createMemoryPersistenceAdapter(options);
        if (options.disabled) return createDisabledPersistenceAdapter();
        return createIndexedDbPersistenceAdapter(options);
    }

    /**
     * 清理当前用户在指定后端下的全部实时工作区。适合显式退出登录使用，
     * 调用方不需要持有某个协议项的 adapter 实例。
     * @param {{adapter?: any, backendIdentity?: string, apiBaseUrl?: string,
     *          location?: Location, userId?: number|string}=} input
     * @returns {Promise<any>}
     */
    async function clearUserWorkspace(input = {}) {
        const adapter = input.adapter || createPersistenceAdapter(input);
        try {
            if (!adapter || typeof adapter.deleteUser !== 'function') {
                return disabledResult();
            }
            const backendIdentity = text(input.backendIdentity) || normalizeBackendIdentity(
                input.apiBaseUrl,
                input.location || global.location,
            );
            return await adapter.deleteUser({
                backendIdentity,
                userId: input.userId,
            });
        } catch (error) {
            return failedResult(error);
        } finally {
            if (!input.adapter && adapter && typeof adapter.close === 'function') {
                try {
                    await adapter.close();
                } catch (error) {
                    // 清理失败不应阻止显式退出登录继续完成。
                }
            }
        }
    }

    const persistence = {
        DB_NAME,
        DB_VERSION,
        SCHEMA_VERSION,
        RESTORE_MARKER_KEY,
        STORE_NAMES,
        INDEX_NAMES,
        RESULT_STATUS,
        normalizeBackendIdentity,
        stableSnapshotKey,
        userBackendKey,
        createTabSessionId,
        createWorkspaceIdentity,
        normalizeRestoreMarker,
        isRestoreMarkerValid,
        readRestoreMarker,
        readValidRestoreMarker,
        writeRestoreMarker,
        clearRestoreMarker,
        normalizeSnapshot,
        validateSnapshot,
        createMemoryPersistenceAdapter,
        createIndexedDbPersistenceAdapter,
        createDisabledPersistenceAdapter,
        createPersistenceAdapter,
        clearUserWorkspace,
    };

    KitProxy.protocolInteractionPersistence = persistence;
})(typeof window !== 'undefined' ? window : globalThis);

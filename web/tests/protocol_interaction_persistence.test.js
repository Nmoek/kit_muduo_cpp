import { describe, expect, it } from 'vitest';
import { createDomContext, runScript } from './helpers/browser_context.js';

function createPersistenceContext() {
    const context = createDomContext('<!doctype html><html><body></body></html>', 'https://example.test/html/protocol_items.html');
    runScript(context, 'js/namespace.js');
    runScript(context, 'js/protocol_interaction_persistence.js');
    return context;
}

function makeIdentity(overrides = {}) {
    return Object.assign({
        schemaVersion: 1,
        backendIdentity: 'https://example.test/api',
        userId: 7,
        tabSessionId: 'tab-a',
        projectId: 3,
        protocolId: 11,
    }, overrides);
}

function makeSnapshot(persistence, overrides = {}) {
    const identity = makeIdentity(overrides);
    return Object.assign({
        snapshotKey: persistence.stableSnapshotKey(identity),
        createdAt: 100,
        updatedAt: 100,
        expiresAt: 86400100,
        protocolDurableCursor: { cacheInstanceId: 4, seq: 8 },
        projectDurableCursor: { cacheInstanceId: 5, seq: 9 },
        visibleKeys: [],
        protocolPendingKeys: [],
        noticePendingKeys: [],
        reflowKeys: [],
        selectedRecordKey: null,
        readRecordKeys: [],
        uiState: { filter: 'all', sortDirection: 'desc', mergeSpeed: 'medium' },
        connectionIntent: { desiredConnected: false, mergePaused: false },
    }, identity, overrides);
}

function createFakeIndexedDB() {
    const databases = new Map();

    function nameList(values) {
        return {
            contains(name) {
                return values.has(name);
            },
        };
    }

    function keyFor(value, keyPath) {
        if (Array.isArray(keyPath)) return keyPath.map(path => value[path]);
        return value[keyPath];
    }

    function sameKey(left, right) {
        if (Array.isArray(left) || Array.isArray(right)) {
            return Array.isArray(left) && Array.isArray(right)
                && left.length === right.length
                && left.every((value, index) => sameKey(value, right[index]));
        }
        return left === right;
    }

    class FakeRequest {
        constructor(transaction, run) {
            this.result = undefined;
            this.error = null;
            this.onsuccess = null;
            this.onerror = null;
            if (transaction) transaction.enqueue(this, run);
        }
    }

    class FakeTransaction {
        constructor(database) {
            this.database = database;
            this.pending = 0;
            this.aborted = false;
            this.oncomplete = null;
            this.onerror = null;
            this.onabort = null;
        }

        objectStore(name) {
            return this.database.stores.get(name);
        }

        enqueue(request, run) {
            this.pending += 1;
            setTimeout(() => {
                if (this.aborted) return;
                try {
                    request.result = run();
                    if (typeof request.onsuccess === 'function') request.onsuccess({ target: request });
                } catch (error) {
                    request.error = error;
                    if (typeof request.onerror === 'function') request.onerror({ target: request });
                } finally {
                    this.pending -= 1;
                    this.finishIfIdle();
                }
            }, 0);
        }

        abort() {
            if (this.aborted) return;
            this.aborted = true;
            if (typeof this.onabort === 'function') this.onabort({ target: this });
        }

        finishIfIdle() {
            if (!this.aborted && this.pending === 0 && typeof this.oncomplete === 'function') {
                this.oncomplete({ target: this });
            }
        }
    }

    class FakeIndex {
        constructor(store, keyPath) {
            this.store = store;
            this.keyPath = keyPath;
        }

        getAll(query) {
            return new FakeRequest(this.store.transaction, () => Array.from(this.store.values.values())
                .filter(value => query === undefined || sameKey(keyFor(value, this.keyPath), query)));
        }
    }

    class FakeStore {
        constructor(database, name, options) {
            this.database = database;
            this.name = name;
            this.keyPath = options.keyPath;
            this.values = new Map();
            this.indexes = new Map();
            this.transaction = null;
            this.indexNames = nameList(this.indexes);
        }

        bind(transaction) {
            this.transaction = transaction;
            return this;
        }

        createIndex(name, keyPath) {
            this.indexes.set(name, keyPath);
            return new FakeIndex(this, keyPath);
        }

        index(name) {
            return new FakeIndex(this, this.indexes.get(name));
        }

        put(value) {
            return new FakeRequest(this.transaction, () => {
                this.values.set(keyFor(value, this.keyPath), value);
                return keyFor(value, this.keyPath);
            });
        }

        get(key) {
            return new FakeRequest(this.transaction, () => this.values.get(key));
        }

        getAll(query) {
            return new FakeRequest(this.transaction, () => Array.from(this.values.values())
                .filter(value => query === undefined || sameKey(keyFor(value, this.keyPath), query)));
        }

        delete(key) {
            return new FakeRequest(this.transaction, () => {
                this.values.delete(key);
                return undefined;
            });
        }
    }

    class FakeDatabase {
        constructor(name) {
            this.name = name;
            this.stores = new Map();
            this.objectStoreNames = nameList(this.stores);
            this.onversionchange = null;
        }

        createObjectStore(name, options) {
            const store = new FakeStore(this, name, options);
            this.stores.set(name, store);
            return store;
        }

        transaction(names) {
            const transaction = new FakeTransaction(this);
            names.forEach(name => this.stores.get(name).bind(transaction));
            return transaction;
        }

        close() {}
    }

    return {
        open(name, version) {
            const request = new FakeRequest();
            setTimeout(() => {
                let database = databases.get(name);
                const upgrade = !database || version > 1;
                if (!database) {
                    database = new FakeDatabase(name);
                    databases.set(name, database);
                }
                request.result = database;
                if (upgrade && typeof request.onupgradeneeded === 'function') {
                    const transaction = new FakeTransaction(database);
                    request.transaction = transaction;
                    request.onupgradeneeded({ target: { result: database, transaction } });
                }
                if (typeof request.onsuccess === 'function') request.onsuccess({ target: request });
            }, 0);
            return request;
        },
    };
}

describe('protocol interaction persistence contract', () => {
    /**
     * 测试思路：schema 常量必须固定，三个 store 和关键索引不能在实现间漂移。
     * 示例：数据库名为 kit_protocol_interaction_workspace，records 使用 recordStorageKey，附件使用 snapshotKey+recordKey 查询。
     */
    it('定义稳定的 IndexedDB schema 常量', () => {
        const persistence = createPersistenceContext().KitProxy.protocolInteractionPersistence;

        expect(persistence.DB_NAME).toBe('kit_protocol_interaction_workspace');
        expect(persistence.DB_VERSION).toBe(1);
        expect(persistence.SCHEMA_VERSION).toBe(1);
        expect(persistence.STORE_NAMES).toEqual({
            snapshots: 'snapshots',
            records: 'records',
            attachments: 'attachments',
        });
        expect(persistence.INDEX_NAMES.recordScopeCursor).toBe('scopeCursor');
        expect(persistence.INDEX_NAMES.attachmentRecord).toBe('recordKey');
    });

    /**
     * 测试思路：隔离键使用结构化序列化，任意用户、标签、项目或协议变化都必须得到不同 key，字段顺序变化不能造成碰撞。
     * 示例：tab-a/protocol=11 与 tab-b/protocol=11 不相等，但同一组 identity 重复计算结果完全相等。
     */
    it('生成稳定且按用户/标签/项目/协议隔离的快照 key', () => {
        const persistence = createPersistenceContext().KitProxy.protocolInteractionPersistence;
        const first = persistence.stableSnapshotKey(makeIdentity());

        expect(first).toBe(persistence.stableSnapshotKey(makeIdentity()));
        expect(first).not.toBe(persistence.stableSnapshotKey(makeIdentity({ tabSessionId: 'tab-b' })));
        expect(first).not.toBe(persistence.stableSnapshotKey(makeIdentity({ userId: 8 })));
        expect(first).not.toBe(persistence.stableSnapshotKey(makeIdentity({ protocolId: 12 })));
        expect(persistence.stableSnapshotKey(makeIdentity({ userId: 0 }))).toBe('');
    });

    /**
     * 测试思路：恢复 marker 只允许同一标签、用户、backend、项目和协议命中，且 drawerOpen=true、未过期；关闭 marker 不应触发自动恢复。
     * 示例：tab-a 在 t=100 写入 marker，t=200 命中；换 tab 或 t 超过 24h 返回 null。
     */
    it('sessionStorage marker 按身份和 24 小时有效期控制恢复', () => {
        const persistence = createPersistenceContext().KitProxy.protocolInteractionPersistence;
        const identity = Object.assign(makeIdentity(), { enabled: true, snapshotKey: persistence.stableSnapshotKey(makeIdentity()) });
        const storage = {
            value: '',
            getItem() { return this.value || null; },
            setItem(key, value) { this.value = value; },
            removeItem() { this.value = ''; },
        };

        expect(persistence.writeRestoreMarker(identity, { drawerOpen: true }, storage, () => 100)).toBe(true);
        expect(persistence.readValidRestoreMarker(identity, storage, 200)).toMatchObject({
            snapshotKey: identity.snapshotKey,
            drawerOpen: true,
        });
        expect(persistence.readValidRestoreMarker(Object.assign({}, identity, { tabSessionId: 'tab-b' }), storage, 200)).toBeNull();
        expect(persistence.readValidRestoreMarker(identity, storage, 100 + 24 * 60 * 60 * 1000 + 1)).toBeNull();
        persistence.writeRestoreMarker(identity, { drawerOpen: false }, storage, () => 300);
        expect(persistence.readValidRestoreMarker(identity, storage, 301)).toBeNull();
    });

    /**
     * 测试思路：工作区身份必须归一化 backend URL，并从可靠 user.id/user_id 和 sessionStorage 生成隔离 key。
     * 示例：带 query/hash/尾斜杠的同一 API 地址得到相同 backend identity；同一 storage 刷新复用 tab id，另一个 storage 生成不同 id。
     */
    it('创建带用户验证和标签页隔离的工作区身份', () => {
        const persistence = createPersistenceContext().KitProxy.protocolInteractionPersistence;
        const storage = {
            values: new Map(),
            getItem(key) { return this.values.get(key) || null; },
            setItem(key, value) { this.values.set(key, value); },
        };
        const first = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test/api/?debug=1#drawer',
            user: { user_id: 7 },
            sessionStorage: storage,
            projectId: 3,
            protocolId: 11,
        });
        const refreshed = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test/api',
            user: { id: 7 },
            sessionStorage: storage,
            projectId: 3,
            protocolId: 11,
        });
        const otherTab = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test/api',
            user: { id: 7 },
            sessionStorage: { values: new Map(), getItem() { return null; }, setItem() {} },
            projectId: 3,
            protocolId: 11,
        });

        expect(first.enabled).toBe(true);
        expect(first.backendIdentity).toBe('https://example.test/api');
        expect(refreshed.tabSessionId).toBe(first.tabSessionId);
        expect(refreshed.snapshotKey).toBe(first.snapshotKey);
        expect(otherTab.snapshotKey).not.toBe(first.snapshotKey);
    });

    /**
     * 测试思路：未认证、缺少 user.id/user_id 或缺少项目定位信息时不得生成 anonymous 快照 key。
     * 示例：user=null 和 user={} 都返回 enabled=false，且 snapshotKey 为空。
     */
    it('缺少可靠用户身份时禁用工作区持久化', () => {
        const persistence = createPersistenceContext().KitProxy.protocolInteractionPersistence;
        const base = {
            apiBaseUrl: 'https://example.test/api',
            tabSessionId: 'tab-a',
            projectId: 3,
            protocolId: 11,
        };

        expect(persistence.createWorkspaceIdentity(Object.assign({}, base, { user: null }))).toMatchObject({
            enabled: false,
            snapshotKey: '',
        });
        expect(persistence.createWorkspaceIdentity(Object.assign({}, base, { user: {} }))).toMatchObject({
            enabled: false,
            snapshotKey: '',
        });
        expect(persistence.createWorkspaceIdentity(Object.assign({}, base, { user: { id: 7 }, protocolId: 0 }))).toMatchObject({
            enabled: false,
            snapshotKey: '',
        });
    });

    /**
     * 测试思路：契约必须覆盖快照命中/未命中、原子记录附件写入、scope 清理和用户清理。
     * 示例：先保存 protocol 与 project 记录，删除 protocol scope 后 project 仍可恢复，再按 user/backend 删除剩余快照。
     */
    it('内存适配器实现共享持久化契约', async () => {
        const persistence = createPersistenceContext().KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const snapshot = makeSnapshot(persistence);
        const protocolRecord = {
            recordStorageKey: `${snapshot.snapshotKey}:protocol:4:8`,
            snapshotKey: snapshot.snapshotKey,
            recordKey: 'protocol:4:8',
            scope: 'protocol',
            cacheInstanceId: 4,
            seq: 8,
            record: { seq: 8 },
        };
        const projectRecord = Object.assign({}, protocolRecord, {
            recordStorageKey: `${snapshot.snapshotKey}:project:5:9`,
            recordKey: 'project:5:9',
            scope: 'project',
            cacheInstanceId: 5,
            seq: 9,
        });
        const attachment = {
            attachmentStorageKey: `${snapshot.snapshotKey}:protocol:4:8:a1`,
            snapshotKey: snapshot.snapshotKey,
            recordKey: protocolRecord.recordKey,
            attachmentKey: 'a1',
            payload: new Uint8Array([1, 2, 3]),
        };

        expect((await adapter.loadWorkspace(snapshot.snapshotKey)).status).toBe('miss');
        await adapter.putRecordBundle({ snapshot, record: protocolRecord, attachments: [attachment] });
        await adapter.putRecordBundle({ snapshot, record: projectRecord });

        const loaded = await adapter.loadWorkspace(snapshot.snapshotKey);
        expect(loaded.status).toBe('hit');
        expect(loaded.workspace.records.map(item => item.recordKey)).toEqual(['protocol:4:8', 'project:5:9']);
        expect(loaded.workspace.attachments).toHaveLength(1);

        await adapter.clearScope({ snapshotKey: snapshot.snapshotKey, scope: 'protocol', snapshot });
        const afterScopeClear = await adapter.loadWorkspace(snapshot.snapshotKey);
        expect(afterScopeClear.workspace.records.map(item => item.scope)).toEqual(['project']);
        expect(afterScopeClear.workspace.attachments).toHaveLength(0);

        await adapter.deleteUser({ backendIdentity: snapshot.backendIdentity, userId: snapshot.userId });
        expect((await adapter.loadWorkspace(snapshot.snapshotKey)).status).toBe('miss');
    });

    /**
     * 测试思路：没有 IndexedDB 时只能返回 disabled，不应伪装成持久化成功。
     * 示例：显式 disabled adapter 的写入返回 disabled，调用方仍可继续使用内存实时状态。
     */
    it('区分持久化不可用与命中/写入成功', async () => {
        const persistence = createPersistenceContext().KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createDisabledPersistenceAdapter();
        const snapshot = makeSnapshot(persistence);

        expect((await adapter.saveSnapshot(snapshot)).status).toBe('disabled');
        expect((await adapter.loadWorkspace(snapshot.snapshotKey)).status).toBe('disabled');
    });

    /**
     * 测试思路：用 IndexedDB 最小兼容实现驱动真实 adapter，覆盖首次建库、事务写入、复合索引查询和级联删除。
     * 示例：保存 protocol/project 两条记录后按 protocol scope 清理，再按 backend+user 删除剩余快照和附件。
     */
    it('IndexedDB adapter 完成打开、读写、索引查询和级联删除', async () => {
        const persistence = createPersistenceContext().KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createIndexedDbPersistenceAdapter({ indexedDB: createFakeIndexedDB() });
        const snapshot = makeSnapshot(persistence);
        const protocolRecord = {
            recordStorageKey: `${snapshot.snapshotKey}:protocol:4:8`,
            snapshotKey: snapshot.snapshotKey,
            recordKey: 'protocol:4:8',
            scope: 'protocol',
            cacheInstanceId: 4,
            seq: 8,
        };
        const projectRecord = Object.assign({}, protocolRecord, {
            recordStorageKey: `${snapshot.snapshotKey}:project:5:9`,
            recordKey: 'project:5:9',
            scope: 'project',
            cacheInstanceId: 5,
            seq: 9,
        });
        const attachment = {
            attachmentStorageKey: `${snapshot.snapshotKey}:protocol:4:8:a1`,
            snapshotKey: snapshot.snapshotKey,
            recordKey: protocolRecord.recordKey,
            attachmentKey: 'a1',
            payload: new Uint8Array([1, 2, 3]),
        };

        await expect(adapter.loadWorkspace(snapshot.snapshotKey)).resolves.toMatchObject({ status: 'miss' });
        await expect(adapter.putRecordBundle({ snapshot, record: protocolRecord, attachments: [attachment] }))
            .resolves.toMatchObject({ status: 'stored' });
        await adapter.putRecordBundle({ snapshot, record: projectRecord });

        const loaded = await adapter.loadWorkspace(snapshot.snapshotKey);
        expect(loaded.status).toBe('hit');
        expect(loaded.workspace.records).toHaveLength(2);
        expect(loaded.workspace.attachments).toHaveLength(1);

        await adapter.clearScope({ snapshotKey: snapshot.snapshotKey, scope: 'protocol', snapshot });
        const afterScopeClear = await adapter.loadWorkspace(snapshot.snapshotKey);
        expect(afterScopeClear.workspace.records.map(record => record.scope)).toEqual(['project']);
        expect(afterScopeClear.workspace.attachments).toHaveLength(0);

        await adapter.deleteUser({ backendIdentity: snapshot.backendIdentity, userId: snapshot.userId });
        expect((await adapter.loadWorkspace(snapshot.snapshotKey)).status).toBe('miss');
    });

    /**
     * 测试思路：内存 adapter 不能让测试调用方持有内部对象引用，否则 hydration 测试会被外部 mutation 污染。
     * 示例：写入 Uint8Array=[1,2] 后修改原数组和第一次读取结果，第二次读取仍得到 [1,2]。
     */
    it('内存 adapter 对写入和读取结果执行深拷贝', async () => {
        const persistence = createPersistenceContext().KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const snapshot = makeSnapshot(persistence);
        const bytes = new Uint8Array([1, 2]);
        const record = {
            recordStorageKey: `${snapshot.snapshotKey}:protocol:4:8`,
            snapshotKey: snapshot.snapshotKey,
            recordKey: 'protocol:4:8',
            scope: 'protocol',
            cacheInstanceId: 4,
            seq: 8,
        };
        await adapter.putRecordBundle({
            snapshot,
            record,
            attachments: [{
                attachmentStorageKey: `${snapshot.snapshotKey}:protocol:4:8:a1`,
                snapshotKey: snapshot.snapshotKey,
                recordKey: record.recordKey,
                attachmentKey: 'a1',
                payload: bytes,
            }],
        });
        bytes[0] = 9;
        const firstRead = await adapter.loadWorkspace(snapshot.snapshotKey);
        firstRead.workspace.attachments[0].payload[1] = 8;
        const secondRead = await adapter.loadWorkspace(snapshot.snapshotKey);

        expect(Array.from(secondRead.workspace.attachments[0].payload)).toEqual([1, 2]);
    });

    /**
     * 测试思路：注入的配额/存储异常必须只影响持久化结果，不改变契约形状；过期清理只删除 expiresAt 已到期快照。
     * 示例：failure('saveSnapshot') 返回 error；一个过期快照和一个未来快照清理后只保留未来快照。
     */
    it('内存 adapter 支持失败注入和精确过期清理', async () => {
        const persistence = createPersistenceContext().KitProxy.protocolInteractionPersistence;
        const failing = persistence.createMemoryPersistenceAdapter({
            failure: operation => operation === 'saveSnapshot' ? new Error('quota') : null,
        });
        const snapshot = makeSnapshot(persistence, { expiresAt: 100 });
        expect((await failing.saveSnapshot(snapshot)).status).toBe('error');

        const adapter = persistence.createMemoryPersistenceAdapter();
        const expired = makeSnapshot(persistence, { tabSessionId: 'expired', expiresAt: 100 });
        const valid = makeSnapshot(persistence, { tabSessionId: 'valid', expiresAt: 300 });
        await adapter.saveSnapshot(expired);
        await adapter.saveSnapshot(valid);
        await adapter.purgeExpired(200);

        expect((await adapter.loadWorkspace(expired.snapshotKey)).status).toBe('miss');
        expect((await adapter.loadWorkspace(valid.snapshotKey)).status).toBe('hit');
    });

    /**
     * 测试思路：附件与对应 record 状态必须由同一个 adapter 操作更新，重复相同附件幂等，冲突附件不能覆盖已验证 payload。
     * 示例：a1 到达后 incomplete->complete；再次写入相同 a1 返回 hit；改成同键不同 SHA1 返回 conflict，原 payload 仍为 [1,2,3]。
     */
    it('原子更新附件和记录完整性并拒绝冲突覆盖', async () => {
        const persistence = createPersistenceContext().KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const snapshot = makeSnapshot(persistence);
        const record = {
            recordStorageKey: `${snapshot.snapshotKey}:protocol:4:8`,
            snapshotKey: snapshot.snapshotKey,
            recordKey: 'protocol:4:8',
            scope: 'protocol',
            cacheInstanceId: 4,
            seq: 8,
            completionState: 'incomplete',
            expectedAttachmentKeys: ['protocol:4:8:a1'],
            storedAttachmentKeys: [],
        };
        const attachment = {
            attachmentStorageKey: `${snapshot.snapshotKey}:protocol:4:8:a1`,
            snapshotKey: snapshot.snapshotKey,
            recordKey: record.recordKey,
            attachmentKey: 'protocol:4:8:a1',
            sha1: 'sha1',
            size: 3,
            payload: new Uint8Array([1, 2, 3]),
        };
        await adapter.putRecordBundle({ snapshot, record });

        await expect(adapter.putAttachmentBundle({
            snapshotKey: snapshot.snapshotKey,
            recordKey: record.recordKey,
            recordStorageKey: record.recordStorageKey,
            attachment,
        })).resolves.toMatchObject({ status: 'stored', completionState: 'complete' });
        await expect(adapter.putAttachmentBundle({
            snapshotKey: snapshot.snapshotKey,
            recordKey: record.recordKey,
            recordStorageKey: record.recordStorageKey,
            attachment,
        })).resolves.toMatchObject({ status: 'hit', duplicate: true });
        await expect(adapter.putAttachmentBundle({
            snapshotKey: snapshot.snapshotKey,
            recordKey: record.recordKey,
            recordStorageKey: record.recordStorageKey,
            attachment: Object.assign({}, attachment, { sha1: 'other-sha1' }),
        })).resolves.toMatchObject({ status: 'error', conflict: true });

        const loaded = (await adapter.loadWorkspace(snapshot.snapshotKey)).workspace;
        expect(loaded.records[0]).toMatchObject({ completionState: 'complete', storedAttachmentKeys: ['protocol:4:8:a1'] });
        expect(Array.from(loaded.attachments[0].payload)).toEqual([1, 2, 3]);
        expect(loaded.attachments[0].objectUrl).toBeUndefined();
    });
});

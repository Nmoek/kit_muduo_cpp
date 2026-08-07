import { describe, expect, it, vi } from 'vitest';
import { createDomContext, runScript } from './helpers/browser_context.js';

function createLiveContext(search = '?apiMode=real') {
    const context = createDomContext('<!doctype html><html><body></body></html>', `https://example.test/html/protocol_items.html${search}`);
    runScript(context, 'js/namespace.js');
    runScript(context, 'js/config.js');
    runScript(context, 'js/protocol_interaction_persistence.js');
    runScript(context, 'js/protocol_interaction_live.js');
    return context;
}

function makeRecord(overrides = {}) {
    return Object.assign({
        seq: 1,
        scope: 'protocol',
        project_id: 1,
        protocol_id: 10,
        cache_instance_id: 7,
        protocol_type: 'http',
        time_ms: 1780000000000,
        peer_addr: '127.0.0.1:50000',
        result: 'matched',
        error_message: '',
        request: {
            meta: { method: 'GET', path: '/health' },
            head_text: 'GET /health HTTP/1.1',
            body: { kind: 'empty', expect_kind: 'empty', attachments: [] },
        },
        response: {
            meta: { status_code: 200 },
            head_text: 'HTTP/1.1 200 OK',
            body: { kind: 'json', expect_kind: 'json', text: '{"ok":true}', attachments: [] },
        },
    }, overrides);
}

function liveReady(overrides = {}) {
    return JSON.stringify(Object.assign({
        type: 'live_ready',
        trigger: 'open',
        project_id: 1,
        protocol_id: 10,
        session_id: 99,
        protocol_cache_info: {
            cache_instance_id: 7,
            last_seq: 3,
            live_start_seq: 4,
            catch_up_count: 0,
            catch_up_gap: false,
            cursor_reset: false,
        },
        project_cache_info: {
            cache_instance_id: 8,
            last_seq: 2,
            live_start_seq: 3,
            catch_up_count: 0,
            catch_up_gap: false,
            cursor_reset: false,
        },
        timestamp: 1780000000000,
    }, overrides));
}

describe('protocol interaction live data layer', () => {
    function createFakeSocket() {
        return {
            sent: [],
            closeCalls: [],
            send(payload) {
                this.sent.push(JSON.parse(payload));
            },
            close(code, reason) {
                this.closeCalls.push({ code, reason });
                if (this.onclose) this.onclose({ code, reason });
            },
        };
    }

    /**
     * 测试思路：只给协议项提供 project cursor，协议 cursor 缺失时不能输出半套恢复参数。
     * 示例：project cursor=8:2，生成 URL 只包含 after_project_cache_instance_id/after_project_seq。
     */
    it('构建 URL 时按 scope 成对输出双游标', () => {
        const live = createLiveContext();
        const buildUrl = live.KitProxy.protocolInteractionLive.buildWebSocketUrl;
        const url = new URL(buildUrl({
            protocolId: 10,
            location: live.location,
            protocolCursor: { cacheInstanceId: 0, seq: 4 },
            projectCursor: { cacheInstanceId: 8, seq: 2 },
        }));

        expect(url.protocol).toBe('wss:');
        expect(url.pathname).toBe('/ws/protocol-interactions/live');
        expect(url.searchParams.get('after_protocol_seq')).toBeNull();
        expect(url.searchParams.get('after_project_cache_instance_id')).toBe('8');
        expect(url.searchParams.get('after_project_seq')).toBe('2');
    });

    /**
     * 测试思路：首次 live_ready 只建立服务端当前边界，不创建历史交互记录。
     * 示例：last_seq=3 且 catch_up_count=0，客户端 active、游标为 7:3/8:2、记录数仍为 0。
     */
    it('首次 live_ready 初始化双游标但不伪造历史记录', () => {
        const live = createLiveContext();
        const client = live.KitProxy.protocolInteractionLive.createClient({ projectId: 1, protocolId: 10 });
        client.receiveText(liveReady());

        const state = client.getState();
        expect(state.connectionState).toBe('active');
        expect(state.visibleRecords).toHaveLength(0);
        expect(state.pendingRecords).toHaveLength(0);
        expect(state.protocolCursor).toEqual({ cacheInstanceId: 7, seq: 3 });
        expect(state.projectCursor).toEqual({ cacheInstanceId: 8, seq: 2 });
        client.destroy();
    });

    /**
     * 测试思路：durable cursor 只跨越同一 scope 的连续 complete 记录，incomplete 中间断点不能跳过；另一个 scope 可以独立推进。
     * 示例：protocol 100 后收到 101 complete、102 incomplete、103 complete，protocol durable=101；project 50 后收到 51 complete，project durable=51。
     */
    it('双 scope durable cursor 按连续完整记录独立推进', async () => {
        const live = createLiveContext();
        const persistence = live.KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test', userId: 7, tabSessionId: 'tab-a', projectId: 1, protocolId: 10,
        });
        const client = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1, protocolId: 10, persistenceAdapter: adapter, workspaceIdentity: identity,
        });
        client.receiveText(liveReady({
            protocol_cache_info: { cache_instance_id: 7, last_seq: 100, catch_up_count: 0, catch_up_gap: false, cursor_reset: false },
            project_cache_info: { cache_instance_id: 8, last_seq: 50, catch_up_count: 0, catch_up_gap: false, cursor_reset: false },
        }));
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'catch_up', record: makeRecord({ seq: 101 }) }));
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'catch_up', record: makeRecord({
            seq: 102,
            request: Object.assign({}, makeRecord().request, {
                body: { kind: 'image', expect_kind: 'image', attachments: [{ attachment_id: 'gap', captured_size: 1, kind: 'image' }] },
            }),
        }) }));
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'catch_up', record: makeRecord({ seq: 103 }) }));
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'catch_up', record: makeRecord({
            scope: 'project', protocol_id: 0, cache_instance_id: 8, seq: 51,
        }) }));
        await client.flushPersistence();

        expect(client.getState().protocolDurableCursor).toEqual({ cacheInstanceId: 7, seq: 101 });
        expect(client.getState().projectDurableCursor).toEqual({ cacheInstanceId: 8, seq: 51 });
        const url = new URL(client.buildUrl());
        expect(url.searchParams.get('after_protocol_seq')).toBe('101');
        expect(url.searchParams.get('after_project_seq')).toBe('51');
        client.destroy();
    });

    /**
     * 测试思路：incomplete 附件补齐后应一次跨过后续已 complete 记录，且 durable cursor 只能在持久化成功后变化。
     * 示例：protocol 101/102/103 中 102 缺 attachment，补发 attachment 后 durable 从 101 变为 103。
     */
    it('缺失附件补齐后连续推进 durable cursor', async () => {
        const live = createLiveContext();
        const persistence = live.KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test', userId: 7, tabSessionId: 'tab-a', projectId: 1, protocolId: 10,
        });
        const client = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1, protocolId: 10, persistenceAdapter: adapter, workspaceIdentity: identity,
        });
        client.receiveText(liveReady({
            protocol_cache_info: { cache_instance_id: 7, last_seq: 100, catch_up_count: 0, catch_up_gap: false, cursor_reset: false },
            project_cache_info: { cache_instance_id: 8, last_seq: 0, catch_up_count: 0, catch_up_gap: false, cursor_reset: false },
        }));
        [101, 102, 103].forEach(seq => {
            const record = seq === 102
                ? makeRecord({ seq, request: Object.assign({}, makeRecord().request, {
                    body: { kind: 'image', expect_kind: 'image', attachments: [{ attachment_id: 'gap', captured_size: 1, kind: 'image' }] },
                }) })
                : makeRecord({ seq });
            client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'catch_up', record }));
        });
        await client.flushPersistence();
        expect(client.getState().protocolDurableCursor).toEqual({ cacheInstanceId: 7, seq: 101 });

        const headerBytes = new TextEncoder().encode(JSON.stringify({
            type: 'attachment', scope: 'protocol', project_id: 1, protocol_id: 10,
            cache_instance_id: 7, record_seq: 102, attachment_id: 'gap', captured_size: 1, sha1: 'gap-sha1',
        }));
        const frame = new Uint8Array(4 + headerBytes.length + 1);
        new DataView(frame.buffer).setUint32(0, headerBytes.length, false);
        frame.set(headerBytes, 4);
        frame[4 + headerBytes.length] = 7;
        expect(client.receiveBinary(frame)).toBe(true);
        await client.flushPersistence();
        expect(client.getState().protocolDurableCursor).toEqual({ cacheInstanceId: 7, seq: 103 });
        client.destroy();
    });

    /**
     * 测试思路：UI 快照写入采用约 150ms 防抖，并且必须保留三组队列的实际顺序、选中键、已读键和 UI 元数据。
     * 示例：visible=[protocol:7:1]、protocol pending=[protocol:7:2]、notice=[project:8:1]、reflow=[protocol:7:3]，flush 后快照逐项相同。
     */
    it('防抖持久化队列顺序和 UI 元数据', async () => {
        vi.useFakeTimers();
        try {
            const live = createLiveContext();
            const persistence = live.KitProxy.protocolInteractionPersistence;
            const adapter = persistence.createMemoryPersistenceAdapter();
            const identity = persistence.createWorkspaceIdentity({
                apiBaseUrl: 'https://example.test', userId: 7, tabSessionId: 'tab-a', projectId: 1, protocolId: 10,
            });
            const client = live.KitProxy.protocolInteractionLive.createClient({
                projectId: 1, protocolId: 10, persistenceAdapter: adapter, workspaceIdentity: identity,
                mergeIntervalMs: 1000,
            });
            client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: makeRecord({ seq: 1, time_ms: 1 }) }));
            client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: makeRecord({ seq: 2, time_ms: 2 }) }));
            client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: makeRecord({
                scope: 'project', protocol_id: 0, cache_instance_id: 8, seq: 1, time_ms: 3,
            }) }));
            vi.advanceTimersByTime(1000);
            client.updatePersistenceUiState({
                filter: 'notice', detailTab: 'attachments', fullscreen: true, mobileDetail: true,
                drawerOpen: true, followLive: false, readRecordKeys: ['protocol:7:1'],
            });
            vi.advanceTimersByTime(149);
            expect((await adapter.loadWorkspace(identity.snapshotKey)).status).toBe('miss');
            vi.advanceTimersByTime(1);
            await client.flushPersistence();
            const workspace = (await adapter.loadWorkspace(identity.snapshotKey)).workspace;

            expect(workspace.snapshot).toMatchObject({
                protocolPendingKeys: ['protocol:7:2'],
                noticePendingKeys: ['project:8:1'],
                reflowKeys: [],
                selectedRecordKey: null,
                readRecordKeys: ['protocol:7:1'],
                uiState: expect.objectContaining({
                    filter: 'notice', detailTab: 'attachments', fullscreen: true,
                    mobileDetail: true, drawerOpen: true, followLive: false,
                }),
            });
            expect(workspace.snapshot.visibleKeys).toEqual(['protocol:7:1']);
            client.destroy();
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：用同一个 adapter 模拟刷新前后两个 live client，第二个 client 必须按 snapshot 键恢复队列、UI、durable cursor 和附件 Object URL。
     * 示例：第一个 client 保存 visible/pending 与 image:a1，第二个 client hydrate 后记录顺序和 read/filter/tab 与刷新前一致。
     */
    it('hydration 恢复记录队列、UI 状态、游标和附件 URL', async () => {
        const live = createLiveContext();
        const persistence = live.KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test', userId: 7, tabSessionId: 'tab-refresh', projectId: 1, protocolId: 10,
        });
        live.URL.createObjectURL = vi.fn(() => 'blob:hydrated-image');
        live.URL.revokeObjectURL = vi.fn();
        const first = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1, protocolId: 10, persistenceAdapter: adapter, workspaceIdentity: identity,
            mergeIntervalMs: 1000,
        });
        const record = makeRecord({
            request: Object.assign({}, makeRecord().request, {
                body: { kind: 'image', expect_kind: 'image', attachments: [{ attachment_id: 'a1', kind: 'image', captured_size: 3 }] },
            }),
        });
        first.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record }));
        first.setMergeSpeed('slow');
        first.updatePersistenceUiState({
            filter: 'image', detailTab: 'attachments', fullscreen: true, mobileDetail: true,
            drawerOpen: true, readRecordKeys: ['protocol:7:1'], sortDirection: 'desc', mergeSpeed: 'slow',
        });
        const headerBytes = new TextEncoder().encode(JSON.stringify({
            type: 'attachment', scope: 'protocol', project_id: 1, protocol_id: 10,
            cache_instance_id: 7, record_seq: 1, attachment_id: 'a1', captured_size: 3, sha1: 'sha1',
        }));
        const payload = new Uint8Array([1, 2, 3]);
        const frame = new Uint8Array(4 + headerBytes.length + payload.length);
        new DataView(frame.buffer).setUint32(0, headerBytes.length, false);
        frame.set(headerBytes, 4);
        frame.set(payload, 4 + headerBytes.length);
        first.receiveBinary(frame);
        await first.flushPersistence();

        const second = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1, protocolId: 10, persistenceAdapter: adapter, workspaceIdentity: identity,
        });
        const result = await second.hydrateWorkspace();

        expect(result).toMatchObject({ status: 'hit', restored: true });
        expect(second.getState()).toMatchObject({
            selectedRecordKey: null,
            protocolDurableCursor: null,
            uiState: expect.objectContaining({
                filter: 'image', detailTab: 'attachments', fullscreen: true,
                mobileDetail: true, drawerOpen: true, readRecordKeys: ['protocol:7:1'],
                sortDirection: 'desc', mergeSpeed: 'slow',
            }),
        });
        expect(second.getState().pendingRecords.map(item => item._key)).toEqual(['protocol:7:1']);
        expect(second.getAttachment('protocol:7:1', 'a1')).toMatchObject({ objectUrl: 'blob:hydrated-image' });
        expect(live.URL.createObjectURL).toHaveBeenCalledTimes(2);
        first.destroy();
        second.destroy();
    });

    /**
     * 测试思路：active 连接意图恢复时只能创建新 WebSocket session，URL 使用持久化 durable cursor，不复用旧 session_id。
     * 示例：snapshot durable=7:101/project=8:51，hydrate 后 transport 收到 after_protocol_seq=101 和 after_project_seq=51。
     */
    it('hydration active 意图使用 durable cursor 自动新建连接', async () => {
        const live = createLiveContext();
        const persistence = live.KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test', userId: 7, tabSessionId: 'tab-active', projectId: 1, protocolId: 10,
        });
        const snapshot = {
            snapshotKey: identity.snapshotKey, schemaVersion: 1, backendIdentity: identity.backendIdentity,
            userId: 7, tabSessionId: identity.tabSessionId, projectId: 1, protocolId: 10,
            protocolLiveCursor: { cacheInstanceId: 7, seq: 120 }, projectLiveCursor: { cacheInstanceId: 8, seq: 70 },
            protocolDurableCursor: { cacheInstanceId: 7, seq: 101 }, projectDurableCursor: { cacheInstanceId: 8, seq: 51 },
            visibleKeys: [], protocolPendingKeys: [], noticePendingKeys: [], reflowKeys: [],
            selectedRecordKey: null, readRecordKeys: [], uiState: {},
            connectionIntent: { desiredConnected: true, mergePaused: false, connectionPaused: false },
        };
        await adapter.saveSnapshot(snapshot);
        const sockets = [];
        const client = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1, protocolId: 10, persistenceAdapter: adapter, workspaceIdentity: identity,
            transportFactory: url => {
                const socket = createFakeSocket();
                socket.url = url;
                sockets.push(socket);
                return socket;
            },
        });

        await client.hydrateWorkspace();
        expect(sockets).toHaveLength(1);
        const url = new URL(sockets[0].url);
        expect(url.searchParams.get('after_protocol_seq')).toBe('101');
        expect(url.searchParams.get('after_project_seq')).toBe('51');
        expect(client.getState().sessionId).toBe(0);
        client.destroy();
    });

    /**
     * 测试思路：paused 意图必须在新 live_ready session 上发送 pause，命令序号从 1 开始，不能重放旧 session/client_seq。
     * 示例：hydrate 创建 socket 后 sent=[]；收到 session_id=501 的 live_ready 后，sent[0] 为 session_id=501/client_seq=1/pause。
     */
    it('hydration paused 意图在新 session 上恢复暂停', async () => {
        const live = createLiveContext();
        const persistence = live.KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test', userId: 7, tabSessionId: 'tab-paused', projectId: 1, protocolId: 10,
        });
        await adapter.saveSnapshot({
            snapshotKey: identity.snapshotKey, schemaVersion: 1, backendIdentity: identity.backendIdentity,
            userId: 7, tabSessionId: identity.tabSessionId, projectId: 1, protocolId: 10,
            protocolLiveCursor: { cacheInstanceId: 7, seq: 10 }, projectLiveCursor: { cacheInstanceId: 8, seq: 2 },
            protocolDurableCursor: { cacheInstanceId: 7, seq: 10 }, projectDurableCursor: { cacheInstanceId: 8, seq: 2 },
            visibleKeys: [], protocolPendingKeys: [], noticePendingKeys: [], reflowKeys: [],
            selectedRecordKey: null, readRecordKeys: [], uiState: {},
            connectionIntent: { desiredConnected: true, mergePaused: false, connectionPaused: true },
        });
        const sockets = [];
        const client = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1, protocolId: 10, persistenceAdapter: adapter, workspaceIdentity: identity,
            transportFactory: url => {
                const socket = createFakeSocket();
                socket.url = url;
                sockets.push(socket);
                return socket;
            },
        });
        await client.hydrateWorkspace();
        expect(sockets[0].sent).toEqual([]);
        sockets[0].onopen?.();
        sockets[0].onmessage?.({ data: liveReady({ session_id: 501 }) });

        expect(sockets[0].sent).toHaveLength(1);
        expect(sockets[0].sent[0]).toMatchObject({ command: 'pause', session_id: 501, client_seq: 1 });
        client.destroy();
    });

    /**
     * 测试思路：用户主动断开意图只恢复现场，不得在 hydration 阶段自动创建 WebSocket。
     * 示例：connectionIntent.desiredConnected=false，hydrate 后 transportFactory 调用次数为 0。
     */
    it('hydration disconnected 意图不自动连接', async () => {
        const live = createLiveContext();
        const persistence = live.KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test', userId: 7, tabSessionId: 'tab-disconnected', projectId: 1, protocolId: 10,
        });
        await adapter.saveSnapshot({
            snapshotKey: identity.snapshotKey, schemaVersion: 1, backendIdentity: identity.backendIdentity,
            userId: 7, tabSessionId: identity.tabSessionId, projectId: 1, protocolId: 10,
            visibleKeys: [], protocolPendingKeys: [], noticePendingKeys: [], reflowKeys: [],
            selectedRecordKey: null, readRecordKeys: [], uiState: {},
            connectionIntent: { desiredConnected: false, mergePaused: false, connectionPaused: false },
        });
        const transportFactory = vi.fn(() => createFakeSocket());
        const client = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1, protocolId: 10, persistenceAdapter: adapter, workspaceIdentity: identity, transportFactory,
        });

        await client.hydrateWorkspace();
        expect(transportFactory).not.toHaveBeenCalled();
        client.destroy();
    });

    /**
     * 测试思路：快照队列引用损坏时不得部分恢复；只删除当前 snapshotKey，其他标签页的 workspace 必须仍可读取。
     * 示例：tab-b 快照 visibleKeys 引用 missing-key，hydrate 返回 error 并删除 tab-b，tab-a 快照保持 hit。
     */
    it('损坏 hydration 快照隔离清理且不影响其他标签页', async () => {
        const live = createLiveContext();
        const persistence = live.KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const valid = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test', userId: 7, tabSessionId: 'tab-a', projectId: 1, protocolId: 10,
        });
        const corrupt = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test', userId: 7, tabSessionId: 'tab-b', projectId: 1, protocolId: 10,
        });
        const snapshot = {
            snapshotKey: corrupt.snapshotKey, schemaVersion: 1, backendIdentity: corrupt.backendIdentity,
            userId: 7, tabSessionId: 'tab-b', projectId: 1, protocolId: 10,
            visibleKeys: ['missing-key'], protocolPendingKeys: [], noticePendingKeys: [], reflowKeys: [],
            readRecordKeys: [], uiState: {}, connectionIntent: {},
        };
        await adapter.saveSnapshot(snapshot);
        await adapter.saveSnapshot(Object.assign({}, snapshot, {
            snapshotKey: valid.snapshotKey, tabSessionId: 'tab-a', visibleKeys: [],
        }));
        const client = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1, protocolId: 10, persistenceAdapter: adapter, workspaceIdentity: corrupt,
        });

        await expect(client.hydrateWorkspace()).resolves.toMatchObject({ status: 'error', restored: false });
        expect((await adapter.loadWorkspace(corrupt.snapshotKey)).status).toBe('miss');
        expect((await adapter.loadWorkspace(valid.snapshotKey)).status).toBe('hit');
        client.destroy();
    });

    /**
     * 测试思路：IndexedDB 读取失败属于持久化降级，不等同于快照内容损坏，不能顺手删除当前 snapshot。
     * 示例：loadWorkspace 返回 error 后 hydrate 返回 error/degraded，但 deleteSnapshot 不得被调用。
     */
    it('hydration 读取错误只降级不删除 snapshot', async () => {
        const live = createLiveContext();
        const persistence = live.KitProxy.protocolInteractionPersistence;
        const baseAdapter = persistence.createMemoryPersistenceAdapter();
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test', userId: 7, tabSessionId: 'tab-read-error', projectId: 1, protocolId: 10,
        });
        const deleteSnapshot = vi.fn(baseAdapter.deleteSnapshot);
        const adapter = Object.assign({}, baseAdapter, {
            loadWorkspace: vi.fn(async () => ({
                status: 'error', ok: false, error: new Error('IndexedDB temporarily unavailable'),
            })),
            deleteSnapshot,
        });
        const client = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1, protocolId: 10, persistenceAdapter: adapter, workspaceIdentity: identity,
        });

        await expect(client.hydrateWorkspace()).resolves.toMatchObject({ status: 'error', restored: false });
        expect(deleteSnapshot).not.toHaveBeenCalled();
        expect(client.getPersistenceState().status).toBe('degraded');
        client.destroy();
    });

    /**
     * 测试思路：不兼容 schema 是当前 snapshot 的内容问题，只能删除当前 key，不能阻断 client 后续实时连接。
     * 示例：schemaVersion=99 的 tab-version 快照 hydration 返回 error 并删除，其他 workspace 不被触碰。
     */
    it('不兼容快照版本只删除当前 workspace', async () => {
        const live = createLiveContext();
        const persistence = live.KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test', userId: 7, tabSessionId: 'tab-version', projectId: 1, protocolId: 10,
        });
        await adapter.saveSnapshot({
            snapshotKey: identity.snapshotKey, schemaVersion: 99, backendIdentity: identity.backendIdentity,
            userId: 7, tabSessionId: identity.tabSessionId, projectId: 1, protocolId: 10,
            visibleKeys: [], protocolPendingKeys: [], noticePendingKeys: [], reflowKeys: [],
            readRecordKeys: [], uiState: {}, connectionIntent: {},
        });
        const client = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1, protocolId: 10, persistenceAdapter: adapter, workspaceIdentity: identity,
        });

        await expect(client.hydrateWorkspace()).resolves.toMatchObject({ status: 'error', restored: false });
        expect((await adapter.loadWorkspace(identity.snapshotKey)).status).toBe('miss');
        client.destroy();
    });

    /**
     * 测试思路：重复 complete JSON 必须保持原队列位置和记录数；相同 key 的内容冲突不能静默覆盖原记录。
     * 示例：protocol:7:1 先入队一次，再以 catch_up 重放一次，pending 仍为 1；path 改变时保留原 /health 并产生冲突告警。
     */
    it('重复 JSON 重放幂等且冲突版本不覆盖', () => {
        const live = createLiveContext();
        const client = live.KitProxy.protocolInteractionLive.createClient({ projectId: 1, protocolId: 10 });
        const record = makeRecord({ seq: 1 });
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record }));
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'catch_up', record }));
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'catch_up', record: makeRecord({
            request: Object.assign({}, record.request, { meta: { method: 'GET', path: '/conflict' } }),
        }) }));

        expect(client.getState().pendingRecords).toHaveLength(1);
        expect(client.getRecord('protocol:7:1').request.meta.path).toBe('/health');
        expect(client.getState().warnings.some(item => item.message.includes('冲突'))).toBe(true);
        client.destroy();
    });

    /**
     * 测试思路：incomplete JSON 被 catch-up 重放时不能创建第二条队列记录，而应校准预期附件并保留已收到附件。
     * 示例：首次声明 a1，重复消息声明 a1/a2，pending 仍为 1，pendingAttachmentRefs 增加 a2。
     */
    it('incomplete JSON 重放只校准附件引用不重复入队', () => {
        const live = createLiveContext();
        const client = live.KitProxy.protocolInteractionLive.createClient({ projectId: 1, protocolId: 10 });
        const first = makeRecord({
            request: Object.assign({}, makeRecord().request, {
                body: { kind: 'image', expect_kind: 'image', attachments: [{ attachment_id: 'a1', kind: 'image' }] },
            }),
        });
        const replay = makeRecord({
            request: Object.assign({}, makeRecord().request, {
                body: { kind: 'image', expect_kind: 'image', attachments: [
                    { attachment_id: 'a1', kind: 'image' }, { attachment_id: 'a2', kind: 'image' },
                ] },
            }),
        });
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: first }));
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'catch_up', record: replay }));

        const state = client.getState();
        expect(state.pendingRecords).toHaveLength(1);
        expect(state.pendingAttachmentRefs.has('protocol:7:1:a2')).toBe(true);
        client.destroy();
    });

    /**
     * 测试思路：
     *
     * 测什么：重复相同二进制附件必须幂等，同 key 不同大小/SHA1
     * 必须拒绝并保留第一个 payload。
     * 为什么这么测：重连或重复投递不应重复创建 Blob URL，冲突帧也不能
     * 静默覆盖已校验的附件。
     * 怎么测：先登记 JSON 引用，连续接收两次相同帧，再接收同 key 冲突帧，
     * 断言返回值、附件数量和原 bytes。
     *
     * 示例：JSON a1 -> payload [1,2,3] -> duplicate [1,2,3] -> reject [4]。
     */
    it('重复附件幂等且冲突附件不覆盖内存 payload', () => {
        const live = createLiveContext();
        const client = live.KitProxy.protocolInteractionLive.createClient({ projectId: 1, protocolId: 10 });
        const record = makeRecord({
            request: Object.assign({}, makeRecord().request, {
                body: { kind: 'image', expect_kind: 'image', attachments: [{ attachment_id: 'a1', kind: 'image' }] },
            }),
        });
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record }));
        const makeFrame = (bytes, sha1) => {
            const headerBytes = new TextEncoder().encode(JSON.stringify({
                type: 'attachment', scope: 'protocol', project_id: 1, protocol_id: 10,
                cache_instance_id: 7, record_seq: 1, attachment_id: 'a1', captured_size: bytes.length, sha1,
            }));
            const frame = new Uint8Array(4 + headerBytes.length + bytes.length);
            new DataView(frame.buffer).setUint32(0, headerBytes.length, false);
            frame.set(headerBytes, 4);
            frame.set(bytes, 4 + headerBytes.length);
            return frame;
        };
        const same = makeFrame(new Uint8Array([1, 2, 3]), 'sha1');
        expect(client.receiveBinary(same)).toBe(true);
        expect(client.receiveBinary(same)).toBe(true);
        expect(client.receiveBinary(makeFrame(new Uint8Array([4]), 'other'))).toBe(false);
        expect(client.getAttachment('protocol:7:1', 'a1').bytes).toEqual(new Uint8Array([1, 2, 3]));
        client.destroy();
    });

    /**
     * 测试思路：
     *
     * 测什么：两条 interaction JSON 都已登记后，即使 B 的 binary 先于 A
     * 到达，也必须按 scope/cache_instance_id/seq/attachment_id 关联到各自记录。
     * 为什么这么测：附件不能依赖“上一条 JSON”或全局当前记录，否则多记录
     * 处理时会把 payload 串到相邻记录。
     * 怎么测：登记 seq=1/a1 和 seq=2/b1，再以 B -> A 顺序传入不同字节，
     * 断言两个 record key 只能读取自己的 attachment ID 和 payload。
     *
     * 示例：JSON A -> JSON B -> binary B -> binary A -> A=[1,2], B=[3,4,5]。
     */
    it('多记录附件交错到达时按完整键精确关联', () => {
        const live = createLiveContext();
        const client = live.KitProxy.protocolInteractionLive.createClient({ projectId: 1, protocolId: 10 });
        const makeAttachmentRecord = (seq, attachmentId) => {
            const base = makeRecord({ seq });
            return makeRecord({
                seq,
                request: Object.assign({}, base.request, {
                    body: {
                        kind: 'binary', expect_kind: 'binary', size: 0, captured_size: 0,
                        text: '', attachments: [{
                            attachment_id: attachmentId, side: 'request', flag: 'request.body',
                            kind: 'binary', binary_available: true, truncated: false,
                        }],
                    },
                }),
            });
        };
        const makeFrame = (seq, attachmentId, payload) => {
            const headerBytes = new TextEncoder().encode(JSON.stringify({
                type: 'attachment', scope: 'protocol', project_id: 1, protocol_id: 10,
                cache_instance_id: 7, record_seq: seq, attachment_id: attachmentId,
                captured_size: payload.length,
            }));
            const frame = new Uint8Array(4 + headerBytes.length + payload.length);
            new DataView(frame.buffer).setUint32(0, headerBytes.length, false);
            frame.set(headerBytes, 4);
            frame.set(payload, 4 + headerBytes.length);
            return frame;
        };

        client.receiveText(JSON.stringify({
            type: 'interaction', delivery: 'live', record: makeAttachmentRecord(1, 'a1'),
        }));
        client.receiveText(JSON.stringify({
            type: 'interaction', delivery: 'live', record: makeAttachmentRecord(2, 'b1'),
        }));
        expect(client.receiveBinary(makeFrame(2, 'b1', new Uint8Array([3, 4, 5])))).toBe(true);
        expect(client.receiveBinary(makeFrame(1, 'a1', new Uint8Array([1, 2])))).toBe(true);

        expect(client.getAttachment('protocol:7:1', 'a1').bytes).toEqual(new Uint8Array([1, 2]));
        expect(client.getAttachment('protocol:7:2', 'b1').bytes).toEqual(new Uint8Array([3, 4, 5]));
        expect(client.getAttachment('protocol:7:1', 'b1')).toBeNull();
        expect(client.getAttachment('protocol:7:2', 'a1')).toBeNull();
        client.destroy();
    });

    /**
     * 测试思路：
     *
     * 测什么：binary 在所属 JSON interaction 之前到达时必须被当作
     * orphan 拒绝，后续 JSON 不能把已拒绝 payload 补绑或绑到其他记录。
     * 为什么这么测：当前 WebSocket 线协议由 TCP 保序，后端 MessageGroup 固定
     * JSON -> binary。明确拒绝非法顺序可以避免无边界 orphan 缓存和附件串联。
     * 怎么测：先接收 protocol:7:9:orphan 帧，断言返回 false 并产生
     * orphan_attachment 告警；再接收对应 JSON，断言附件仍不可用。
     *
     * 示例：binary orphan -> reject/warn -> JSON arrives -> incomplete, no payload。
     */
    it('拒绝 binary-first orphan 且后续 JSON 不会补绑', () => {
        const live = createLiveContext();
        const client = live.KitProxy.protocolInteractionLive.createClient({ projectId: 1, protocolId: 10 });
        const payload = new Uint8Array([9, 8, 7]);
        const headerBytes = new TextEncoder().encode(JSON.stringify({
            type: 'attachment', scope: 'protocol', project_id: 1, protocol_id: 10,
            cache_instance_id: 7, record_seq: 9, attachment_id: 'orphan',
            captured_size: payload.length,
        }));
        const frame = new Uint8Array(4 + headerBytes.length + payload.length);
        new DataView(frame.buffer).setUint32(0, headerBytes.length, false);
        frame.set(headerBytes, 4);
        frame.set(payload, 4 + headerBytes.length);

        expect(client.receiveBinary(frame)).toBe(false);
        expect(client.getState().warnings.at(-1).message).toContain('附件未找到对应的交互记录');

        const base = makeRecord({ seq: 9 });
        const record = makeRecord({
            seq: 9,
            request: Object.assign({}, base.request, {
                body: {
                    kind: 'binary', expect_kind: 'binary', size: 3, captured_size: 3,
                    text: '', attachments: [{
                        attachment_id: 'orphan', side: 'request', flag: 'request.body',
                        kind: 'binary', captured_size: 3, binary_available: true, truncated: false,
                    }],
                },
            }),
        });
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record }));
        expect(client.getAttachment('protocol:7:9', 'orphan')).toBeNull();
        expect(client.getState().pendingAttachmentRefs.has('protocol:7:9:orphan')).toBe(true);
        expect(client.getState().attachmentPayloads.size).toBe(0);
        client.destroy();
    });

    /**
     * 测试思路：两个 scope 使用相同 seq 时，唯一键必须仍然不同；同一键重复消息应被丢弃。
     * 示例：protocol:7:4 与 project:8:4 都保留，protocol:7:4 第二次到达不增加记录。
     */
    it('按 scope/cache_instance_id/seq 去重，不混淆两个 scope', () => {
        const live = createLiveContext();
        const client = live.KitProxy.protocolInteractionLive.createClient({ projectId: 1, protocolId: 10 });
        const protocolRecord = makeRecord({ seq: 4 });
        const projectRecord = makeRecord({
            scope: 'project',
            protocol_id: 0,
            cache_instance_id: 8,
            seq: 4,
            result: 'route_not_found',
        });

        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: protocolRecord }));
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: projectRecord }));
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: protocolRecord }));

        expect(client.getState().pendingRecords).toHaveLength(2);
        expect(client.getState().pendingRecords.map(record => record._key)).toEqual([
            'protocol:7:4',
            'project:8:4',
        ]);
        client.destroy();
    });

    /**
     * 测试思路：JSON 校验成功后立即异步写入 workspace；没有附件的记录必须直接 complete，不能阻塞内存入队。
     * 示例：protocol:7:4 无 attachments，flushPersistence 后 records[0].completionState=complete 且 pending 仍有该记录。
     */
    it('接收无附件 JSON 后持久化为 complete', async () => {
        const live = createLiveContext();
        const persistence = live.KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test',
            userId: 7,
            tabSessionId: 'tab-a',
            projectId: 1,
            protocolId: 10,
        });
        const client = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1,
            protocolId: 10,
            persistenceAdapter: adapter,
            workspaceIdentity: identity,
        });

        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: makeRecord({ seq: 4 }) }));
        expect(client.getState().pendingRecords.map(record => record._key)).toEqual(['protocol:7:4']);
        await client.flushPersistence();

        const workspace = (await adapter.loadWorkspace(identity.snapshotKey)).workspace;
        expect(workspace.records[0]).toMatchObject({
            recordKey: 'protocol:7:4',
            completionState: 'complete',
            expectedAttachmentKeys: [],
            storedAttachmentKeys: [],
        });
        client.destroy();
    });

    /**
     * 测试思路：声明了二进制附件的 JSON 必须先落为 incomplete，后续 R2 才能在同一 record entry 上补齐。
     * 示例：request.body 声明 image:a1 但暂未收到二进制帧，持久化状态为 incomplete，内存 pending 不受影响。
     */
    it('接收带附件 JSON 后持久化为 incomplete', async () => {
        const live = createLiveContext();
        const persistence = live.KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test', userId: 7, tabSessionId: 'tab-a', projectId: 1, protocolId: 10,
        });
        const client = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1, protocolId: 10, persistenceAdapter: adapter, workspaceIdentity: identity,
        });
        const record = makeRecord({
            request: Object.assign({}, makeRecord().request, {
                body: {
                    kind: 'image',
                    expect_kind: 'image',
                    attachments: [{ attachment_id: 'a1', kind: 'image', captured_size: 3, binary_available: true }],
                },
            }),
        });

        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record }));
        await client.flushPersistence();

        const workspace = (await adapter.loadWorkspace(identity.snapshotKey)).workspace;
        expect(workspace.records[0]).toMatchObject({
            completionState: 'incomplete',
            expectedAttachmentKeys: ['protocol:7:1:a1'],
            storedAttachmentKeys: [],
        });
        expect(client.getState().pendingRecords).toHaveLength(1);
        client.destroy();
    });

    /**
     * 测试思路：持久化 adapter 失败时只能记录一次降级告警，实时 client 仍应完成校验、入队和消费调度。
     * 示例：putRecordBundle 返回 error，pendingRecords 仍包含 protocol:7:4，persistence status 为 degraded。
     */
    it('持久化失败不阻断内存实时流程', async () => {
        const live = createLiveContext();
        const persistence = live.KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter({ failure: () => new Error('quota') });
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test', userId: 7, tabSessionId: 'tab-a', projectId: 1, protocolId: 10,
        });
        const client = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1, protocolId: 10, persistenceAdapter: adapter, workspaceIdentity: identity,
        });
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: makeRecord({ seq: 4 }) }));
        await client.flushPersistence();

        expect(client.getState().pendingRecords.map(record => record._key)).toEqual(['protocol:7:4']);
        expect(client.getPersistenceState().status).toBe('degraded');
        expect(client.getState().warnings.filter(item => item.message.includes('恢复不可用'))).toHaveLength(1);
        client.destroy();
    });

    /**
     * 测试思路：遇到 QuotaExceededError 时先清理过期 snapshot，再只重试原持久化操作一次。
     * 示例：首次 putRecordBundle 报 quota、purgeExpired 成功后第二次 put 成功，记录应落库且不产生降级告警。
     */
    it('quota 失败先清理过期工作区再重试写入', async () => {
        const live = createLiveContext();
        const persistence = live.KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test', userId: 7, tabSessionId: 'tab-quota-retry', projectId: 1, protocolId: 10,
        });
        const originalPut = adapter.putRecordBundle.bind(adapter);
        let quota = true;
        adapter.putRecordBundle = vi.fn(input => {
            if (quota) {
                quota = false;
                const error = new Error('storage quota exceeded');
                error.name = 'QuotaExceededError';
                return Promise.resolve({ status: 'error', ok: false, error });
            }
            return originalPut(input);
        });
        const purgeExpired = vi.spyOn(adapter, 'purgeExpired');
        const client = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1, protocolId: 10, persistenceAdapter: adapter, workspaceIdentity: identity,
        });
        purgeExpired.mockClear();

        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: makeRecord({ seq: 4 }) }));
        await client.flushPersistence();

        expect(adapter.putRecordBundle).toHaveBeenCalledTimes(2);
        expect(purgeExpired).toHaveBeenCalledTimes(1);
        expect((await adapter.loadWorkspace(identity.snapshotKey)).workspace.records).toHaveLength(1);
        expect(client.getPersistenceState().status).toBe('ready');
        expect(client.getState().warnings.filter(item => item.message.includes('恢复不可用'))).toHaveLength(0);
        client.destroy();
    });

    /**
     * 测试思路：cursor_reset 只能清理对应 scope；另一个 scope 的记录和游标必须保留。
     * 示例：协议缓存实例从 7 重置到 9，project:8:2 仍然可见，protocol 记录和附件索引被清理。
     */
    it('cursor_reset 只清理对应 scope', () => {
        const live = createLiveContext();
        const client = live.KitProxy.protocolInteractionLive.createClient({ projectId: 1, protocolId: 10 });
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: makeRecord() }));
        client.receiveText(JSON.stringify({
            type: 'interaction',
            delivery: 'live',
            record: makeRecord({ scope: 'project', protocol_id: 0, cache_instance_id: 8 }),
        }));
        client.receiveText(JSON.stringify({
            type: 'live_ready',
            trigger: 'open',
            project_id: 1,
            protocol_id: 10,
            session_id: 100,
            protocol_cache_info: {
                cache_instance_id: 9,
                last_seq: 0,
                catch_up_count: 0,
                catch_up_gap: false,
                cursor_reset: true,
            },
            project_cache_info: {
                cache_instance_id: 8,
                last_seq: 1,
                catch_up_count: 0,
                catch_up_gap: false,
                cursor_reset: false,
            },
        }));

        const state = client.getState();
        expect(state.pendingRecords.map(record => record.scope)).toEqual(['project']);
        expect(state.protocolCursor).toEqual({ cacheInstanceId: 9, seq: 0 });
        expect(state.projectCursor).toEqual({ cacheInstanceId: 8, seq: 1 });
        client.destroy();
    });

    /**
     * 测试思路：服务端更换 cache_instance_id 即使没有显式 cursor_reset，也必须让对应 scope 进入 reset 流程。
     * 示例：protocol 7:2 和 project 8:2 已落库，协议缓存切到 9 后只删除 protocol 记录、附件和 durable cursor。
     */
    it('cache_instance_id 变化时只清理对应 scope 的持久化工作区', async () => {
        const live = createLiveContext();
        const persistence = live.KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test', userId: 7, tabSessionId: 'tab-cache-reset', projectId: 1, protocolId: 10,
        });
        const client = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1, protocolId: 10, persistenceAdapter: adapter, workspaceIdentity: identity,
        });
        client.receiveText(liveReady({
            protocol_cache_info: { cache_instance_id: 7, last_seq: 1, catch_up_count: 0, catch_up_gap: false, cursor_reset: false },
            project_cache_info: { cache_instance_id: 8, last_seq: 1, catch_up_count: 0, catch_up_gap: false, cursor_reset: false },
        }));
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: makeRecord({ seq: 2 }) }));
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: makeRecord({
            scope: 'project', protocol_id: 0, cache_instance_id: 8, seq: 2,
        }) }));
        await client.flushPersistence();

        client.receiveText(liveReady({
            session_id: 100,
            protocol_cache_info: { cache_instance_id: 9, last_seq: 0, catch_up_count: 0, catch_up_gap: false, cursor_reset: false },
            project_cache_info: { cache_instance_id: 8, last_seq: 2, catch_up_count: 0, catch_up_gap: false, cursor_reset: false },
        }));
        await client.flushPersistence();

        const workspace = (await adapter.loadWorkspace(identity.snapshotKey)).workspace;
        expect(workspace.records.map(entry => entry.scope)).toEqual(['project']);
        expect(workspace.snapshot.protocolLiveCursor).toEqual({ cacheInstanceId: 9, seq: 0 });
        expect(workspace.snapshot.protocolDurableCursor).toBeNull();
        expect(workspace.snapshot.projectDurableCursor).toEqual({ cacheInstanceId: 8, seq: 2 });
        expect(client.getState().pendingRecords.map(record => record.scope)).toEqual(['project']);
        client.destroy();
    });

    /**
     * 测试思路：
     *
     * 测什么：分别对 protocol 和 project 执行 cursor_reset 时，只删除
     * 目标 scope 的记录、附件、pending ref、Blob URL、durable cursor 和 IndexedDB
     * workspace 条目，另一 scope 必须完整保留。
     * 为什么这么测：现有用例只覆盖 protocol reset，且没有同时验证
     * 两域附件和持久化清理边界，不能证明 project-only 路径是对称的。
     * 怎么测：用 memory adapter 建立双域工作区，两条记录各绑定一个
     * binary payload 并 flush；注入只有目标域 `cursor_reset=true` 的 live_ready，
     * 再同时检查 client state、revoke 调用和 adapter workspace。
     *
     * 示例：
     * protocol reset -> remove protocol:7:1/pa -> keep project:8:1/qa；
     * project reset  -> remove project:8:1/qa  -> keep protocol:7:1/pa。
     */
    it.each(['protocol', 'project'])('%s cursor_reset 只清理目标域记录、附件和持久化游标', async (resetScope) => {
        const live = createLiveContext();
        const persistence = live.KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test', userId: 7,
            tabSessionId: `tab-reset-${resetScope}`, projectId: 1, protocolId: 10,
        });
        const revokeObjectURL = vi.fn();
        live.URL.createObjectURL = vi.fn(() => `blob:reset-${resetScope}`);
        live.URL.revokeObjectURL = revokeObjectURL;
        const client = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1, protocolId: 10, persistenceAdapter: adapter, workspaceIdentity: identity,
        });
        client.receiveText(liveReady({
            protocol_cache_info: { cache_instance_id: 7, last_seq: 0, catch_up_count: 0, catch_up_gap: false, cursor_reset: false },
            project_cache_info: { cache_instance_id: 8, last_seq: 0, catch_up_count: 0, catch_up_gap: false, cursor_reset: false },
        }));

        const makeAttachmentRecord = (scope) => {
            const isProject = scope === 'project';
            const base = makeRecord({
                scope,
                protocol_id: isProject ? 0 : 10,
                cache_instance_id: isProject ? 8 : 7,
                seq: 1,
                result: isProject ? 'route_not_found' : 'matched',
            });
            const attachmentId = isProject ? 'qa' : 'pa';
            return makeRecord(Object.assign({}, base, {
                request: Object.assign({}, base.request, {
                    body: {
                        kind: 'binary', expect_kind: 'binary', size: 2, captured_size: 2,
                        text: '', attachments: [{
                            attachment_id: attachmentId, side: 'request', flag: 'request.body',
                            kind: 'binary', captured_size: 2, binary_available: true, truncated: false,
                        }],
                    },
                }),
            }));
        };
        const makeFrame = (record, attachmentId, bytes) => {
            const headerBytes = new TextEncoder().encode(JSON.stringify({
                type: 'attachment', scope: record.scope, project_id: 1,
                protocol_id: record.protocol_id, cache_instance_id: record.cache_instance_id,
                record_seq: record.seq, attachment_id: attachmentId,
                captured_size: bytes.length,
            }));
            const frame = new Uint8Array(4 + headerBytes.length + bytes.length);
            new DataView(frame.buffer).setUint32(0, headerBytes.length, false);
            frame.set(headerBytes, 4);
            frame.set(bytes, 4 + headerBytes.length);
            return frame;
        };
        const protocolRecord = makeAttachmentRecord('protocol');
        const projectRecord = makeAttachmentRecord('project');
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: protocolRecord }));
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: projectRecord }));
        client.receiveBinary(makeFrame(protocolRecord, 'pa', new Uint8Array([1, 2])));
        client.receiveBinary(makeFrame(projectRecord, 'qa', new Uint8Array([3, 4])));
        await client.flushPersistence();

        client.receiveText(liveReady({
            session_id: 100,
            protocol_cache_info: {
                cache_instance_id: resetScope === 'protocol' ? 9 : 7,
                last_seq: resetScope === 'protocol' ? 0 : 1,
                catch_up_count: 0, catch_up_gap: false,
                cursor_reset: resetScope === 'protocol',
            },
            project_cache_info: {
                cache_instance_id: resetScope === 'project' ? 10 : 8,
                last_seq: resetScope === 'project' ? 0 : 1,
                catch_up_count: 0, catch_up_gap: false,
                cursor_reset: resetScope === 'project',
            },
        }));
        await client.flushPersistence();

        const survivorScope = resetScope === 'protocol' ? 'project' : 'protocol';
        const resetPrefix = `${resetScope}:`;
        const state = client.getState();
        expect(state.pendingRecords.map(record => record.scope)).toEqual([survivorScope]);
        expect(Array.from(state.attachmentPayloads.keys()).every(key => key.startsWith(`${survivorScope}:`))).toBe(true);
        expect(Array.from(state.pendingAttachmentRefs.keys()).some(key => key.startsWith(resetPrefix))).toBe(false);
        expect(state[resetScope === 'protocol' ? 'protocolDurableCursor' : 'projectDurableCursor']).toBeNull();
        expect(state[survivorScope === 'protocol' ? 'protocolDurableCursor' : 'projectDurableCursor']).toBeTruthy();
        expect(revokeObjectURL).toHaveBeenCalledTimes(1);

        const workspace = (await adapter.loadWorkspace(identity.snapshotKey)).workspace;
        expect(workspace.records.map(entry => entry.scope)).toEqual([survivorScope]);
        expect(workspace.attachments).toHaveLength(1);
        expect(workspace.attachments[0].recordKey.startsWith(`${survivorScope}:`)).toBe(true);
        client.destroy();
    });

    /**
     * 测试思路：用户点击清空只删除本地记录和附件，不应把服务端 catch-up 游标退回起点。
     * 示例：protocol/project 各有一条记录时 clearRecords 后 records 为空，但 live/durable cursor 仍保持原值。
     */
    it('清空实时记录同步删除持久化记录但保留双 scope 游标', async () => {
        const live = createLiveContext();
        const persistence = live.KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test', userId: 7, tabSessionId: 'tab-clear-all', projectId: 1, protocolId: 10,
        });
        const client = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1, protocolId: 10, persistenceAdapter: adapter, workspaceIdentity: identity,
        });
        client.receiveText(liveReady());
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: makeRecord({ seq: 4 }) }));
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: makeRecord({
            scope: 'project', protocol_id: 0, cache_instance_id: 8, seq: 3,
        }) }));
        await client.flushPersistence();
        const before = client.getState();

        client.clearRecords();
        await client.flushPersistence();

        const workspace = (await adapter.loadWorkspace(identity.snapshotKey)).workspace;
        expect(workspace.records).toHaveLength(0);
        expect(workspace.snapshot.protocolLiveCursor).toEqual(before.protocolCursor);
        expect(workspace.snapshot.projectLiveCursor).toEqual(before.projectCursor);
        expect(workspace.snapshot.protocolDurableCursor).toEqual(before.protocolDurableCursor);
        expect(workspace.snapshot.projectDurableCursor).toEqual(before.projectDurableCursor);
        client.destroy();
    });

    /**
     * 测试思路：附件帧使用 4 字节大端 Header 长度，先收到记录再收到二进制帧时应延迟绑定。
     * 示例：interaction 声明 image:a1，随后 frame(header captured_size=3 + payload=01 02 03)，产生可用附件。
     */
    it('解析附件帧并支持记录先到、附件后绑定', () => {
        const live = createLiveContext();
        const client = live.KitProxy.protocolInteractionLive.createClient({ projectId: 1, protocolId: 10 });
        const record = makeRecord({
            request: Object.assign({}, makeRecord().request, {
                body: {
                    kind: 'image',
                    expect_kind: 'image',
                    attachments: [{
                        attachment_id: 'a1', kind: 'image', captured_size: 3, binary_available: true,
                    }],
                },
            }),
        });
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record }));

        const header = JSON.stringify({
            type: 'attachment', scope: 'protocol', project_id: 1, protocol_id: 10,
            cache_instance_id: 7, record_seq: 1, attachment_id: 'a1', captured_size: 3, sha1: 'sha1',
        });
        const headerBytes = new TextEncoder().encode(header);
        const payload = new Uint8Array([1, 2, 3]);
        const frame = new Uint8Array(4 + headerBytes.length + payload.length);
        new DataView(frame.buffer).setUint32(0, headerBytes.length, false);
        frame.set(headerBytes, 4);
        frame.set(payload, 4 + headerBytes.length);

        expect(client.receiveBinary(frame)).toBe(true);
        const attachment = client.getAttachment('protocol:7:1', 'a1');
        expect(attachment.bytes).toEqual(new Uint8Array([1, 2, 3]));
        expect(attachment.sha1).toBe('sha1');
        client.destroy();
    });

    /**
     * 测试思路：live client 收到二进制帧后必须把 payload 和 record 完整性一起交给持久化 adapter，同时不把当前页 objectUrl 写入存储。
     * 示例：JSON 声明 protocol:7:1:a1，收到 [1,2,3] 后 workspace record=complete、附件 payload 保留字节且无 objectUrl。
     */
    it('接收附件后持久化 payload 并完成��录', async () => {
        const live = createLiveContext();
        const persistence = live.KitProxy.protocolInteractionPersistence;
        const adapter = persistence.createMemoryPersistenceAdapter();
        const identity = persistence.createWorkspaceIdentity({
            apiBaseUrl: 'https://example.test', userId: 7, tabSessionId: 'tab-a', projectId: 1, protocolId: 10,
        });
        const client = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1, protocolId: 10, persistenceAdapter: adapter, workspaceIdentity: identity,
        });
        const record = makeRecord({
            request: Object.assign({}, makeRecord().request, {
                body: {
                    kind: 'image', expect_kind: 'image',
                    attachments: [{ attachment_id: 'a1', kind: 'image', captured_size: 3, binary_available: true }],
                },
            }),
        });
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record }));
        const headerBytes = new TextEncoder().encode(JSON.stringify({
            type: 'attachment', scope: 'protocol', project_id: 1, protocol_id: 10,
            cache_instance_id: 7, record_seq: 1, attachment_id: 'a1', captured_size: 3, sha1: 'sha1',
        }));
        const payload = new Uint8Array([1, 2, 3]);
        const frame = new Uint8Array(4 + headerBytes.length + payload.length);
        new DataView(frame.buffer).setUint32(0, headerBytes.length, false);
        frame.set(headerBytes, 4);
        frame.set(payload, 4 + headerBytes.length);

        expect(client.receiveBinary(frame)).toBe(true);
        await client.flushPersistence();
        const workspace = (await adapter.loadWorkspace(identity.snapshotKey)).workspace;
        expect(workspace.records[0]).toMatchObject({ completionState: 'complete', storedAttachmentKeys: ['protocol:7:1:a1'] });
        expect(Array.from(workspace.attachments[0].payload)).toEqual([1, 2, 3]);
        expect(workspace.attachments[0].objectUrl).toBeUndefined();
        client.destroy();
    });

    /**
     * 测试思路：同一个 Multiform Body 的多个 part 必须以各自完整 attachment_id 建立独立缓存。
     * 示例：连续接收两个不同 attachment_id 的二进制帧，getAttachment 不能互相覆盖。
     */
    it('Multiform 多个 part 的附件 payload 独立关联', () => {
        const live = createLiveContext();
        const client = live.KitProxy.protocolInteractionLive.createClient({ projectId: 1, protocolId: 10 });
        const refs = [
            {
                attachment_id: 'request.body.multiform.1:first',
                side: 'request', flag: 'request.body.multiform.1', kind: 'binary',
                size: 2, captured_size: 2, binary_available: true, truncated: false,
            },
            {
                attachment_id: 'request.body.multiform.2:second',
                side: 'request', flag: 'request.body.multiform.2', kind: 'binary',
                size: 3, captured_size: 3, binary_available: true, truncated: false,
            },
        ];
        const baseRecord = makeRecord();
        const record = makeRecord({
            request: Object.assign({}, baseRecord.request, {
                body: { kind: 'multiform', expect_kind: 'unknown', size: 5, captured_size: 5, text: '', attachments: refs },
            }),
        });
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record }));

        const makeFrame = (attachmentId, payload) => {
            const headerBytes = new TextEncoder().encode(JSON.stringify({
                type: 'attachment', scope: 'protocol', project_id: 1, protocol_id: 10,
                cache_instance_id: 7, record_seq: 1, attachment_id: attachmentId,
                captured_size: payload.length,
            }));
            const frame = new Uint8Array(4 + headerBytes.length + payload.length);
            new DataView(frame.buffer).setUint32(0, headerBytes.length, false);
            frame.set(headerBytes, 4);
            frame.set(payload, 4 + headerBytes.length);
            return frame;
        };

        expect(client.receiveBinary(makeFrame(refs[0].attachment_id, new Uint8Array([1, 2])))).toBe(true);
        expect(client.receiveBinary(makeFrame(refs[1].attachment_id, new Uint8Array([3, 4, 5])))).toBe(true);
        expect(client.getAttachment('protocol:7:1', refs[0]).bytes).toEqual(new Uint8Array([1, 2]));
        expect(client.getAttachment('protocol:7:1', refs[1]).bytes).toEqual(new Uint8Array([3, 4, 5]));
        expect(client.getState().attachmentPayloads.size).toBe(2);
        client.destroy();
    });

    /**
     * 测试思路：用 fake timer 验证默认中速每 1000ms 只取一条 pending 记录，避免高频消息一次性改变列表。
     * 示例：连续接收 2 条，1000ms 后 visible=1，再过 1000ms 后 visible=2。
     */
    it('按 1000ms 逐条合并缓冲记录', () => {
        vi.useFakeTimers();
        try {
            const live = createLiveContext();
            const client = live.KitProxy.protocolInteractionLive.createClient({ projectId: 1, protocolId: 10 });
            client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: makeRecord({ seq: 1 }) }));
            client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: makeRecord({ seq: 2 }) }));
            expect(client.getState().visibleRecords).toHaveLength(0);
            vi.advanceTimersByTime(1000);
            expect(client.getState().visibleRecords).toHaveLength(1);
            vi.advanceTimersByTime(1000);
            expect(client.getState().visibleRecords).toHaveLength(2);
            client.destroy();
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：主动暂停合并时只暂停淡入消费，不丢失 pending；恢复后继续按原速度消费。
     * 示例：暂停期间接收 1 条并等待 1000ms 仍不可见，恢复后再等待 1000ms 才显示。
     */
    it('暂停和恢复 pending 消费不会丢失自动淡入', () => {
        vi.useFakeTimers();
        try {
            const live = createLiveContext();
            const client = live.KitProxy.protocolInteractionLive.createClient({
                projectId: 1,
                protocolId: 10,
                mergeIntervalMs: 1000,
            });
            client.pauseMerge();
            client.receiveText(JSON.stringify({
                type: 'interaction', delivery: 'live', record: makeRecord({ seq: 1 }),
            }));
            vi.advanceTimersByTime(1000);
            expect(client.getState().visibleRecords).toHaveLength(0);
            expect(client.getState().pendingRecords).toHaveLength(1);

            client.resumeMerge();
            vi.advanceTimersByTime(1000);
            expect(client.getState().visibleRecords.map(record => record.seq)).toEqual([1]);
            client.destroy();
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：回退后恢复消费必须重新从完整间隔开始，并且每个间隔只能消费一条。
     * 示例：回退 2 条后，1000ms 只显示 1 条，再过 1000ms 才显示第 2 条。
     */
    it('回退恢复后按正常合并速度逐条消费', () => {
        vi.useFakeTimers();
        try {
            const live = createLiveContext();
            const client = live.KitProxy.protocolInteractionLive.createClient({
                projectId: 1,
                protocolId: 10,
                recordOrder: 'asc',
                maxVisibleRecords: 2,
                maxPendingRecords: 4,
                mergeIntervalMs: 1,
            });
            [1, 2].forEach(seq => {
                client.receiveText(JSON.stringify({
                    type: 'interaction', delivery: 'live', record: makeRecord({ seq, time_ms: seq }),
                }));
                vi.advanceTimersByTime(1);
            });
            client.setMergeSpeed('medium');
            client.pauseMerge();
            client.setBufferLimits(
                { maxVisibleRecords: 1, maxPendingRecords: 5 },
                {
                    reflowVisibleRecords: client.getState().visibleRecords,
                },
            );
            client.resumeMerge();

            vi.advanceTimersByTime(999);
            expect(client.getState().visibleRecords.map(record => record.seq)).toEqual([1]);
            expect(client.getState().pendingQueues.reflow.map(record => record.seq)).toEqual([2]);
            vi.advanceTimersByTime(1);
            expect(client.getState().visibleRecords.map(record => record.seq)).toEqual([2]);
            client.destroy();
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：切换排序方向只切换双端队列的入队和消费端，不重新排列已有 pending 记录。
     * 示例：正序队列 [1,2,3] 切到倒序后，新记录从队首加入为 [4,1,2,3]，再从队尾消费 3。
     */
    it('切换排序方向只切换 pending 双端队列的两端操作', () => {
        vi.useFakeTimers();
        try {
            const live = createLiveContext();
            const client = live.KitProxy.protocolInteractionLive.createClient({
                projectId: 1,
                protocolId: 10,
                recordOrder: 'asc',
                mergeIntervalMs: 1000,
            });
            [1, 2, 3].forEach(seq => {
                client.receiveText(JSON.stringify({
                    type: 'interaction', delivery: 'live', record: makeRecord({ seq, time_ms: seq }),
                }));
            });
            expect(client.getState().pendingQueues.protocol.map(record => record.seq)).toEqual([1, 2, 3]);

            client.setRecordOrder('desc');
            expect(client.getState().pendingQueues.protocol.map(record => record.seq)).toEqual([1, 2, 3]);

            client.receiveText(JSON.stringify({
                type: 'interaction', delivery: 'live', record: makeRecord({ seq: 4, time_ms: 4 }),
            }));
            expect(client.getState().pendingQueues.protocol.map(record => record.seq)).toEqual([4, 1, 2, 3]);
            vi.advanceTimersByTime(1000);
            expect(client.getState().visibleRecords.map(record => record.seq)).toEqual([3]);
            expect(client.getState().pendingQueues.protocol.map(record => record.seq)).toEqual([4, 1, 2]);
            client.destroy();
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：容量淘汰的方向必须和插入方向配套，且淘汰事件要在新记录可见事件之前发出。
     * 示例：倒序为 [1,2,3] 追加 4 后淘汰 1；正序为 [3,2,1] 插入 4 后淘汰 1。
     */
    it('正序头部插入、倒序尾部插入并从对应端淘汰', () => {
        vi.useFakeTimers();
        try {
            const receiveRecords = (recordOrder) => {
                const live = createLiveContext();
                const events = [];
                const client = live.KitProxy.protocolInteractionLive.createClient({
                    projectId: 1,
                    protocolId: 10,
                    recordOrder,
                    maxVisibleRecords: 3,
                    mergeIntervalMs: 1,
                });
                client.on('recordEvicted', payload => events.push(`evicted:${payload.record.seq}`));
                client.on('recordVisible', payload => events.push(`visible:${payload.record.seq}`));
                for (let seq = 1; seq <= 3; seq += 1) {
                    client.receiveText(JSON.stringify({
                        type: 'interaction', delivery: 'live', record: makeRecord({ seq, time_ms: seq }),
                    }));
                    vi.advanceTimersByTime(1);
                }
                return { client, events };
            };

            const desc = receiveRecords('desc');
            expect(desc.client.getState().visibleRecords.map(record => record.seq)).toEqual([1, 2, 3]);
            desc.client.receiveText(JSON.stringify({
                type: 'interaction', delivery: 'live', record: makeRecord({ seq: 4, time_ms: 4 }),
            }));
            vi.advanceTimersByTime(1);
            expect(desc.client.getState().visibleRecords.map(record => record.seq)).toEqual([2, 3, 4]);
            expect(desc.events.slice(-2)).toEqual(['evicted:1', 'visible:4']);
            desc.client.destroy();

            const asc = receiveRecords('asc');
            expect(asc.client.getState().visibleRecords.map(record => record.seq)).toEqual([3, 2, 1]);
            asc.client.receiveText(JSON.stringify({
                type: 'interaction', delivery: 'live', record: makeRecord({ seq: 4, time_ms: 4 }),
            }));
            vi.advanceTimersByTime(1);
            expect(asc.client.getState().visibleRecords.map(record => record.seq)).toEqual([4, 3, 2]);
            expect(asc.events.slice(-2)).toEqual(['evicted:1', 'visible:4']);
            asc.client.destroy();
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：被选中的记录淘汰后只能清空选择，不能自动替换成剩余记录。
     * 示例：正序列表 [2,1] 选中 1，插入 3 淘汰 1 后 selectedRecordKey 必须为 null。
     */
    it('选中记录淘汰后使选择失效而不自动替换', () => {
        vi.useFakeTimers();
        try {
            const live = createLiveContext();
            const selectionEvents = [];
            const client = live.KitProxy.protocolInteractionLive.createClient({
                projectId: 1,
                protocolId: 10,
                recordOrder: 'asc',
                maxVisibleRecords: 2,
                maxPendingRecords: 4,
                mergeIntervalMs: 1,
            });
            client.on('selectionChanged', payload => selectionEvents.push(payload.record || null));

            [1, 2].forEach(seq => {
                client.receiveText(JSON.stringify({
                    type: 'interaction', delivery: 'live', record: makeRecord({ seq, time_ms: seq }),
                }));
                vi.advanceTimersByTime(1);
            });
            expect(client.getState().visibleRecords.map(record => record.seq)).toEqual([2, 1]);
            expect(client.selectRecord('protocol:7:1')).toBe(true);

            client.receiveText(JSON.stringify({
                type: 'interaction', delivery: 'live', record: makeRecord({ seq: 3, time_ms: 3 }),
            }));
            vi.advanceTimersByTime(1);

            expect(client.getState().visibleRecords.map(record => record.seq)).toEqual([3, 2]);
            expect(client.getState().selectedRecordKey).toBe(null);
            expect(selectionEvents.at(-1)).toBe(null);
            client.destroy();
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：实时 client 的容量可在抽屉展开状态变化时动态调整；pending 固定为 20，
     * 非全屏为 10/20（总量 30），全屏为 20/20（总量 40），每个 scope 分别计算。
     * 示例：创建 10/20 后切换 20/20，再缩回 10/20 时多出的可见记录从对应淘汰方向释放。
     */
    it('动态调整可见容量且 pending 固定 20', () => {
        vi.useFakeTimers();
        try {
            const live = createLiveContext();
            const client = live.KitProxy.protocolInteractionLive.createClient({
                projectId: 1,
                protocolId: 10,
                maxVisibleRecords: 10,
                maxPendingRecords: 20,
                mergeIntervalMs: 1,
            });
            for (let seq = 1; seq <= 10; seq += 1) {
                client.receiveText(JSON.stringify({
                    type: 'interaction', delivery: 'live', record: makeRecord({ seq, time_ms: seq }),
                }));
            }
            vi.advanceTimersByTime(10);
            expect(client.getState().visibleRecords).toHaveLength(10);

            for (let seq = 11; seq <= 40; seq += 1) {
                client.receiveText(JSON.stringify({
                    type: 'interaction', delivery: 'live', record: makeRecord({ seq, time_ms: seq }),
                }));
            }
            expect(client.getState().pendingRecords).toHaveLength(20);
            expect(client.getState().visibleRecords.length + client.getState().pendingRecords.length).toBe(30);

            client.setBufferLimits({ maxVisibleRecords: 20, maxPendingRecords: 20 });
            expect(client.getState().maxVisibleRecords).toBe(20);
            expect(client.getState().maxPendingRecords).toBe(20);
            expect(client.getState().visibleRecords).toHaveLength(20);
            expect(client.getState().pendingRecords).toHaveLength(10);
            expect(client.getState().visibleRecords.length + client.getState().pendingRecords.length).toBe(30);

            for (let seq = 41; seq <= 50; seq += 1) {
                client.receiveText(JSON.stringify({
                    type: 'interaction', delivery: 'live', record: makeRecord({ seq, time_ms: seq }),
                }));
            }
            expect(client.getState().pendingRecords).toHaveLength(20);
            expect(client.getState().visibleRecords.length + client.getState().pendingRecords.length).toBe(40);

            client.setBufferLimits({ maxVisibleRecords: 10, maxPendingRecords: 20 });
            expect(client.getState().maxVisibleRecords).toBe(10);
            expect(client.getState().maxPendingRecords).toBe(20);
            expect(client.getState().visibleRecords).toHaveLength(10);
            vi.advanceTimersByTime(1000);
            expect(client.getState().pendingRecords.length).toBeLessThanOrEqual(20);
            expect(client.getState().visibleRecords.length + client.getState().pendingRecords.length).toBeLessThanOrEqual(30);
            client.destroy();
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：protocol 和 project 必须各自拥有 visible、pending 和 total 容量预算，
     * 一个 scope 注入到上限时不能淘汰另一个 scope 的记录。
     * 示例：protocol/project 各先显示 10 条，再各追加 20 条 pending，最终两域均为 10+20=30。
     */
    it('双 scope 分别计算 visible、pending 和 total 容量', () => {
        vi.useFakeTimers();
        try {
            const live = createLiveContext();
            const client = live.KitProxy.protocolInteractionLive.createClient({
                projectId: 1,
                protocolId: 10,
                recordOrder: 'asc',
                maxVisibleRecords: 10,
                maxPendingRecords: 20,
                mergeIntervalMs: 1,
            });
            for (let seq = 1; seq <= 10; seq += 1) {
                client.receiveText(JSON.stringify({
                    type: 'interaction', delivery: 'live', record: makeRecord({ seq, time_ms: seq }),
                }));
                client.receiveText(JSON.stringify({
                    type: 'interaction', delivery: 'live', record: makeRecord({
                        scope: 'project', protocol_id: 0, cache_instance_id: 8,
                        seq, time_ms: seq + 0.5,
                    }),
                }));
                vi.advanceTimersByTime(2);
            }
            client.pauseMerge();
            for (let seq = 11; seq <= 30; seq += 1) {
                client.receiveText(JSON.stringify({
                    type: 'interaction', delivery: 'live', record: makeRecord({ seq, time_ms: seq }),
                }));
                client.receiveText(JSON.stringify({
                    type: 'interaction', delivery: 'live', record: makeRecord({
                        scope: 'project', protocol_id: 0, cache_instance_id: 8,
                        seq, time_ms: seq + 0.5,
                    }),
                }));
            }
            const state = client.getState();
            const records = state.visibleRecords.concat(state.pendingRecords);
            const counts = scope => {
                const visible = state.visibleRecords.filter(record => record.scope === scope).length;
                const pending = state.pendingQueues[scope === 'protocol' ? 'protocol' : 'notice']
                    .filter(record => record.scope === scope).length;
                return { visible, pending, total: visible + pending };
            };
            expect(counts('protocol')).toEqual({ visible: 10, pending: 20, total: 30 });
            expect(counts('project')).toEqual({ visible: 10, pending: 20, total: 30 });
            expect(records.filter(record => record.scope === 'protocol')).toHaveLength(30);
            expect(records.filter(record => record.scope === 'project')).toHaveLength(30);
            expect(state.recordKeys.size).toBe(60);
            client.destroy();
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：Fake WebSocket 完成 open/live_ready 后暂停，服务端 ACK 必须使用 client_seq=1；恢复必须等待 live_ready 与最终 state ACK。
     * 示例：pause #1 -> state(paused)，resume #2 -> live_ready(resume) -> state(active)。
     */
    it('pause/resume 严格按当前 session 的连续命令序号确认', () => {
        const live = createLiveContext();
        const socket = createFakeSocket();
        const client = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1,
            protocolId: 10,
            transportFactory: () => socket,
        });
        client.connect();
        socket.onopen();
        socket.onmessage({ data: liveReady() });

        expect(client.pause()).toBe(true);
        expect(socket.sent[0]).toMatchObject({ command: 'pause', client_seq: 1, session_id: 99 });
        socket.onmessage({ data: JSON.stringify({
            type: 'state', command: 'pause', ok: true, state: 'paused', client_seq: 1,
            accepted_seq: 1, protocol_cursor: { cache_instance_id: 7, seq: 3 },
            project_cursor: { cache_instance_id: 8, seq: 2 }, timestamp: 1,
        }) });
        expect(client.getState().connectionState).toBe('paused');

        expect(client.resume()).toBe(true);
        expect(socket.sent[1]).toMatchObject({ command: 'resume', client_seq: 2, session_id: 99 });
        socket.onmessage({ data: liveReady({ trigger: 'resume' }) });
        expect(client.getState().connectionState).toBe('catching_up');
        socket.onmessage({ data: JSON.stringify({
            type: 'state', command: 'resume', ok: true, state: 'active', client_seq: 2,
            accepted_seq: 2, protocol_cursor: { cache_instance_id: 7, seq: 3 },
            project_cursor: { cache_instance_id: 8, seq: 2 }, timestamp: 2,
        }) });
        expect(client.getState().connectionState).toBe('active');
        client.destroy();
    });

    /**
     * 测试思路：ACK 的 client_seq 不等于当前在途命令时不能提交任何本地状态。
     * 示例：pause #1 在途时收到 client_seq=0 的 paused ACK，状态仍保持 pausing。
     */
    it('忽略过期 ACK，不提交不匹配的本地命令状态', () => {
        const live = createLiveContext();
        const socket = createFakeSocket();
        const client = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1,
            protocolId: 10,
            transportFactory: () => socket,
        });
        client.connect();
        socket.onopen();
        socket.onmessage({ data: liveReady() });
        client.pause();
        socket.onmessage({ data: JSON.stringify({
            type: 'state', command: 'pause', ok: true, state: 'paused', client_seq: 0,
            accepted_seq: 0, protocol_cursor: { cache_instance_id: 7, seq: 3 },
            project_cursor: { cache_instance_id: 8, seq: 2 }, timestamp: 1,
        }) });
        expect(client.getState().connectionState).toBe('pausing');
        expect(client.getState().pendingCommand.clientSeq).toBe(1);
        client.destroy();
    });

    /**
     * 测试思路：主动断开必须取消 desiredConnected 与重连；异常 close 才允许按退避重新 connect。
     * 示例：close(1006) 后等待 1000ms 创建新 socket，用户 disconnect 后再 close 不创建新 socket。
     */
    it('主动断开不重连，异常断线按退避重连', () => {
        vi.useFakeTimers();
        try {
            const live = createLiveContext();
            const sockets = [];
            const client = live.KitProxy.protocolInteractionLive.createClient({
                projectId: 1,
                protocolId: 10,
                transportFactory: () => {
                    const socket = createFakeSocket();
                    sockets.push(socket);
                    return socket;
                },
            });
            client.connect();
            expect(sockets).toHaveLength(1);
            sockets[0].onclose({ code: 1006, reason: 'network' });
            expect(client.getState().connectionState).toBe('reconnect_wait');
            vi.advanceTimersByTime(1000);
            expect(sockets).toHaveLength(2);

            client.disconnect();
            sockets[1].onclose({ code: 1006, reason: 'network after user disconnect' });
            vi.advanceTimersByTime(20000);
            expect(sockets).toHaveLength(2);
            client.destroy();
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：重连后旧 WebSocket 的迟到事件不能覆盖新 session 的状态。
     * 示例：session A 异常断开创建 session B，随后 A 再发送 live_ready/close，当前连接仍必须是 B。
     */
    it('忽略旧 WebSocket session 的迟到消息和关闭事件', () => {
        vi.useFakeTimers();
        try {
            const live = createLiveContext();
            const sockets = [];
            const client = live.KitProxy.protocolInteractionLive.createClient({
                projectId: 1,
                protocolId: 10,
                transportFactory: url => {
                    const socket = createFakeSocket();
                    socket.url = url;
                    sockets.push(socket);
                    return socket;
                },
            });

            client.connect();
            const first = sockets[0];
            first.onopen();
            first.onmessage({ data: liveReady({ session_id: 101 }) });
            first.onclose({ code: 1006, reason: 'network' });

            vi.advanceTimersByTime(1000);
            expect(sockets).toHaveLength(2);
            const second = sockets[1];
            second.onopen();
            second.onmessage({ data: liveReady({ session_id: 202 }) });

            first.onmessage({ data: liveReady({ session_id: 101 }) });
            first.onclose({ code: 1006, reason: 'late old socket close' });

            expect(client.getState()).toMatchObject({
                connectionState: 'active',
                sessionId: 202,
                socketSessionId: 202,
                socket: second,
            });
            expect(second.closeCalls).toHaveLength(0);

            second.onmessage({ data: JSON.stringify({ type: 'state', session_id: 101 }) });
            expect(second.closeCalls).toHaveLength(1);
            expect(second.closeCalls[0]).toMatchObject({ code: 1011, reason: 'stale session message' });
            client.destroy();
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：本地快照已恢复的记录再次收到 catch_up 时，重复消息仍必须计入当前 session 的补发进度。
     * 示例：已有 seq=1/2，live_ready 声明 catch_up_count=2，重放两条重复记录后状态进入 active。
     */
    it('重复 catch_up 记录可以完成当前 session 的补发', () => {
        const live = createLiveContext();
        const socket = createFakeSocket();
        const client = live.KitProxy.protocolInteractionLive.createClient({
            projectId: 1,
            protocolId: 10,
            transportFactory: () => socket,
        });

        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: makeRecord({ seq: 1 }) }));
        client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record: makeRecord({ seq: 2 }) }));
        client.connect();
        socket.onopen();
        socket.onmessage({ data: liveReady({
            session_id: 303,
            protocol_cache_info: { cache_instance_id: 7, last_seq: 2, live_start_seq: 3, catch_up_count: 2, catch_up_gap: false, cursor_reset: false },
        }) });
        socket.onmessage({ data: JSON.stringify({ type: 'interaction', delivery: 'catch_up', record: makeRecord({ seq: 1 }) }) });
        socket.onmessage({ data: JSON.stringify({ type: 'interaction', delivery: 'catch_up', record: makeRecord({ seq: 2 }) }) });

        expect(client.getState()).toMatchObject({
            connectionState: 'active',
            sessionId: 303,
            socketSessionId: 303,
            catchUpExpected: 2,
            catchUpReceived: 2,
        });
        client.destroy();
    });

    /**
     * 测试思路：默认 client 的 pending 固定为 20，列表与 pending 合计受 40 条上限约束，
     * 记录被淘汰时应释放其附件对象 URL。
     * 示例：发送 41 条时列表尚未消费，pending 只保留最新 20 条并淘汰最早记录；逐条合并后列表最终保留 20 条。
     */
    it('限制可见和 pending 容量并释放淘汰记录资源', () => {
        vi.useFakeTimers();
        const revokeObjectURL = vi.fn();
        const oldRevoke = globalThis.URL.revokeObjectURL;
        globalThis.URL.revokeObjectURL = revokeObjectURL;
        try {
            const live = createLiveContext();
            const client = live.KitProxy.protocolInteractionLive.createClient({ projectId: 1, protocolId: 10 });
            for (let seq = 1; seq <= 41; seq += 1) {
                client.receiveText(JSON.stringify({
                    type: 'interaction', delivery: 'live', record: makeRecord({ seq }),
                }));
            }
            expect(client.getState().pendingRecords).toHaveLength(20);
            expect(client.getState().recordKeys.has('protocol:7:1')).toBe(false);
            vi.advanceTimersByTime(20 * 1000);
            expect(client.getState().visibleRecords).toHaveLength(20);
            expect(client.getState().recordKeys.has('protocol:7:1')).toBe(false);
            client.destroy();
        } finally {
            globalThis.URL.revokeObjectURL = oldRevoke;
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：全屏回退记录暂存到 reflow 队列后，清空客户端也必须释放其中附件的对象 URL。
     * 示例：第 11 条记录携带图片附件，切回单列后进入 reflow，再清空记录，附件对象 URL 应被回收。
     */
    it('清空记录时释放 reflow 队列中的附件资源', () => {
        vi.useFakeTimers();
        const live = createLiveContext();
        const createObjectURL = vi.fn(() => 'blob:reflow-image');
        const revokeObjectURL = vi.fn();
        const oldCreate = live.URL.createObjectURL;
        const oldRevoke = live.URL.revokeObjectURL;
        live.URL.createObjectURL = createObjectURL;
        live.URL.revokeObjectURL = revokeObjectURL;
        try {
            const client = live.KitProxy.protocolInteractionLive.createClient({
                projectId: 1,
                protocolId: 10,
                mergeIntervalMs: 1,
            });
            const attachmentRef = {
                attachment_id: 'request.body.image:reflow',
                side: 'request',
                flag: 'request.body',
                kind: 'binary',
                size: 2,
                captured_size: 2,
                binary_available: true,
                truncated: false,
            };
            for (let seq = 1; seq <= 11; seq += 1) {
                const base = makeRecord({ seq, time_ms: seq });
                const record = seq === 11
                    ? Object.assign({}, base, {
                        request: Object.assign({}, base.request, {
                            body: Object.assign({}, base.request.body, { kind: 'image', attachments: [attachmentRef] }),
                        }),
                    })
                    : base;
                client.receiveText(JSON.stringify({ type: 'interaction', delivery: 'live', record }));
                vi.advanceTimersByTime(1);
            }
            const headerBytes = new TextEncoder().encode(JSON.stringify({
                type: 'attachment', scope: 'protocol', project_id: 1, protocol_id: 10,
                cache_instance_id: 7, record_seq: 11, attachment_id: attachmentRef.attachment_id,
                captured_size: 2,
            }));
            const frame = new Uint8Array(4 + headerBytes.length + 2);
            new DataView(frame.buffer).setUint32(0, headerBytes.length, false);
            frame.set(headerBytes, 4);
            frame.set(new Uint8Array([1, 2]), 4 + headerBytes.length);
            expect(client.receiveBinary(frame)).toBe(true);
            expect(client.getState().attachmentPayloads.size).toBe(1);

            client.setBufferLimits(
                { maxVisibleRecords: 10, maxPendingRecords: 30 },
                { reflowVisibleRecords: client.getState().visibleRecords },
            );
            expect(client.getState().pendingQueues.reflow.map(record => record.seq)).toContain(11);
            client.clearRecords();
            expect(revokeObjectURL).toHaveBeenCalledWith('blob:reflow-image');
            client.destroy();
        } finally {
            live.URL.createObjectURL = oldCreate;
            live.URL.revokeObjectURL = oldRevoke;
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：全屏切回单列时，从可见区移出的回退记录必须放回所属队列队首，并按消费顺序排列。
     * 示例：回退选择 [4,3] 后，队列最终为 [3,4]；再收到 5 时，新记录追加在回退记录之后。
     */
    it('回退记录放回所属 pending 队列队首且新事件不改写其位置', () => {
        vi.useFakeTimers();
        try {
            const live = createLiveContext();
            const client = live.KitProxy.protocolInteractionLive.createClient({
                projectId: 1,
                protocolId: 10,
                recordOrder: 'asc',
                maxVisibleRecords: 4,
                maxPendingRecords: 4,
                mergeIntervalMs: 1,
            });
            for (let seq = 1; seq <= 4; seq += 1) {
                client.receiveText(JSON.stringify({
                    type: 'interaction', delivery: 'live', record: makeRecord({ seq, time_ms: seq }),
                }));
                vi.advanceTimersByTime(1);
            }
            expect(client.getState().visibleRecords.map(record => record.seq)).toEqual([4, 3, 2, 1]);

            client.setBufferLimits(
                { maxVisibleRecords: 2, maxPendingRecords: 4 },
                {
                    reflowVisibleRecords: client.getState().visibleRecords,
                },
            );
            expect(client.getState().visibleRecords.map(record => record.seq)).toEqual([2, 1]);
            expect(client.getState().pendingQueues.reflow.map(record => record.seq)).toEqual([3, 4]);

            client.receiveText(JSON.stringify({
                type: 'interaction', delivery: 'live', record: makeRecord({ seq: 5, time_ms: 5 }),
            }));
            expect(client.getState().pendingQueues.reflow.map(record => record.seq)).toEqual([3, 4]);
            expect(client.getState().pendingQueues.protocol.map(record => record.seq)).toEqual([5]);
            client.destroy();
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：全屏 20 条切回单列时只回退最新 10 条，并按队首消费所需顺序排列；展示不满 10 条时不回退已展示记录。
     * 示例：20 条 [20..1] 切换后列表保留 [10..1]，队列为 [11..20]；8 条切换后仍保留 8 条。
     */
    it('全屏回退只重新加入最新十条，单列不足十条时不回退', () => {
        vi.useFakeTimers();
        try {
            const live = createLiveContext();
            const client = live.KitProxy.protocolInteractionLive.createClient({
                projectId: 1,
                protocolId: 10,
                recordOrder: 'asc',
                maxVisibleRecords: 20,
                maxPendingRecords: 20,
                mergeIntervalMs: 1,
            });
            for (let seq = 1; seq <= 20; seq += 1) {
                client.receiveText(JSON.stringify({
                    type: 'interaction', delivery: 'live', record: makeRecord({ seq, time_ms: seq }),
                }));
                vi.advanceTimersByTime(1);
            }
            client.setBufferLimits(
                { maxVisibleRecords: 10, maxPendingRecords: 30 },
                {
                    reflowVisibleRecords: client.getState().visibleRecords,
                },
            );
            expect(client.getState().visibleRecords.map(record => record.seq)).toEqual([
                10, 9, 8, 7, 6, 5, 4, 3, 2, 1,
            ]);
            expect(client.getState().pendingQueues.reflow.map(record => record.seq)).toEqual([
                11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
            ]);
            client.destroy();

            const shortClient = live.KitProxy.protocolInteractionLive.createClient({
                projectId: 1,
                protocolId: 10,
                recordOrder: 'asc',
                maxVisibleRecords: 10,
                maxPendingRecords: 30,
                mergeIntervalMs: 1,
            });
            for (let seq = 1; seq <= 8; seq += 1) {
                shortClient.receiveText(JSON.stringify({
                    type: 'interaction', delivery: 'live', record: makeRecord({ seq, time_ms: seq }),
                }));
                vi.advanceTimersByTime(1);
            }
            shortClient.setBufferLimits(
                { maxVisibleRecords: 10, maxPendingRecords: 30 },
                { preserveVisibleKeys: shortClient.getState().visibleRecords.map(record => record._key) },
            );
            expect(shortClient.getState().visibleRecords).toHaveLength(8);
            expect(shortClient.getState().pendingQueues.protocol).toHaveLength(0);
            shortClient.destroy();
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：Mock transport 必须复用真实 client 的连接与消息入口，覆盖 HTTP、TCP/Hex、附件和项目 Notice。
     * 示例：connect -> live_ready -> HTTP matched/image -> HTTP request_mismatch -> Custom TCP -> Project Notice。
     */
    it('Mock transport 覆盖实时记录、项目 Notice 和附件消息组', async () => {
        vi.useFakeTimers();
        try {
            const live = createLiveContext('?apiMode=mock');
            // 此用例用 runAllTimers 校验首批固定消息；周期 live 消息由独立 E2E 覆盖。
            live.KitProxy.protocolInteractionLive.setMockScenario(1, 10, { liveEnabled: false });
            const client = live.KitProxy.protocolInteractionLive.createClient({ projectId: 1, protocolId: 10 });
            client.connect();
            vi.runAllTimers();
            await Promise.resolve();
            const state = client.getState();
            expect(state.connectionState).toBe('active');
            const records = state.visibleRecords.concat(state.pendingRecords);
            expect(records.some(record => record.scope === 'protocol')).toBe(true);
            expect(records.some(record => record.scope === 'project')).toBe(true);
            expect(records.some(record => record.result === 'request_mismatch')).toBe(true);
            expect(records.some(record => record.protocol_type === 'custom_tcp'
                && record.request.raw_packet.raw_hex.includes('48 31 30 30'))).toBe(true);
            expect(state.protocolCursor).toEqual({ cacheInstanceId: 1001, seq: 3 });
            expect(state.projectCursor).toEqual({ cacheInstanceId: 2002, seq: 1 });
            expect(state.attachmentPayloads.size).toBe(1);
            client.destroy();
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：Mock transport 必须根据当前协议项类型生成对应的交互数据，TCP 不能复用 HTTP 的 method/path 或图片样例。
     * 示例：projectId=2、protocolId=2、protocolType=TCP 建立连接后，协议记录全部为 custom_tcp，并包含功能码和 Raw Hex。
     */
    it('Mock TCP transport 只生成 Custom TCP 实时记录', async () => {
        vi.useFakeTimers();
        try {
            const live = createLiveContext('?apiMode=mock');
            // 此用例用 runAllTimers 校验首批固定 TCP 样例，不能递归推进长期 live 定时器。
            live.KitProxy.protocolInteractionLive.setMockScenario(2, 2, { liveEnabled: false });
            const client = live.KitProxy.protocolInteractionLive.createClient({
                projectId: 2,
                protocolId: 2,
                protocolType: 'TCP',
            });
            client.connect();
            vi.runAllTimers();
            await Promise.resolve();

            const records = client.getState().visibleRecords.concat(client.getState().pendingRecords);
            const protocolRecords = records.filter(record => record.scope === 'protocol');
            const projectRecords = records.filter(record => record.scope === 'project');

            expect(protocolRecords).toHaveLength(3);
            expect(protocolRecords.every(record => record.protocol_type === 'custom_tcp')).toBe(true);
            expect(protocolRecords.every(record => record.request.meta.function_code)).toBe(true);
            expect(protocolRecords.every(record => !record.request.meta.method && !record.request.meta.path)).toBe(true);
            expect(protocolRecords.every(record => record.request.raw_packet.raw_hex)).toBe(true);
            expect(protocolRecords.some(record => record.request.raw_packet.raw_hex.includes('48 31 30 30 30'))).toBe(true);
            expect(projectRecords).toHaveLength(1);
            expect(projectRecords[0].protocol_type).toBe('custom_tcp');
            client.destroy();
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：Mock 暂停期间生成的记录不能实时进入列表，resume 要按 live_ready -> catch_up -> state(active) 发送。
     * 示例：pause #1 -> paused，暂停产生 request_mismatch，resume #2 -> catch_up 记录 -> active。
     */
    it('Mock transport 覆盖暂停积压和恢复补发时序', async () => {
        vi.useFakeTimers();
        try {
            const live = createLiveContext('?apiMode=mock');
            const client = live.KitProxy.protocolInteractionLive.createClient({ projectId: 1, protocolId: 10 });
            client.connect();
            vi.advanceTimersByTime(25);
            await Promise.resolve();
            expect(client.getState().connectionState).toBe('active');
            expect(client.pause()).toBe(true);
            vi.advanceTimersByTime(1);
            await Promise.resolve();
            expect(client.getState().connectionState).toBe('paused');
            vi.advanceTimersByTime(40);
            expect(client.resume()).toBe(true);
            vi.advanceTimersByTime(30);
            await Promise.resolve();
            expect(client.getState().connectionState).toBe('active');
            expect(client.getState().visibleRecords.concat(client.getState().pendingRecords)
                .some(record => record.result === 'request_mismatch')).toBe(true);
            client.destroy();
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：Mock 场景可注入 cursor_reset 和 catch_up_gap，客户端只重置协议 scope 并保留缺口警告。
     * 示例：下一次 resume 返回 protocol cursor_reset=true/catch_up_gap=true，project scope 记录仍保留。
     */
    it('Mock transport 支持游标重置和缓存缺口场景注入', async () => {
        vi.useFakeTimers();
        try {
            const live = createLiveContext('?apiMode=mock');
            const liveApi = live.KitProxy.protocolInteractionLive;
            liveApi.setMockScenario(1, 10, { resetNextResume: true, gapNextReady: true });
            const client = liveApi.createClient({ projectId: 1, protocolId: 10 });
            client.connect();
            vi.advanceTimersByTime(100);
            await Promise.resolve();
            client.pause();
            vi.advanceTimersByTime(35);
            await Promise.resolve();
            client.resume();
            vi.advanceTimersByTime(50);
            await Promise.resolve();
            expect(client.getState().warnings.some(item => item.message.includes('缺口'))).toBe(true);
            expect(client.getState().protocolCursor.cacheInstanceId).toBe(1001);
            client.destroy();
        } finally {
            vi.useRealTimers();
        }
    });

    /**
     * 测试思路：Mock 一次性异常断线必须走真实 client 的退避重连路径，新 WebSocket session 恢复为 active。
     * 示例：open/live_ready(session A) -> close(1006) -> reconnect_wait -> 1000ms -> open/live_ready(session B) -> active。
     */
    it('Mock transport 支持异常断线和退避重连', async () => {
        vi.useFakeTimers();
        try {
            const live = createLiveContext('?apiMode=mock');
            const liveApi = live.KitProxy.protocolInteractionLive;
            liveApi.setMockScenario(1, 10, { failNextConnection: true });
            const client = liveApi.createClient({ projectId: 1, protocolId: 10 });
            client.connect();
            vi.advanceTimersByTime(30);
            await Promise.resolve();
            const firstSessionId = client.getState().sessionId;
            expect(client.getState().connectionState).toBe('active');

            vi.advanceTimersByTime(60);
            expect(client.getState().connectionState).toBe('reconnect_wait');
            vi.advanceTimersByTime(1000);
            await Promise.resolve();
            expect(client.getState().connectionState).toBe('active');
            expect(client.getState().sessionId).not.toBe(firstSessionId);
            client.destroy();
        } finally {
            vi.useRealTimers();
        }
    });
});

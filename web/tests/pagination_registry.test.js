import { describe, expect, it, vi } from 'vitest';
import { createBrowserContext, loadCoreScripts, runScript } from './helpers/browser_context.js';

describe('V1.2 pagination and protocol registry', () => {
    /**
     * 测试思路：分页请求多取一条用于判断是否存在下一页，展示时只保留 pageSize 条。
     * 示例：pageSize=2 时输入 3 条数据，visibleItems 只返回前 2 条且 hasMore=true。
     */
    it('pageSize + 1 截断后能判断是否还有下一页', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        const state = context.KitProxy.pagination.createState(2);
        const visibleItems = context.KitProxy.pagination.takeVisibleItems([1, 2, 3], state);

        expect(visibleItems).toEqual([1, 2]);
        expect(state.hasMore).toBe(true);
        expect(context.KitProxy.pagination.getRequestLimit(state)).toBe(3);
    });

    /**
     * 测试思路：删除当前页最后一条会造成空页时，应自动回退到上一页。
     * 示例：第 3 页只剩 1 条时删除后回到第 2 页；还有 2 条时仍停留第 3 页。
     */
    it('删除当前页最后一条时按规则回退上一页', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        const state = context.KitProxy.pagination.createState();
        state.currentPage = 3;

        expect(context.KitProxy.pagination.nextPageAfterDelete(state, 1)).toBe(2);
        expect(context.KitProxy.pagination.nextPageAfterDelete(state, 2)).toBe(3);
    });

    /**
     * 测试思路：服务分页的每页数量只允许使用预设选项，请求 limit 始终多取一条。
     * 示例：pageSize=5/10/20/50 时，实际请求 limit 分别是 6/11/21/51。
     */
    it('服务分页 pageSize 支持 5/10/20/50 且请求 limit 使用 pageSize + 1', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        [5, 10, 20, 50].forEach(pageSize => {
            const state = context.KitProxy.pagination.createState(pageSize);
            expect(state.pageSize).toBe(pageSize);
            expect(context.KitProxy.pagination.getRequestLimit(state)).toBe(pageSize + 1);
        });
    });

    /**
     * 测试思路：Project 分页由后端 total 决定下一页，不再依赖 pageSize+1 哨兵记录。
     * 示例：pageSize=10、total=21 时第一页可继续，第二页不可继续；total=0 时最后一页固定为 1。
     */
    it('Project 分页使用后端 total 判断下一页和最后一页', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);

        const state = context.KitProxy.pagination.createState(10);
        const firstPage = context.KitProxy.pagination.setPageResult({ items: [1], total: 21 }, state);
        expect(firstPage).toEqual([1]);
        expect(state.hasMore).toBe(true);
        expect(context.KitProxy.pagination.getLastPage(state)).toBe(3);

        state.currentPage = 3;
        context.KitProxy.pagination.setPageResult({ items: [1], total: 21 }, state);
        expect(state.hasMore).toBe(false);

        context.KitProxy.pagination.setPageResult({ items: [], total: 0 }, state);
        expect(context.KitProxy.pagination.getLastPage(state)).toBe(1);
    });

    /**
     * 测试思路：分页条渲染应包含每页数量选择器，并在变更时通知页面刷新。
     * 示例：把选择器从 10 改为 5，应触发 onPageSizeChange(5)。
     */
    it('分页条内置每页数量选择器并支持切换回调', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        const container = context.document.createElement('div');
        const state = context.KitProxy.pagination.createState(10);
        const onPageSizeChange = vi.fn();

        context.KitProxy.pagination.render(container, state, {
            pageSizeOptions: context.KitProxy.pagination.DEFAULT_PAGE_SIZE_OPTIONS,
            onPageSizeChange,
        });

        const select = container.querySelector('.pagination-page-size');
        expect(container.classList.contains('has-page-size-control')).toBe(true);
        expect(Array.from(select.options).map(option => option.value)).toEqual(['5', '10', '20', '50']);
        expect(select.value).toBe('10');

        select.value = '5';
        select.dispatchEvent(new context.Event('change', { bubbles: true }));
        expect(onPageSizeChange).toHaveBeenCalledWith(5);
    });

    /**
     * 测试思路：协议类型注册表负责把协议类型映射到对应弹窗和详情网格渲染器。
     * 示例：HTTP 类型应能拿到新增弹窗工厂，并渲染 method/path/status 等字段。
     */
    it('协议类型注册表能返回新增弹窗和协议项详情网格', () => {
        const context = createBrowserContext('?apiMode=mock');
        loadCoreScripts(context);
        [
            'js/tcp_pattern_modal.js',
            'js/add_protocol_modal.js',
            'js/protocol_item.js',
            'js/protocol_registry.js',
        ].forEach(filePath => runScript(context, filePath));

        const modalFactory = context.ProtocolTypeRegistry.getAddProtocolModal(context.ProtocolType.HTTP);
        const grid = context.ProtocolTypeRegistry.createProtocolItemGrid({
            id: 1,
            type: 'HTTP',
            req_cfg: {
                method: 'GET',
                path: '/api/test',
            },
            resp_cfg: {},
            req_body_status: 0,
            resp_body_status: 0,
        });

        expect(modalFactory).toBeTruthy();
        expect(grid.className).toContain('http');
        expect(grid.querySelector('[data-field-name="path"] .value').textContent).toBe('/api/test');
        expect(grid.querySelector('[data-field-name="path"] .value').getAttribute('title')).toBe('/api/test');
        expect(grid.querySelector('[data-field-name="status_code"] .value').textContent).toBe('200');
    });
});

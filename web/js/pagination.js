(function initKitProxyPagination(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});
    const DEFAULT_PAGE_SIZE = 5;

    /**
     * 创建页面级分页状态。
     * @param {number=} pageSize
     * @returns {{ currentPage: number; pageSize: number; hasMore: boolean; }}
     */
    function createState(pageSize = DEFAULT_PAGE_SIZE) {
        return {
            currentPage: 1,
            pageSize,
            hasMore: false,
        };
    }

    /**
     * 根据当前页计算后端列表接口 offset。
     * @param {{ currentPage: number; pageSize: number; }} state
     * @returns {number}
     */
    function getOffset(state) {
        return (Math.max(1, state.currentPage) - 1) * state.pageSize;
    }

    /**
     * 后端 limit 请求 pageSize + 1 条，用额外一条判断是否还有下一页。
     * @param {{ pageSize: number; }} state
     * @returns {number}
     */
    function getRequestLimit(state) {
        return state.pageSize + 1;
    }

    /**
     * 截取当前页真正需要展示的数据，并回写 hasMore。
     * @template T
     * @param {T[]} items
     * @param {{ pageSize: number; hasMore: boolean; }} state
     * @returns {T[]}
     */
    function takeVisibleItems(items, state) {
        const list = Array.isArray(items) ? items : [];
        state.hasMore = list.length > state.pageSize;
        return list.slice(0, state.pageSize);
    }

    /**
     * 删除当前页最后一条时，如果不是第一页，需要回退一页再刷新。
     * @param {{ currentPage: number; }} state
     * @param {number} visibleCountBeforeDelete
     * @returns {number}
     */
    function nextPageAfterDelete(state, visibleCountBeforeDelete) {
        if (visibleCountBeforeDelete <= 1 && state.currentPage > 1) {
            return state.currentPage - 1;
        }
        return state.currentPage;
    }

    /**
     * 渲染简单上一页/下一页分页条。
     * @param {HTMLElement | null} container
     * @param {{ currentPage: number; hasMore: boolean; }} state
     * @param {{ onPrev?: Function; onNext?: Function; }} handlers
     */
    function render(container, state, handlers = {}) {
        if (!container) return;

        container.innerHTML = `
            <button type="button" class="pagination-btn pagination-prev" ${state.currentPage <= 1 ? 'disabled' : ''}>上一页</button>
            <span class="pagination-current">第 ${state.currentPage} 页</span>
            <button type="button" class="pagination-btn pagination-next" ${state.hasMore ? '' : 'disabled'}>下一页</button>
        `;

        const prevBtn = container.querySelector('.pagination-prev');
        const nextBtn = container.querySelector('.pagination-next');

        if (prevBtn && typeof handlers.onPrev === 'function') {
            prevBtn.addEventListener('click', function() {
                if (state.currentPage > 1) handlers.onPrev();
            });
        }

        if (nextBtn && typeof handlers.onNext === 'function') {
            nextBtn.addEventListener('click', function() {
                if (state.hasMore) handlers.onNext();
            });
        }
    }

    KitProxy.pagination = {
        DEFAULT_PAGE_SIZE,
        createState,
        getOffset,
        getRequestLimit,
        takeVisibleItems,
        nextPageAfterDelete,
        render,
    };
})(typeof window !== 'undefined' ? window : globalThis);

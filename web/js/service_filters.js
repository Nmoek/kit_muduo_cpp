(function initKitProxyServiceFilters(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});

    function createState() {
        return {
            filters: {
                startDate: '',
                endDate: '',
                status: 'all',
                protocolType: 'all',
            },
            active: false,
        };
    }

    function readFromDOM(root) {
        const scope = root || document;

        return {
            startDate: scope.querySelector('#filter-create-start')?.value || '',
            endDate: scope.querySelector('#filter-create-end')?.value || '',
            status: scope.querySelector('#filter-status')?.value || 'all',
            protocolType: scope.querySelector('#filter-protocol-type')?.value || 'all',
        };
    }

    function validate(filters) {
        const startDate = filters.startDate || '';
        const endDate = filters.endDate || '';

        if (startDate && endDate && startDate > endDate) {
            return {
                valid: false,
                message: '创建日期开始时间不能晚于结束时间',
            };
        }

        return {
            valid: true,
            message: '',
        };
    }

    function hasActiveFilters(filters) {
        return Boolean(
            filters.startDate ||
            filters.endDate ||
            (filters.status && filters.status !== 'all') ||
            (filters.protocolType && filters.protocolType !== 'all')
        );
    }

    function getProjectDate(project) {
        const ctime = String(project && project.ctime ? project.ctime : '').trim();
        if (!ctime) return '';
        return ctime.slice(0, 10);
    }

    function matchStatus(project, statusFilter) {
        if (!statusFilter || statusFilter === 'all') return true;
        const isActive = Number(project.status) === 1;

        if (statusFilter === 'active') return isActive;
        if (statusFilter === 'inactive') return !isActive;
        return true;
    }

    function matchProtocolType(project, protocolTypeFilter) {
        if (!protocolTypeFilter || protocolTypeFilter === 'all') return true;
        return Number(project.protocol_type) === Number(protocolTypeFilter);
    }

    function matchDate(project, filters) {
        if (!filters.startDate && !filters.endDate) return true;

        const projectDate = getProjectDate(project);
        if (!projectDate) return false;

        if (filters.startDate && projectDate < filters.startDate) return false;
        if (filters.endDate && projectDate > filters.endDate) return false;

        return true;
    }

    function apply(projects, filters) {
        const list = Array.isArray(projects) ? projects : [];
        const safeFilters = filters || createState().filters;

        return list.filter(project => {
            return matchStatus(project, safeFilters.status) &&
                matchProtocolType(project, safeFilters.protocolType) &&
                matchDate(project, safeFilters);
        });
    }

    function describe(filters) {
        const activeParts = [];

        if (filters.startDate || filters.endDate) {
            activeParts.push(`创建日期 ${filters.startDate || '不限'} 至 ${filters.endDate || '不限'}`);
        }

        if (filters.status === 'active') {
            activeParts.push('状态：开启');
        } else if (filters.status === 'inactive') {
            activeParts.push('状态：未开启');
        }

        if (filters.protocolType && filters.protocolType !== 'all') {
            const label = global.ProtocolTypeStr
                ? global.ProtocolTypeStr[Number(filters.protocolType)]
                : filters.protocolType;
            activeParts.push(`协议：${label || filters.protocolType}`);
        }

        return activeParts.length ? activeParts.join('，') : '未启用筛选';
    }

    KitProxy.serviceFilters = {
        createState,
        readFromDOM,
        validate,
        hasActiveFilters,
        apply,
        describe,
    };
})(typeof window !== 'undefined' ? window : globalThis);

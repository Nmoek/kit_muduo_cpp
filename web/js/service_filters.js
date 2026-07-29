(function initKitProxyServiceFilters(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});

    function createState() {
        return {
            filters: {
                startDate: '',
                endDate: '',
                startTime: '',
                endTime: '',
                status: 'all',
                protocolType: 'all',
                ownerKeyword: '',
                ownerUserId: null,
                ownerNote: '',
            },
            active: false,
        };
    }

    function readFromDOM(root, options = {}) {
        const scope = root || document;
        const prefix = options.timeRangePrefix || 'filter-create';
        const ownerInput = scope.querySelector('#filter-owner-note');
        const ownerUserId = Number(ownerInput && ownerInput.dataset.selectedUserId);
        const directStartDate = scope.querySelector(`#${prefix}-direct-start`);
        const directEndDate = scope.querySelector(`#${prefix}-direct-end`);
        const timeRangeController = scope.querySelector(`#${prefix}-range`)?._timeRangeController;
        const committedRange = typeof timeRangeController?.read === 'function'
            ? timeRangeController.read()
            : null;
        const startDate = committedRange
            ? committedRange.startDate
            : (directStartDate?.value || scope.querySelector(`#${prefix}-start`)?.value || '');
        const endDate = committedRange
            ? committedRange.endDate
            : (directEndDate?.value || scope.querySelector(`#${prefix}-end`)?.value || '');

        return {
            startDate,
            endDate,
            startTime: committedRange
                ? committedRange.startTime
                : (scope.querySelector(`#${prefix}-start-time`)?.value || ''),
            endTime: committedRange
                ? committedRange.endTime
                : (scope.querySelector(`#${prefix}-end-time`)?.value || ''),
            status: scope.querySelector('#filter-status')?.value || 'all',
            protocolType: scope.querySelector('#filter-protocol-type')?.value || 'all',
            ownerKeyword: ownerInput?.value.trim() || '',
            ownerUserId: Number.isInteger(ownerUserId) && ownerUserId > 0 ? ownerUserId : null,
            ownerNote: ownerInput?.value.trim() || '',
        };
    }

    function validate(filters) {
        const startDate = filters.startDate || '';
        const endDate = filters.endDate || '';
        const startTime = filters.startTime || '';
        const endTime = filters.endTime || '';

        if (Boolean(startDate) !== Boolean(endDate)) {
            return {
                valid: false,
                message: '创建日期开始和结束时间必须同时填写',
            };
        }

        if ((startDate && !normalizeDateText(startDate)) || (endDate && !normalizeDateText(endDate))) {
            return {
                valid: false,
                message: '请输入有效的年月日，格式为 YYYY-MM-DD',
            };
        }

        if (startDate && endDate && startDate > endDate) {
            return {
                valid: false,
                message: '创建日期开始时间不能晚于结束时间',
            };
        }

        if (Boolean(startTime) !== Boolean(endTime)) {
            return {
                valid: false,
                message: '时间开始和结束时间必须同时填写',
            };
        }

        if ((startTime && !isCompleteTime(startTime)) || (endTime && !isCompleteTime(endTime))) {
            return {
                valid: false,
                message: '请输入有效的时分秒，格式为 HH:MM:SS',
            };
        }

        if ((startTime || endTime) && (!startDate || !endDate)) {
            return {
                valid: false,
                message: '填写时分秒前必须先填写开始和结束日期',
            };
        }

        if (startDate && endDate && startDate === endDate
            && startTime && endTime && normalizeTime(startTime) > normalizeTime(endTime)) {
            return {
                valid: false,
                message: '开始时间不能晚于结束时间',
            };
        }

        if (filters.ownerKeyword && !filters.ownerUserId) {
            return {
                valid: false,
                message: '请选择候选 note',
            };
        }

        if (filters.ownerUserId != null
            && (!Number.isInteger(Number(filters.ownerUserId)) || Number(filters.ownerUserId) <= 0)) {
            return {
                valid: false,
                message: '所有者候选无效，请重新选择',
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
            filters.startTime ||
            filters.endTime ||
            (filters.status && filters.status !== 'all') ||
            (filters.protocolType && filters.protocolType !== 'all') ||
            filters.ownerUserId ||
            filters.ownerKeyword
        );
    }

    function normalizeTime(timeText) {
        const match = String(timeText || '').match(/^(\d{2}):(\d{2})(?::(\d{2}))?$/);
        if (!match) return '';
        const hour = Number(match[1]);
        const minute = Number(match[2]);
        const second = Number(match[3] || 0);
        if (hour > 23 || minute > 59 || second > 59) return '';
        return [match[1], match[2], match[3] || '00'].join(':');
    }

    function localDateTimeBoundary(dateText, timeText, fallbackTime) {
        const [year, month, day] = String(dateText).split('-').map(Number);
        const normalizedTime = normalizeTime(timeText) || fallbackTime;
        const [hour, minute, second] = normalizedTime.split(':').map(Number);
        return new Date(year, month - 1, day, hour, minute, second).getTime();
    }

    /**
     * 将页面筛选条件转换为当前后端 /projects/list 的请求字段。
     * Project 主列表使用 user_id 和 user_note 这组已经落地的后端字段，
     * 不发送未被当前 Handler 接收的 owner_user_id/include_deleted。
     * @param {any} filters
     * @returns {Record<string, any>}
     */
    function toRequest(filters) {
        const safeFilters = filters || createState().filters;
        const request = {};

        if (safeFilters.startDate && safeFilters.endDate) {
            request.created_from = localDateTimeBoundary(safeFilters.startDate, safeFilters.startTime, '00:00:00');
            // Project DAO uses [created_from, created_to); include the entered end second.
            request.created_to = localDateTimeBoundary(safeFilters.endDate, safeFilters.endTime, '23:59:59') + 1000;
        }

        if (safeFilters.status === 'active') {
            request.status = 1;
            request.runtime_state = 1;
        } else if (safeFilters.status === 'inactive') {
            request.status = 1;
            request.runtime_state = 0;
        } else if (safeFilters.status === 'deleted') {
            request.status = 0;
        }

        if (safeFilters.protocolType && safeFilters.protocolType !== 'all') {
            request.protocol_type = Number(safeFilters.protocolType);
        }

        if (safeFilters.ownerUserId) {
            request.user_id = Number(safeFilters.ownerUserId);
        }

        return request;
    }

    function describe(filters) {
        const activeParts = [];

        if (filters.startDate || filters.endDate || filters.startTime || filters.endTime) {
            const start = filters.startDate
                ? `${filters.startDate} ${normalizeTime(filters.startTime) || '00:00:00'}`
                : '不限';
            const end = filters.endDate
                ? `${filters.endDate} ${normalizeTime(filters.endTime) || '23:59:59'}`
                : '不限';
            activeParts.push(`创建日期 ${start} 至 ${end}`);
        }

        if (filters.status === 'active') {
            activeParts.push('状态：开启');
        } else if (filters.status === 'inactive') {
            activeParts.push('状态：未开启');
        } else if (filters.status === 'deleted') {
            activeParts.push('状态：已删除');
        }

        if (filters.protocolType && filters.protocolType !== 'all') {
            const label = global.ProtocolTypeStr
                ? global.ProtocolTypeStr[Number(filters.protocolType)]
                : filters.protocolType;
            activeParts.push(`协议：${label || filters.protocolType}`);
        }

        if (filters.ownerNote || filters.ownerKeyword) {
            activeParts.push(`所有者：${filters.ownerNote || filters.ownerKeyword}`);
        }

        return activeParts.length ? activeParts.join('，') : '未启用筛选';
    }

    function normalizeDateText(dateText) {
        const match = String(dateText || '').match(/^(\d{4})[-/](\d{1,2})[-/](\d{1,2})$/);
        if (!match) return '';

        const year = Number(match[1]);
        const month = Number(match[2]);
        const day = Number(match[3]);
        const date = new Date(year, month - 1, day);
        if (date.getFullYear() !== year || date.getMonth() !== month - 1 || date.getDate() !== day) {
            return '';
        }
        return [match[1], String(month).padStart(2, '0'), String(day).padStart(2, '0')].join('-');
    }

    function parseDateRangeText(text) {
        const matches = String(text || '').match(/\d{4}[-/]\d{1,2}[-/]\d{1,2}/g) || [];
        const dates = matches.map(normalizeDateText).filter(Boolean);
        return dates.slice(0, 2);
    }

    function rangeElements(scope, prefix) {
        const id = suffix => `#${prefix}-${suffix}`;
        const trigger = scope.querySelector(id('range'));
        const panel = scope.querySelector(id('range-panel'));
        const timeRange = scope.querySelector(id('time-range'));
        const host = trigger?.closest('.filter-time-range-shell') || timeRange;
        return {
            host,
            trigger,
            panel,
            startDate: scope.querySelector(id('start')),
            endDate: scope.querySelector(id('end')),
            startTimeDisplay: scope.querySelector(id('start-clock')),
            endTimeDisplay: scope.querySelector(id('end-clock')),
            directStartDate: scope.querySelector(id('direct-start')),
            directEndDate: scope.querySelector(id('direct-end')),
            startTime: scope.querySelector(id('start-time')),
            endTime: scope.querySelector(id('end-time')),
            calendarDays: scope.querySelector(id('calendar-days')),
            calendarTitle: scope.querySelector(id('calendar-title')),
            calendarStatus: scope.querySelector(id('calendar-status')),
            calendarPrev: scope.querySelector(id('calendar-prev')),
            calendarNext: scope.querySelector(id('calendar-next')),
            panelError: scope.querySelector(id('range-error')),
            panelReset: scope.querySelector(id('range-reset')),
            panelConfirm: scope.querySelector(id('range-confirm')),
        };
    }

    function dateParts(dateText) {
        const normalized = normalizeDateText(dateText);
        if (!normalized) return null;
        const [year, month, day] = normalized.split('-').map(Number);
        return { year, month: month - 1, day };
    }

    function formatCalendarDate(year, month, day) {
        return [
            String(year).padStart(4, '0'),
            String(month + 1).padStart(2, '0'),
            String(day).padStart(2, '0'),
        ].join('-');
    }

    function formatDateInput(value) {
        const digits = String(value || '').replace(/\D/g, '').slice(0, 8);
        if (!digits) return '';

        let formatted = digits.slice(0, 4);
        if (digits.length > 4) formatted += `-${digits.slice(4, 6)}`;
        if (digits.length > 6) formatted += `-${digits.slice(6, 8)}`;
        return formatted;
    }

    function formatTimeInput(value) {
        const digits = String(value || '').replace(/\D/g, '').slice(0, 6);
        if (!digits) return '';

        let formatted = digits.slice(0, 2);
        if (digits.length > 2) formatted += `:${digits.slice(2, 4)}`;
        if (digits.length > 4) formatted += `:${digits.slice(4, 6)}`;
        return formatted;
    }

    function dateDigitsAreAllowed(digits) {
        if (digits.length >= 5) {
            const monthPrefix = Number(digits.slice(4, 5));
            if (monthPrefix > 1) return false;
        }
        if (digits.length >= 6) {
            const month = Number(digits.slice(4, 6));
            if (month < 1 || month > 12) return false;
        }
        if (digits.length >= 7 && Number(digits.slice(6, 7)) > 3) return false;
        if (digits.length === 8) {
            const year = Number(digits.slice(0, 4));
            const month = Number(digits.slice(4, 6));
            const day = Number(digits.slice(6, 8));
            const daysInMonth = new Date(year, month, 0).getDate();
            if (day < 1 || day > daysInMonth) return false;
        }
        return true;
    }

    function timeDigitsAreAllowed(digits) {
        if (digits.length >= 1 && Number(digits.slice(0, 1)) > 2) return false;
        if (digits.length >= 2 && Number(digits.slice(0, 2)) > 23) return false;
        if (digits.length >= 3 && Number(digits.slice(2, 3)) > 5) return false;
        if (digits.length >= 4 && Number(digits.slice(2, 4)) > 59) return false;
        if (digits.length >= 5 && Number(digits.slice(4, 5)) > 5) return false;
        if (digits.length === 6 && Number(digits.slice(4, 6)) > 59) return false;
        return true;
    }

    function maskedInputValue(value, previousValue, formatter, digitsAreAllowed) {
        const digits = String(value || '').replace(/\D/g, '').slice(0, formatter.maxDigits);
        if (!digitsAreAllowed(digits)) {
            return {
                value: previousValue || '',
                rejected: true,
            };
        }
        return {
            value: formatter.format(digits),
            rejected: false,
        };
    }

    function maskDateValue(value, previousValue) {
        return maskedInputValue(value, previousValue, {
            maxDigits: 8,
            format: digits => formatDateInput(digits),
        }, dateDigitsAreAllowed);
    }

    function maskTimeValue(value, previousValue) {
        return maskedInputValue(value, previousValue, {
            maxDigits: 6,
            format: digits => formatTimeInput(digits),
        }, timeDigitsAreAllowed);
    }

    function countDigitsBeforeCursor(value, cursor) {
        return String(value || '').slice(0, cursor).replace(/\D/g, '').length;
    }

    function cursorAfterDigitCount(value, digitCount) {
        if (digitCount <= 0) return 0;
        let seen = 0;
        for (let index = 0; index < value.length; index += 1) {
            if (/\d/.test(value[index])) seen += 1;
            if (seen >= digitCount) return index + 1;
        }
        return value.length;
    }

    function applyMaskedInput(input, mask) {
        const rawValue = input.value;
        const previousValue = input.dataset.maskValue || '';
        const cursor = typeof input.selectionStart === 'number'
            ? input.selectionStart
            : rawValue.length;
        const digitsBeforeCursor = countDigitsBeforeCursor(rawValue, cursor);
        const result = mask(rawValue, previousValue);
        input.value = result.value;
        input.dataset.maskValue = result.value;
        if (typeof input.setSelectionRange === 'function') {
            const nextCursor = result.rejected
                ? Math.min(cursor, result.value.length)
                : cursorAfterDigitCount(result.value, digitsBeforeCursor);
            input.setSelectionRange(nextCursor, nextCursor);
        }
        return result;
    }

    function isCompleteDate(value) {
        return /^\d{4}-\d{2}-\d{2}$/.test(value) && Boolean(normalizeDateText(value));
    }

    function isCompleteTime(value) {
        return /^\d{2}:\d{2}:\d{2}$/.test(value) && Boolean(normalizeTime(value));
    }

    function bindTimeRangeFilter(root, options = {}) {
        const scope = root || document;
        const prefix = options.prefix || 'filter-create';
        const elements = rangeElements(scope, prefix);
        if (!elements.trigger || !elements.panel || !elements.startDate || !elements.endDate
            || !elements.startTimeDisplay || !elements.endTimeDisplay
            || !elements.directStartDate || !elements.directEndDate
            || !elements.startTime || !elements.endTime || !elements.calendarDays
            || !elements.calendarTitle || !elements.calendarStatus
            || !elements.panelError || !elements.panelReset || !elements.panelConfirm) {
            return null;
        }
        if (elements.trigger.dataset.timeRangeBound === '1') {
            return elements.trigger._timeRangeController || elements;
        }

        elements.trigger.dataset.timeRangeBound = '1';

        const initialDate = dateParts(elements.startDate.value)
            || dateParts(elements.directStartDate.value)
            || dateParts(new Date().toISOString().slice(0, 10));
        const calendarState = {
            year: initialDate.year,
            month: initialDate.month,
            startDate: normalizeDateText(elements.startDate.value || elements.directStartDate.value),
            endDate: normalizeDateText(elements.endDate.value || elements.directEndDate.value),
            selectionClicks: [],
        };
        let inputError = '';
        let committedRange = {
            startDate: calendarState.startDate,
            endDate: calendarState.endDate,
            startTime: elements.startTime.value || '',
            endTime: elements.endTime.value || '',
        };
        const inputMasks = new Map();
        const useIMask = options.inputMask === 'imask' && typeof global.IMask === 'function';
        let syncingMaskValue = false;

        const setControlledInputValue = (input, value) => {
            const nextValue = value || '';
            const mask = inputMasks.get(input);
            if (!mask) {
                input.value = nextValue;
                return;
            }

            const wasSyncing = syncingMaskValue;
            syncingMaskValue = true;
            mask.value = nextValue;
            syncingMaskValue = wasSyncing;
        };

        const readRange = () => Object.assign({}, committedRange);

        const readDraftRange = () => ({
            startDate: elements.directStartDate.value || '',
            endDate: elements.directEndDate.value || '',
            startTime: elements.startTime.value || '',
            endTime: elements.endTime.value || '',
        });

        const setInputError = message => {
            inputError = message || '';
            elements.panelError.textContent = inputError;
            elements.panelError.hidden = !inputError;
            elements.panelConfirm.disabled = Boolean(inputError);
        };

        const syncDefaults = () => {
            if (elements.directStartDate.value && !elements.startTime.value) {
                setControlledInputValue(elements.startTime, '00:00:00');
            }
            if (elements.directEndDate.value && !elements.endTime.value) {
                setControlledInputValue(elements.endTime, '23:59:59');
            }
        };

        const syncCalendarStatus = message => {
            if (message) {
                elements.calendarStatus.textContent = message;
                return;
            }
            if (calendarState.selectionClicks.length === 1 && calendarState.selectionClicks[0]) {
                elements.calendarStatus.textContent = `已选择开始日期 ${calendarState.startDate}，第二次点击选择结束日期`;
            } else if (calendarState.selectionClicks.length === 2) {
                elements.calendarStatus.textContent = `已选择 ${calendarState.startDate} 至 ${calendarState.endDate}`;
            } else {
                elements.calendarStatus.textContent = '第一次点击选择开始日期';
            }
        };

        const syncMaskSnapshots = () => {
            elements.directStartDate.dataset.maskValue = elements.directStartDate.value || '';
            elements.directEndDate.dataset.maskValue = elements.directEndDate.value || '';
            elements.startTime.dataset.maskValue = elements.startTime.value || '';
            elements.endTime.dataset.maskValue = elements.endTime.value || '';
        };

        const syncDateDisplays = (startDate, endDate, options = {}) => {
            const start = normalizeDateText(startDate);
            const end = normalizeDateText(endDate);
            if (options.syncDirectInputs !== false) {
                setControlledInputValue(elements.directStartDate, start);
                setControlledInputValue(elements.directEndDate, end);
            }
            elements.directStartDate.setCustomValidity('');
            elements.directEndDate.setCustomValidity('');
            elements.directStartDate.removeAttribute('aria-invalid');
            elements.directEndDate.removeAttribute('aria-invalid');
            setInputError('');
            if (!start) setControlledInputValue(elements.startTime, '');
            if (!end) setControlledInputValue(elements.endTime, '');
            calendarState.startDate = start;
            calendarState.endDate = end;
            calendarState.selectionClicks = Array.isArray(options.calendarClicks)
                ? options.calendarClicks.slice(0, 2)
                : (start && !end ? [start] : []);
            syncDefaults();
            syncMaskSnapshots();
        };

        const syncTrigger = () => {
            if (options.iconTrigger) {
                const label = committedRange.startDate && committedRange.endDate
                    ? '修改起止日期'
                    : '配置起止日期';
                elements.trigger.textContent = '';
                elements.trigger.setAttribute('aria-label', label);
                elements.trigger.setAttribute('title', label);
                return;
            }
            if (committedRange.startDate && committedRange.endDate) {
                elements.trigger.textContent = '修改日期';
            } else {
                elements.trigger.textContent = '选择日期';
            }
        };

        const clearInputValidation = () => {
            [
                elements.directStartDate,
                elements.directEndDate,
                elements.startTime,
                elements.endTime,
            ].forEach(input => {
                input.setCustomValidity('');
                input.removeAttribute('aria-invalid');
            });
        };

        const commitRange = range => {
            const startDate = normalizeDateText(range.startDate);
            const endDate = normalizeDateText(range.endDate);
            committedRange = {
                startDate,
                endDate,
                startTime: startDate ? normalizeTime(range.startTime) : '',
                endTime: endDate ? normalizeTime(range.endTime) : '',
            };
            elements.startDate.value = committedRange.startDate;
            elements.endDate.value = committedRange.endDate;
            elements.startTimeDisplay.value = committedRange.startTime;
            elements.endTimeDisplay.value = committedRange.endTime;
            syncTrigger();
        };

        const loadCommittedIntoDraft = () => {
            setControlledInputValue(elements.directStartDate, committedRange.startDate);
            setControlledInputValue(elements.directEndDate, committedRange.endDate);
            setControlledInputValue(elements.startTime, committedRange.startTime);
            setControlledInputValue(elements.endTime, committedRange.endTime);
            calendarState.startDate = committedRange.startDate;
            calendarState.endDate = committedRange.endDate;
            calendarState.selectionClicks = [];
            const selectedDate = dateParts(committedRange.startDate);
            if (selectedDate) {
                calendarState.year = selectedDate.year;
                calendarState.month = selectedDate.month;
            }
            clearInputValidation();
            setInputError('');
            syncMaskSnapshots();
        };

        const setOpen = open => {
            elements.panel.hidden = !open;
            elements.trigger.setAttribute('aria-expanded', open ? 'true' : 'false');
            elements.host?.classList.toggle('is-open', open);
            if (open) {
                loadCommittedIntoDraft();
                syncCalendarStatus();
                renderCalendar();
            }
        };

        const renderCalendar = () => {
            const firstDay = new Date(calendarState.year, calendarState.month, 1).getDay();
            const daysInMonth = new Date(calendarState.year, calendarState.month + 1, 0).getDate();
            elements.calendarTitle.textContent = `${calendarState.year}年${calendarState.month + 1}月`;
            elements.calendarDays.innerHTML = '';

            for (let index = 0; index < firstDay; index += 1) {
                const blank = document.createElement('span');
                blank.className = 'filter-calendar-day is-empty';
                blank.setAttribute('aria-hidden', 'true');
                elements.calendarDays.appendChild(blank);
            }

            for (let day = 1; day <= daysInMonth; day += 1) {
                const dateText = formatCalendarDate(calendarState.year, calendarState.month, day);
                const dayButton = document.createElement('button');
                dayButton.type = 'button';
                dayButton.className = 'filter-calendar-day';
                dayButton.textContent = String(day);
                dayButton.dataset.date = dateText;
                dayButton.setAttribute('role', 'gridcell');
                dayButton.setAttribute('aria-label', dateText);
                const isStart = dateText === calendarState.startDate;
                const isEnd = dateText === calendarState.endDate;
                const isInRange = Boolean(calendarState.startDate && calendarState.endDate)
                    && dateText > calendarState.startDate
                    && dateText < calendarState.endDate;
                dayButton.setAttribute('aria-selected', isStart || isEnd ? 'true' : 'false');
                dayButton.classList.toggle('is-start', isStart);
                dayButton.classList.toggle('is-end', isEnd);
                dayButton.classList.toggle('is-in-range', isInRange);
                dayButton.addEventListener('click', function() {
                    const firstClick = calendarState.selectionClicks[0];
                    if (!firstClick) {
                        syncDateDisplays(dateText, '', { calendarClicks: [dateText] });
                        syncCalendarStatus();
                        renderCalendar();
                        return;
                    }

                    if (dateText < firstClick) {
                        setInputError('结束日期不能早于开始日期');
                        return;
                    }

                    syncDateDisplays(firstClick, dateText, { calendarClicks: [firstClick, dateText] });
                    syncCalendarStatus();
                    renderCalendar();
                });
                elements.calendarDays.appendChild(dayButton);
            }
        };

        elements.trigger.addEventListener('click', function() {
            const willOpen = elements.panel.hidden;
            setOpen(willOpen);
        });

        const clearDraft = () => {
            setControlledInputValue(elements.directStartDate, '');
            setControlledInputValue(elements.directEndDate, '');
            setControlledInputValue(elements.startTime, '');
            setControlledInputValue(elements.endTime, '');
            calendarState.startDate = '';
            calendarState.endDate = '';
            calendarState.selectionClicks = [];
            clearInputValidation();
            setInputError('');
            syncMaskSnapshots();
            syncCalendarStatus();
            renderCalendar();
        };

        const validateDraftRange = range => validate({
            startDate: range.startDate,
            endDate: range.endDate,
            startTime: range.startTime,
            endTime: range.endTime,
            status: 'all',
            protocolType: 'all',
            ownerKeyword: '',
            ownerUserId: null,
            ownerNote: '',
        });

        elements.panelReset.addEventListener('click', function() {
            clearDraft();
        });

        elements.panelConfirm.addEventListener('click', function() {
            const draftRange = readDraftRange();
            const validation = validateDraftRange(draftRange);
            if (!validation.valid) {
                setInputError(validation.message);
                return;
            }

            commitRange(draftRange);
            setInputError('');
            setOpen(false);
        });

        const syncDirectDates = (input, externalMask = false, showIncomplete = true) => {
            const result = externalMask
                ? { rejected: false }
                : applyMaskedInput(input, (value, previousValue) => {
                    return maskDateValue(value, previousValue);
                });
            const directDateInputs = [elements.directStartDate, elements.directEndDate];

            if (result.rejected) {
                input.setCustomValidity('日期只能输入有效的 YYYY-MM-DD');
                input.setAttribute('aria-invalid', 'true');
                setInputError('日期只能输入有效的 YYYY-MM-DD');
                return;
            }

            const startText = elements.directStartDate.value;
            const endText = elements.directEndDate.value;
            const start = startText ? normalizeDateText(startText) : '';
            const end = endText ? normalizeDateText(endText) : '';
            const hasInvalidDate = directDateInputs.some(input => input.value && !isCompleteDate(input.value));

            if (hasInvalidDate) {
                if (externalMask && !showIncomplete) {
                    directDateInputs.forEach(input => {
                        input.setCustomValidity('');
                        input.removeAttribute('aria-invalid');
                    });
                    setInputError('');
                    return;
                }
                directDateInputs.forEach(input => {
                    if (input.value && !isCompleteDate(input.value)) {
                        input.setCustomValidity('日期格式应为 YYYY-MM-DD，且必须是有效日期');
                        input.setAttribute('aria-invalid', 'true');
                    } else {
                        input.setCustomValidity('');
                        input.removeAttribute('aria-invalid');
                    }
                });
                setInputError('日期格式应为 YYYY-MM-DD，且必须是有效日期');
                return;
            }

            if (start && end && start > end) {
                elements.directStartDate.setCustomValidity('结束日期不能早于开始日期');
                elements.directEndDate.setCustomValidity('结束日期不能早于开始日期');
                elements.directStartDate.setAttribute('aria-invalid', 'true');
                elements.directEndDate.setAttribute('aria-invalid', 'true');
                setInputError('结束日期不能早于开始日期');
                return;
            }

            directDateInputs.forEach(input => {
                input.setCustomValidity('');
                input.removeAttribute('aria-invalid');
            });
            setInputError('');
            syncDateDisplays(start, end, { syncDirectInputs: false });
            renderCalendar();
        };

        const syncDirectTime = (input, externalMask = false, showIncomplete = true) => {
            const result = externalMask
                ? { rejected: false }
                : applyMaskedInput(input, (value, previousValue) => {
                    return maskTimeValue(value, previousValue);
                });
            const timeInputs = [elements.startTime, elements.endTime];

            if (result.rejected) {
                input.setCustomValidity('时间只能输入有效的 HH:MM:SS');
                input.setAttribute('aria-invalid', 'true');
                setInputError('时间只能输入有效的 HH:MM:SS');
                return;
            }

            const hasInvalidTime = timeInputs.some(timeInput => {
                return timeInput.value && !isCompleteTime(timeInput.value);
            });
            if (hasInvalidTime && externalMask && !showIncomplete) {
                timeInputs.forEach(timeInput => {
                    timeInput.setCustomValidity('');
                    timeInput.removeAttribute('aria-invalid');
                });
                setInputError('');
                return;
            }
            timeInputs.forEach(timeInput => {
                if (timeInput.value && !isCompleteTime(timeInput.value)) {
                    timeInput.setCustomValidity('时分秒格式应为 HH:MM:SS，且数值必须有效');
                    timeInput.setAttribute('aria-invalid', 'true');
                } else {
                    timeInput.setCustomValidity('');
                    timeInput.removeAttribute('aria-invalid');
                }
            });
            if (hasInvalidTime) {
                setInputError('时分秒格式应为 HH:MM:SS，且数值必须有效');
                return;
            }

            setInputError('');
        };

        const bindNativeInputMasks = () => {
            [elements.directStartDate, elements.directEndDate].forEach(input => {
                input.setAttribute('inputmode', 'numeric');
                input.setAttribute('maxlength', '10');
                input.setAttribute('pattern', '\\d{4}-\\d{2}-\\d{2}');
                input.addEventListener('input', function() {
                    syncDirectDates(input);
                });
                input.addEventListener('change', function() {
                    syncDirectDates(input);
                });
            });

            [elements.startTime, elements.endTime].forEach(input => {
                input.setAttribute('inputmode', 'numeric');
                input.setAttribute('maxlength', '8');
                input.setAttribute('pattern', '\\d{2}:\\d{2}:\\d{2}');
                input.addEventListener('input', function() {
                    syncDirectTime(input);
                });
                input.addEventListener('change', function() {
                    syncDirectTime(input);
                });
            });
        };

        const bindIMaskInputs = () => {
            const dateOptions = {
                mask: Date,
                pattern: 'Y-`m-`d',
                lazy: true,
                overwrite: true,
                autofix: true,
                blocks: {
                    Y: {
                        mask: global.IMask.MaskedRange,
                        from: 1900,
                        to: 9999,
                    },
                    m: {
                        mask: global.IMask.MaskedRange,
                        from: 1,
                        to: 12,
                        maxLength: 2,
                    },
                    d: {
                        mask: global.IMask.MaskedRange,
                        from: 1,
                        to: 31,
                        maxLength: 2,
                    },
                },
                format: date => formatCalendarDate(date.getFullYear(), date.getMonth(), date.getDate()),
                parse: value => {
                    const [year, month, day] = value.split('-').map(Number);
                    return new Date(year, month - 1, day);
                },
            };
            const timeOptions = {
                mask: 'HH:MM:SS',
                lazy: true,
                overwrite: true,
                autofix: true,
                blocks: {
                    HH: {
                        mask: global.IMask.MaskedRange,
                        from: 0,
                        to: 23,
                        maxLength: 2,
                    },
                    MM: {
                        mask: global.IMask.MaskedRange,
                        from: 0,
                        to: 59,
                        maxLength: 2,
                    },
                    SS: {
                        mask: global.IMask.MaskedRange,
                        from: 0,
                        to: 59,
                        maxLength: 2,
                    },
                },
            };

            [elements.directStartDate, elements.directEndDate].forEach(input => {
                const mask = global.IMask(input, dateOptions);
                inputMasks.set(input, mask);
                input.dataset.maskProvider = 'imask';
                mask.on('accept', function() {
                    if (!syncingMaskValue) syncDirectDates(input, true, false);
                });
                input.addEventListener('blur', function() {
                    syncDirectDates(input, true, true);
                });
            });
            [elements.startTime, elements.endTime].forEach(input => {
                const mask = global.IMask(input, timeOptions);
                inputMasks.set(input, mask);
                input.dataset.maskProvider = 'imask';
                mask.on('accept', function() {
                    if (!syncingMaskValue) syncDirectTime(input, true, false);
                });
                input.addEventListener('blur', function() {
                    syncDirectTime(input, true, true);
                });
            });
        };

        if (useIMask) {
            bindIMaskInputs();
        } else {
            bindNativeInputMasks();
        }

        if (elements.calendarPrev) {
            elements.calendarPrev.addEventListener('click', function() {
                calendarState.month -= 1;
                if (calendarState.month < 0) {
                    calendarState.month = 11;
                    calendarState.year -= 1;
                }
                renderCalendar();
            });
        }
        if (elements.calendarNext) {
            elements.calendarNext.addEventListener('click', function() {
                calendarState.month += 1;
                if (calendarState.month > 11) {
                    calendarState.month = 0;
                    calendarState.year += 1;
                }
                renderCalendar();
            });
        }

        document.addEventListener('click', function(event) {
            const eventPath = typeof event.composedPath === 'function' ? event.composedPath() : [];
            const clickedInside = elements.host && (eventPath.includes(elements.host)
                || elements.host.contains(event.target));
            if (elements.host && !clickedInside) setOpen(false);
        });

        const reset = () => {
            commitRange({
                startDate: '',
                endDate: '',
                startTime: '',
                endTime: '',
            });
            clearDraft();
            setOpen(false);
        };

        commitRange(committedRange);
        loadCommittedIntoDraft();
        renderCalendar();
        syncTrigger();

        const controller = Object.assign(elements, {
            read: readRange,
            reset,
            open: () => setOpen(true),
            close: () => setOpen(false),
            update: syncTrigger,
        });
        elements.trigger._timeRangeController = controller;
        return controller;
    }

    KitProxy.timeRangeFilter = {
        bind: bindTimeRangeFilter,
        parseDateRangeText,
        normalizeDateText,
        normalizeTime,
    };

    KitProxy.serviceFilters = {
        createState,
        readFromDOM,
        validate,
        hasActiveFilters,
        toRequest,
        describe,
    };
})(typeof window !== 'undefined' ? window : globalThis);

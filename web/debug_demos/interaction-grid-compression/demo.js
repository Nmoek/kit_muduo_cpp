(() => {
    'use strict';

    const oldList = document.querySelector('#old-list');
    const newList = document.querySelector('#new-list');
    const range = document.querySelector('#record-count');
    const rangeValue = document.querySelector('#record-count-value');
    const oldCase = document.querySelector('#old-case');
    const oldStatus = document.querySelector('#old-status');
    const newStatus = document.querySelector('#new-status');
    const oldExplanation = document.querySelector('#old-explanation');
    const oldHeightLabel = document.querySelector('#old-height-label');
    const newHeightLabel = document.querySelector('#new-height-label');

    const records = Array.from({ length: 20 }, (_, index) => ({
        scope: index < 10 ? 'project' : 'protocol',
        number: index < 10 ? 10485 - index : 2885 - (index - 10),
    }));

    function makeCard(record, index) {
        const card = document.createElement('div');
        card.className = `record-card ${record.scope}`;
        card.dataset.index = String(index);
        if (record.scope === 'project') {
            card.innerHTML = `<strong class="card-title">没有协议项可以处理请求</strong><span class="card-sub"><span>项目 Notice</span><span>seq=${record.number}</span><span>empty · 0B</span></span>`;
        } else {
            card.innerHTML = `<strong class="card-title">FUNC H${String(record.number).slice(-4)}</strong><span class="card-sub"><span>协议项</span><span>seq=${record.number}</span><span>binary · 4B</span></span>`;
        }
        return card;
    }

    records.forEach((record, index) => {
        oldList.appendChild(makeCard(record, index));
        newList.appendChild(makeCard(record, index));
    });

    function visibleCards(list) {
        return Array.from(list.children).filter(card => card.classList.contains('is-visible'));
    }

    function updateMetrics() {
        const oldCards = visibleCards(oldList);
        const newCards = visibleCards(newList);
        const oldHeight = oldCards[0] ? oldCards[0].getBoundingClientRect().height : 0;
        const newHeight = newCards[0] ? newCards[0].getBoundingClientRect().height : 0;
        document.querySelector('#old-card-height').textContent = oldCards.length ? `${Math.round(oldHeight)}px` : '-';
        document.querySelector('#new-card-height').textContent = newCards.length ? `${Math.round(newHeight)}px` : '-';
        document.querySelector('#old-overflow').textContent = oldCards.length ? `${oldList.scrollHeight}px` : '-';
        document.querySelector('#new-overflow').textContent = newCards.length ? `${newList.scrollHeight}px，可滚动` : '-';
    }

    function render() {
        const count = Number(range.value);
        [oldList, newList].forEach(list => {
            Array.from(list.children).forEach((card, index) => {
                card.classList.toggle('is-visible', index < count);
            });
        });
        rangeValue.textContent = `${count} / 20`;
        const hasProjects = count > 0;
        const hasProtocols = count > 10;
        oldCase.classList.toggle('is-compressed', hasProtocols);
        oldStatus.textContent = !hasProjects ? '等待记录' : hasProtocols ? '卡片被压扁' : '暂时正常';
        oldStatus.className = `case-status ${hasProtocols ? 'bad' : 'bad'}`;
        newStatus.textContent = !hasProjects ? '等待记录' : '尺寸稳定';
        newStatus.className = `case-status ${hasProjects ? 'good' : 'good'}`;
        oldHeightLabel.textContent = !hasProjects ? '卡片尚未进入' : hasProtocols ? '显式行被压缩' : '10 个显式行';
        newHeightLabel.textContent = hasProjects ? '超出视口后滚动' : '卡片尚未进入';
        oldExplanation.textContent = !hasProjects
            ? '点击播放，观察第 11 条记录进入后显式行开始让位。'
            : !hasProtocols
                ? '目前只有 10 条记录，刚好填满 10 个显式行，所以看起来正常。'
                : '第 11 条记录起进入隐式行；浏览器压缩前 10 行，现场的 14px 就在这里产生。';
        document.querySelector('#step-1').classList.toggle('is-active', hasProjects && !hasProtocols);
        document.querySelector('#step-2').classList.toggle('is-active', hasProtocols);
        document.querySelector('#step-3').classList.toggle('is-active', hasProjects);
        requestAnimationFrame(updateMetrics);
    }

    let timer = null;
    function pause() {
        if (timer) window.clearInterval(timer);
        timer = null;
    }
    function play() {
        pause();
        if (Number(range.value) >= 20) range.value = '0';
        timer = window.setInterval(() => {
            const next = Number(range.value) + 1;
            range.value = String(next);
            render();
            if (next >= 20) pause();
        }, 300);
    }

    document.querySelector('#play').addEventListener('click', play);
    document.querySelector('#pause').addEventListener('click', pause);
    document.querySelector('#reset').addEventListener('click', () => { pause(); range.value = '0'; render(); });
    range.addEventListener('input', () => { pause(); render(); });
    document.querySelector('#show-guides').addEventListener('change', event => {
        oldCase.classList.toggle('show-guides', event.target.checked);
    });
    oldCase.classList.add('show-guides');
    render();
})();

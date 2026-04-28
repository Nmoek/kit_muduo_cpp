(function initKitProxyNamespace(global) {
    // V1 仍然使用经典 script 顺序加载，统一命名空间用于减少新增全局变量。
    global.KitProxy = global.KitProxy || {};
})(typeof window !== 'undefined' ? window : globalThis);

(function initKitProxyConstants(global) {
    const KitProxy = global.KitProxy || (global.KitProxy = {});

    // 这些数字和后端 payload 语义绑定，改值会影响接口兼容性。
    const ProjectMode = Object.freeze({
        SERVER: 1,
        CLIENT: 2,
    });

    const ProjectModeStr = Object.freeze({
        1: '服务器模式',
        2: '客户端模式',
    });

    const ProtocolType = Object.freeze({
        HTTP: 1,
        CUSTOM_TCP: 2,
        HTTPS: 3,
    });

    const ProtocolTypeStr = Object.freeze({
        1: 'HTTP',
        2: '自定义TCP',
        3: 'HTTPS',
    });

    const PatternType = Object.freeze({
        STANDARD: 0,
        BODY_LENGTH_DEP: 1,
        TOTAL_LENGTH_DEP: 2,
        NO_LENGTH_DEP: 3,
    });

    const PatternTypeStr = Object.freeze({
        0: '标准格式',
        1: 'Body长度依赖',
        2: '总长度依赖',
        3: '无长度依赖',
    });

    const PatternFieldType = Object.freeze({
        INT8: 1,
        UINT8: 2,
        INT16: 3,
        UINT16: 4,
        INT32: 5,
        UINT32: 6,
        INT64: 7,
        UINT64: 8,
        FLOAT: 9,
        DOUBLE: 10,
        STR: 11,
    });

    const PatternFieldTypeStr = Object.freeze({
        1: 'INT8',
        2: 'UINT8',
        3: 'INT16',
        4: 'UINT16',
        5: 'INT32',
        6: 'UINT32',
        7: 'INT64',
        8: 'UINT64',
        9: 'FLOAT',
        10: 'DOUBLE',
        11: 'STR',
    });

    KitProxy.constants = {
        ProjectMode,
        ProjectModeStr,
        ProtocolType,
        ProtocolTypeStr,
        PatternType,
        PatternTypeStr,
        PatternFieldType,
        PatternFieldTypeStr,
        PatternFiledTypeStr: PatternFieldTypeStr,
    };

    // 旧代码仍通过全局变量访问常量；V1 先保留别名，V2 再迁移到模块导入。
    global.ProjectMode = ProjectMode;
    global.ProjectModeStr = ProjectModeStr;
    global.ProtocolType = ProtocolType;
    global.ProtocolTypeStr = ProtocolTypeStr;
    global.PatternType = PatternType;
    global.PatternTypeStr = PatternTypeStr;
    global.PatternFieldType = PatternFieldType;
    global.PatternFieldTypeStr = PatternFieldTypeStr;
    global.PatternFiledTypeStr = PatternFieldTypeStr;
})(typeof window !== 'undefined' ? window : globalThis);

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gmock_gen.py - 自动生成C++接口类的Google Mock测试桩
兼容Python 3.5+版本
"""

import re
import sys
import os


class MethodInfo:
    def __init__(self):
        self.return_type = ""       # 返回类型
        self.name = ""              # 方法名
        self.params = ""            # 参数列表
        self.is_const = False       # 是否是const方法


class ClassInfo:
    def __init__(self):
        self.name = ""              # 类名
        self.base_class = ""        # 基类名
        self.ctor_defaults = []     # 构造函数默认参数列表（每个构造函数一组）
        self.methods = []           # 方法列表
        self.namespace = ""         # 命名空间
        self.extra_includes = []    # 额外依赖的头文件


def _remove_comments(content):
    """移除 C++ 注释，保留行结构"""
    # 先移除块注释
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
    # 移除行注释（保留换行符，避免行合并）
    content = re.sub(r'//[^\n]*', '', content)
    return content


def _extract_class_body(content, start_pos):
    """从 content[start_pos] 开始，用大括号计数提取整个类体
    start_pos 已经位于类开括号 '{' 之后，所以 brace_count 从 1 开始"""
    brace_count = 1
    i = start_pos
    while i < len(content):
        ch = content[i]
        if ch == '{':
            brace_count += 1
        elif ch == '}':
            brace_count -= 1
            if brace_count == 0:
                return content[start_pos:i], i + 1
        i += 1
    return content[start_pos:], len(content)


def _find_class_ctors(content, class_name):
    """在类体中查找类的构造函数，返回默认值列表（每个构造函数一组默认参数）
    匹配 ClassName(params...)...{ 或 ClassName(params...);"""
    pattern = re.compile(
        r'\b{}\s*\(([^)]*)\)'.format(re.escape(class_name))
    )
    ctors = []
    for m in pattern.finditer(content):
        params = m.group(1).strip()
        if params:
            param_list = _split_params(params)
            defaults = [_param_default(p) for p in param_list]
            ctors.append(', '.join(defaults))
        else:
            ctors.append("")  # 无参构造函数
    return ctors


def _split_params(params_str):
    """分割函数参数列表，处理模板中的逗号"""
    parts = []
    depth = 0
    current = []
    for ch in params_str:
        if ch in '<(':
            depth += 1
        elif ch in '>)':
            depth -= 1
        elif ch == ',' and depth == 0:
            parts.append(''.join(current).strip())
            current = []
            continue
        current.append(ch)
    if current:
        parts.append(''.join(current).strip())
    return [p for p in parts if p]


def _param_default(param_str):
    """为单个参数生成默认值"""
    # 去除参数名，保留类型
    p = param_str.strip()
    # 指针/引用类型用 nullptr
    if '*' in p or '::Ptr' in p or 'shared_ptr' in p or 'unique_ptr' in p:
        return 'nullptr'
    # 引用类型比较棘手，先给个 {} 占位
    if '&' in p:
        return '{}'
    # 其他用值初始化
    return '{}'


def _make_include_path(input_file):
    """尝试从 input_file 推导相对于 include/ 的路径"""
    # 统一处理绝对路径和相对路径中的 include/ 目录
    path = input_file
    for sep in ('/include/', 'include/'):
        if sep in path:
            idx = path.index(sep) + len(sep)
            return path[idx:]
    return os.path.basename(input_file)


def parse_header(file_path):
    """解析C++头文件，提取所有接口类及其纯虚方法"""
    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()

    content = _remove_comments(content)

    classes = []
    namespace = ""

    # 提取命名空间
    ns_match = re.search(r'namespace\s+(\w+)\s*\{', content)
    if ns_match:
        namespace = ns_match.group(1)

    # 查找所有类定义: class ClassName [: [public|private|protected] BaseClass] {
    class_pattern = re.compile(
        r'class\s+(\w+)\s*(?::\s*(?:public|private|protected)?\s*(\w+))?\s*\{'
    )

    for match in class_pattern.finditer(content):
        class_info = ClassInfo()
        class_info.name = match.group(1)
        class_info.base_class = match.group(2) or ""
        class_info.namespace = namespace

        # 用大括号计数提取完整类体
        class_body, _ = _extract_class_body(content, match.end())

        # 查找类自身的构造函数（用于生成mock的委托构造）
        class_info.ctor_defaults = _find_class_ctors(class_body, class_info.name)

        # 匹配纯虚方法: virtual ReturnType methodName(params) [const] = 0;
        # 注意: 返回类型中可能有模板<>、命名空间::、指针*、引用&
        method_pattern = re.compile(
            r'virtual\s+'
            r'(.+?)'            # 返回类型 (non-greedy)
            r'\s+'
            r'(\w+)'            # 方法名
            r'\s*'
            r'\(([^)]*)\)'      # 参数列表 (不含嵌套括号，函数声明足够)
            r'\s*'
            r'(const\s*)?'      # 可选的 const
            r'=\s*0\s*;'        # 纯虚标识
        )

        for m in method_pattern.finditer(class_body):
            method = MethodInfo()
            method.return_type = m.group(1).strip()
            method.name = m.group(2)
            method.params = m.group(3).strip()
            method.is_const = bool(m.group(4) and m.group(4).strip())
            class_info.methods.append(method)

        if class_info.methods:
            classes.append(class_info)

    return classes


def _strip_interface_suffix(name):
    """去除接口类名的常见后缀（Interface, Base, Abstract 等）"""
    for suffix in ('Interface', 'Base', 'Abstract'):
        if name.endswith(suffix) and len(name) > len(suffix):
            return name[:-len(suffix)]
    return name


def generate_mock(class_info, input_file):
    """生成Mock类代码"""
    mock_name = "Mock{}".format(_strip_interface_suffix(class_info.name))

    lines = [
        "#pragma once",
        "",
        "#include <gmock/gmock.h>",
        '#include "{}"'.format(_make_include_path(input_file)),
        "",
    ]

    # 命名空间开始
    if class_info.namespace:
        lines.append("namespace {} {{".format(class_info.namespace))
        lines.append("")

    # 类定义 — Mock 始终继承被mock的接口类
    lines.append("class {} : public {} {{".format(mock_name, class_info.name))

    lines.append("public:")

    # 构造函数 — 如果被mock的类有带参构造函数，生成委托调用
    if class_info.ctor_defaults:
        for i, defaults in enumerate(class_info.ctor_defaults):
            if defaults:
                lines.append("    {}()".format(mock_name))
                lines.append("        : {}({}) {{}}".format(class_info.name, defaults))
            else:
                lines.append("    {}() = default;".format(mock_name))
            break  # 只生成第一个构造函数（多个构造函数需手动处理）
    else:
        lines.append("    {}() = default;".format(mock_name))

    lines.append("    virtual ~{}() = default;".format(mock_name))
    lines.append("")

    # Mock方法
    for method in class_info.methods:
        const_str = " const" if method.is_const else ""
        lines.append("    MOCK_METHOD({}, {}, ({}){}, (override));".format(
            method.return_type, method.name, method.params, const_str))

    lines.append("};")

    # 命名空间结束
    if class_info.namespace:
        lines.append("")
        lines.append("}} // namespace {}".format(class_info.namespace))

    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python gmock_gen.py <header_file> [output_file]")
        print("Example: python gmock_gen.py include/work/service/svc_project.h "
              "include/work/service/mock/svc_project_mock.h")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else "{}_mock.h".format(
        os.path.splitext(input_file)[0])

    # 确保输出目录存在
    output_dir = os.path.dirname(output_file)
    if output_dir and not os.path.exists(output_dir):
        try:
            os.makedirs(output_dir)
        except OSError as e:
            print("Error: Failed to create directory {}: {}".format(output_dir, e))
            sys.exit(1)

    if not os.path.exists(input_file):
        print("Error: Input file not found: {}".format(input_file))
        sys.exit(1)

    classes = parse_header(input_file)

    if not classes:
        print("No interface class (with = 0 methods) found in {}".format(input_file))
        sys.exit(1)

    try:
        with open(output_file, 'w', encoding='utf-8') as f:
            for class_info in classes:
                f.write(generate_mock(class_info, input_file))

        class_names = [c.name for c in classes]
        print("Successfully generated {} -> {} (classes: {})".format(
            input_file, output_file, ", ".join(class_names)))
    except IOError as e:
        print("Error: Failed to write to {}: {}".format(output_file, e))
        sys.exit(1)

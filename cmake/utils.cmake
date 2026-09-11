# utils.cmake

#重新定义当前目标的源文件的__FILE__宏
function(redefine_file_macro targetname)
    #获取当前目标的所有源文件
    get_target_property(source_files "${targetname}" SOURCES)
    #遍历源文件
    foreach(sourcefile ${source_files})
        #获取当前源文件的编译参数
        get_property(defs SOURCE "${sourcefile}"
            PROPERTY COMPILE_DEFINITIONS)
        #获取当前文件的绝对路径
        get_filename_component(abs_path "${sourcefile}" ABSOLUTE)

        #将绝对路径中的项目路径替换成空,得到源文件相对于项目路径的相对路径
        file(RELATIVE_PATH relpath "${PROJECT_SOURCE_DIR}" "${abs_path}")

        #将我们要加的编译参数(__FILE__定义)添加到原来的编译参数里面
        list(APPEND defs "__FILE__=\"${relpath}\"")
        #重新设置源文件的编译参数
        set_property(
            SOURCE "${sourcefile}"
            PROPERTY COMPILE_DEFINITIONS ${defs}
            )
    endforeach()
endfunction()


# 查找 vcpkg CONFIG 包，并打印包信息。
#
# 用法：
# find_kit_package(
#     <包名>
#     [VERSION <最低版本>]
#     [TARGET_VAR <返回 target 的变量名>]
#     TARGETS <全部必需的 imported target>...
# )
#
# TARGETS 不是候选列表：每个 target 都必须由包导出。验证全部通过后，
# 函数才会通过 TARGET_VAR 将原始列表完整返回给调用方。
#
# 示例：
# find_kit_package(
#     zstd
#     VERSION 1.5.7
#     TARGET_VAR KIT_ZSTD_TARGET
#     TARGETS
#         zstd::libzstd
# )
function(find_kit_package PACKAGE_NAME)
    # 解析函数参数。
    set(_options)
    set(_one_value_args VERSION TARGET_VAR)
    set(_multi_value_args TARGETS)

    cmake_parse_arguments(
        KIT_PACKAGE
        "${_options}"
        "${_one_value_args}"
        "${_multi_value_args}"
        ${ARGN}
    )

    # 防止调用时拼写了未知参数。
    if(KIT_PACKAGE_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "find_kit_package(${PACKAGE_NAME}) received unknown arguments: "
            "${KIT_PACKAGE_UNPARSED_ARGUMENTS}")
    endif()

    if(NOT KIT_PACKAGE_TARGETS)
        message(FATAL_ERROR
            "find_kit_package(${PACKAGE_NAME}) requires at least one TARGETS entry")
    endif()

    # 强制使用 CONFIG 模式。
    #
    # 这样会优先查找 vcpkg 安装生成的：
    #
    #   <package>Config.cmake
    #
    # 而不是误用系统中的 Find<Package>.cmake。
    if(KIT_PACKAGE_VERSION)
        find_package(
            "${PACKAGE_NAME}"
            "${KIT_PACKAGE_VERSION}"
            CONFIG
            REQUIRED)
    else()
        find_package(
            "${PACKAGE_NAME}"
            CONFIG
            REQUIRED)
    endif()

    # 不同包的版本变量大小写可能不同。
    #
    # 例如可能是：
    #
    #   zstd_VERSION
    #   ZSTD_VERSION
    #   zstd_VERSION_STRING
    #
    # 因此依次尝试常见变量名。
    string(TOLOWER "${PACKAGE_NAME}" _package_lower)
    string(TOUPPER "${PACKAGE_NAME}" _package_upper)

    set(_package_version "")

    foreach(_version_var IN ITEMS
        "${PACKAGE_NAME}_VERSION"
        "${_package_lower}_VERSION"
        "${_package_upper}_VERSION"
        "${PACKAGE_NAME}_VERSION_STRING"
        "${_package_lower}_VERSION_STRING"
        "${_package_upper}_VERSION_STRING"
    )
        if(DEFINED ${_version_var})
            if(NOT "${${_version_var}}" STREQUAL "")
                set(_package_version "${${_version_var}}")
                break()
            endif()
        endif()
    endforeach()

    if(_package_version STREQUAL "")
        set(_package_version "unknown")
    endif()

    # 获取 CONFIG 包实际所在目录。
    #
    # 对 zstd 来说，通常是：
    #
    #   .../share/zstd
    #
    set(_package_config_dir "")

    foreach(_config_dir_var IN ITEMS
        "${PACKAGE_NAME}_DIR"
        "${_package_lower}_DIR"
        "${_package_upper}_DIR"
    )
        if(DEFINED ${_config_dir_var})
            if(NOT "${${_config_dir_var}}" STREQUAL "")
                set(_package_config_dir "${${_config_dir_var}}")
                break()
            endif()
        endif()
    endforeach()

    if(_package_config_dir STREQUAL "")
        set(_package_config_dir "unknown")
    endif()

    # TARGETS 表示调用方后续需要链接的完整 target 集合，不是候选集合。
    # 逐项验证并收集所有缺失项，便于一次配置就看到完整错误信息。
    set(_missing_targets "")

    foreach(_required_target IN LISTS KIT_PACKAGE_TARGETS)
        if(NOT TARGET "${_required_target}")
            list(APPEND _missing_targets "${_required_target}")
        endif()
    endforeach()

    if(_missing_targets)
        string(JOIN ", " _missing_target_text ${_missing_targets})
        message(FATAL_ERROR
            "Package '${PACKAGE_NAME}' was found, but required imported "
            "target(s) are missing: ${_missing_target_text}")
    endif()

    # 打印统一的包信息。
    message(STATUS "Found ${PACKAGE_NAME}: ${_package_version}")
    message(STATUS "  config directory: ${_package_config_dir}")

    if(DEFINED VCPKG_TARGET_TRIPLET)
        message(STATUS "  vcpkg triplet: ${VCPKG_TARGET_TRIPLET}")
    endif()

    string(JOIN ", " _target_text ${KIT_PACKAGE_TARGETS})
    message(STATUS "  required targets: ${_target_text}")

    # 验证全部通过后原样返回调用方传入的完整列表。function() 有自己的
    # 作用域，因此必须通过 PARENT_SCOPE 写回调用方作用域。
    if(KIT_PACKAGE_TARGET_VAR)
        set(
            "${KIT_PACKAGE_TARGET_VAR}"
            "${KIT_PACKAGE_TARGETS}"
            PARENT_SCOPE
        )
    endif()
endfunction()

/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 静态页面实例注册表接口
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-06-24
 */
#ifndef SCREEN_PAGE_REGISTRY_H
#define SCREEN_PAGE_REGISTRY_H

#include <cstddef>

#include "core/page.h"

namespace SCREEN {

/**
 * @brief 固定页面注册表。
 *
 * 页面对象由注册表实现文件静态持有，避免动态分配并解除 UIManager 对具体页面类型的依赖。
 */
struct PageRegistry {
    Page* const* pages = nullptr; // 页面指针数组
    size_t       count = 0;       // 页面数量
};

/**
 * @brief 获取按默认翻页顺序排列的页面注册表。
 * @return 页面注册表只读视图。
 */
PageRegistry get_page_registry();

} // namespace SCREEN

#endif // SCREEN_PAGE_REGISTRY_H

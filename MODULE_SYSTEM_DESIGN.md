# 模块系统设计

## 目标

1. **配置隔离**：模块私有配置按 `[模块名]` 分区，不互相覆盖
2. **统一接口**：`ModuleDef` + `g_modules[]` 数组，菜单注册和命令分发全部自动化
3. **开发模板**：新增模块 = 写一个 `.cpp` + 在 `g_modules[]` 加一行 + CMake 加一行

---

## 1. 配置系统

### INI 分区约定

```
[Database]          ← 全局共享
Server=...
InitialDatabase=...
User=...
Password=...

[UI]                ← 全局共享
FontSize=9

[Query]             ← 查询模块私有
SplitterX=500

[Blood]             ← 输血模块私有
SplitterX=300

[RegularReport]     ← 常规报告模块私有
SplitterX=900
```

每个模块的 `ModuleDef.name` 即 section 名，私有 key 只在对应 section 下读写。

### API

```cpp
// app_settings_io.h

namespace search {

// 全局配置（现有，不变）
AppSettings load_settings(const std::filesystem::path& ini_path);
bool save_settings(const std::filesystem::path& ini_path, const AppSettings& settings);

// 模块私有配置（新增）
void save_module_int(const wchar_t* module, const wchar_t* key, int value);
void save_module_str(const wchar_t* module, const wchar_t* key, const std::wstring& value);
int  load_module_int(const wchar_t* module, const wchar_t* key, int fallback);
std::wstring load_module_str(const wchar_t* module, const wchar_t* key, const wchar_t* fallback);

}
```

内部统一用 `default_ini_path()`，section 参数即 `module`。实现就是 `WritePrivateProfileStringW` / `GetPrivateProfileIntW` 的简单封装。

---

## 2. 模块接口

### ModuleDef

```cpp
// src/module_registry.h
#pragma once
#ifdef _WIN32
#include "app_settings.h"
#include <windows.h>

struct ModuleContext {
    HWND mdiClient;
    HINSTANCE instance;
    HFONT uiFont;
    search::DbSettings dbSettings;
    int fontSize;
};

struct ModuleDef {
    const wchar_t* name;          // 模块标识 + INI section 名
    const wchar_t* menuParent;    // 所属菜单
    const wchar_t* menuLabel;     // 菜单项文字
    int menuId;
    HWND (*create)(const ModuleContext& ctx);
};
#endif
```

`ModuleContext` 打包所有共享资源，一次传完。模块内部从中取自己需要的字段。后续扩展只需在 `ModuleContext` 加字段，不用改 `create` 签名。

### 模块实现模板

```cpp
// src/xxx_module.cpp
#include "module_registry.h"
#include "resource.h"

namespace {

constexpr const wchar_t* WND_CLASS  = L"XxxModuleChild";
constexpr const wchar_t* PROP_STATE = L"XxxSt";

struct XxxState {
    ModuleContext ctx;
    // 模块私有字段...
};

XxxState* g_pending = nullptr;  // WM_CREATE 前传递状态的临时通道

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<XxxState*>(GetPropW(hwnd, PROP_STATE));

    switch (msg) {
        case WM_CREATE: {
            st = g_pending; g_pending = nullptr;
            SetPropW(hwnd, PROP_STATE, reinterpret_cast<HANDLE>(st));
            // 1. 创建控件
            // 2. 从 st->ctx 取共享资源
            // 3. 从 INI 读私有配置：search::load_module_int(L"Xxx", L"Key", 0)
            return 0;
        }
        case WM_SIZE:
            // 布局逻辑，必须 return 0（不落入 DefMDIChildProcW）
            return 0;
        case WM_DESTROY:
            RemovePropW(hwnd, PROP_STATE);
            delete st;
            break;  // 落入 DefMDIChildProcW 完成 MDI 清理
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_xxx_module(const ModuleContext& ctx) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = wndProc;
        wc.hInstance     = ctx.instance;
        wc.hIcon         = LoadIconW(ctx.instance, MAKEINTRESOURCEW(IDI_APP));
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = WND_CLASS;
        RegisterClassExW(&wc);
        registered = true;
    }

    auto* st = new XxxState;
    st->ctx = ctx;

    MDICREATESTRUCTW mcs{};
    mcs.szClass = WND_CLASS;
    mcs.szTitle = L"模块标题";
    mcs.hOwner  = ctx.instance;
    mcs.x = mcs.y = mcs.cx = mcs.cy = CW_USEDEFAULT;

    g_pending = st;
    HWND child = reinterpret_cast<HWND>(
        SendMessageW(ctx.mdiClient, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&mcs)));
    SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    return child;
}
```

### 模板要点（必须遵守）

| 规则 | 说明 |
|------|------|
| 状态存储 | `SetProp`/`GetProp`，禁止 `GWLP_USERDATA` |
| WM_NCCREATE | 不拦截，交 `DefMDIChildProcW` |
| WM_NCDESTROY | 不拦截，交 `DefMDIChildProcW` |
| WM_SIZE | 必须 `return 0`，不可落入 `DefMDIChildProcW`（菜单栏残留） |
| 状态传递 | `g_pending` 临时指针 → `WM_CREATE` 取走 → `SetProp` 持久化 |
| 类注册 | `static bool` 守卫，仅注册一次 |

### 通用拖条控件

MDI 模块需要左右拖动分割区时，优先复用 `search_splitter.*`：

- `search::create_splitter(...)` 创建 `LISWorkbenchSplitter` 子控件。
- 拖动中向父窗口发送 `search::WM_SPLITTER_DRAG`，`wParam` 为父窗口客户区内的分割器 X 坐标，`lParam` 为拖条 HWND。
- 鼠标释放时发送 `search::WM_SPLITTER_RELEASED`，模块在释放消息中保存自己的 `SplitterX`。
- 每个模块仍负责自己的布局约束和 INI section，例如 `Query/SplitterX`、`RegularReport/SplitterX`。

### 分割器位置持久化（重要）

若模块包含可拖拽分割器，加载位置值后**不可在 `WM_CREATE` 的小窗口中传入 `layout_main_window`**（会被钳制）。应存入 `pendingSplitterX` 字段，在 `WM_SIZE` 中 `IsZoomed(hwnd)` 为真时才写入真值：

```cpp
case WM_CREATE: {
    st->pendingSplitterX = search::load_module_int(L"Xxx", L"SplitterX", 0);
    int initX = 0;
    layout_main_window(hwnd, ui, initX);  // 用临时变量，不碰 pending 值
    return 0;
}
case WM_SIZE: {
    if (st->pendingSplitterX > 0 && IsZoomed(hwnd)) {
        st->splitterX = st->pendingSplitterX;
        st->pendingSplitterX = 0;
    }
    int x = st->splitterX;
    layout_main_window(hwnd, ui, x);
    st->splitterX = x;
    return 0;
}
```

---

## 3. 主窗口注册表

### g_modules 数组

```cpp
// main_frame.cpp

#include "module_registry.h"
#include "query_module.h"     // HWND create_query_module(const ModuleContext&);
#include "blood_module.h"     // HWND create_blood_module(const ModuleContext&);
#include "settings_module.h"  // HWND create_settings_module(const ModuleContext&);

const ModuleDef g_modules[] = {
    { L"Query",    L"检验管理", L"检验结果查询(&Q)...", IDM_QUERY,    create_query_module    },
    { L"Blood",    L"检验管理", L"输血结果查询(&B)...", IDM_BLOOD,    create_blood_module     },
    { L"Settings", L"系统",     L"系统设置(&S)...",     IDM_SETTINGS, create_settings_module  },
};
constexpr int g_moduleCount = sizeof(g_modules) / sizeof(g_modules[0]);
```

当前 `Blood` 模块通过 `ModuleContext.dbSettings` 复用主程序数据库配置，只执行只读查询：

- 主表：`LS_XK_BloodRequestApply`
- 子表：`LS_XK_BloodRequestApplySon`，通过 `ApplyFormNO` 聚合申请成分
- 申请状态：按 `LS_XK_BloodRequestApply.ApplyForm_Statue` 的中文值（`未审核` / `已审核` / `已完结`）匹配

### 自动菜单注册

`setupMenus` 中遍历 `g_modules`，按 `menuParent` 去重创建弹出菜单，按数组顺序追加菜单项：

```cpp
HMENU setupMenus(HWND hwnd) {
    HMENU bar = CreateMenu();
    HMENU subMenus[8]{};    // 最多 8 个不同的 menuParent
    const wchar_t* subNames[8]{};
    int subCount = 0;

    for (int i = 0; i < g_moduleCount; i++) {
        const auto& m = g_modules[i];
        // 查找或创建 menuParent 对应的弹出菜单
        int idx = -1;
        for (int j = 0; j < subCount; j++) {
            if (wcscmp(subNames[j], m.menuParent) == 0) { idx = j; break; }
        }
        if (idx < 0) {
            subMenus[subCount] = CreatePopupMenu();
            subNames[subCount] = m.menuParent;
            AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(subMenus[subCount]), m.menuParent);
            idx = subCount++;
        }
        AppendMenuW(subMenus[idx], MF_STRING, m.menuId, m.menuLabel);
    }

    // 固定项：窗口菜单
    HMENU windowMenu = CreatePopupMenu();
    AppendMenuW(windowMenu, MF_STRING, IDM_CASCADE,  L"层叠(&C)");
    AppendMenuW(windowMenu, MF_STRING, IDM_TILE_H,   L"水平平铺(&H)");
    AppendMenuW(windowMenu, MF_STRING, IDM_TILE_V,   L"垂直平铺(&V)");
    AppendMenuW(windowMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(windowMenu, MF_STRING, IDM_ARRANGE,  L"排列图标(&A)");
    AppendMenuW(windowMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(windowMenu, MF_STRING, IDM_CLOSE_ACTIVE, L"关闭当前(&L)");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(windowMenu), L"窗口(&W)");

    // 固定项：关于 + 退出（归入最后一个 subMenu 或单独处理）
    HMENU sysMenu = CreatePopupMenu();
    AppendMenuW(sysMenu, MF_STRING, IDM_ABOUT, L"关于(&A)...");
    AppendMenuW(sysMenu, MF_STRING, IDM_EXIT,  L"退出(&X)");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(sysMenu), L"系统(&Y)");

    SetMenu(hwnd, bar);
    return windowMenu;
}
```

> 注意：系统设置菜单项已通过 `g_modules` 的 `L"系统"` 条目自动注册。`关于` 和 `退出` 属于固定项，不在 `g_modules` 中，保留手动处理。如果以后需要将 `关于`/`退出` 也统一，可以把它们定义为特殊 `ModuleDef`（`create = nullptr`）。

### 自动命令分发

```cpp
case WM_COMMAND: {
    int id = LOWORD(wp);
    // 1. 先查模块注册表
    for (int i = 0; i < g_moduleCount; i++) {
        if (g_modules[i].menuId == id) {
            ModuleContext ctx = { g_ctx.mdiClient, g_ctx.instance,
                                  g_ctx.uiFont, g_ctx.dbSettings, g_ctx.fontSize };
            g_modules[i].create(ctx);
            return 0;
        }
    }
    // 2. 固定项
    switch (id) {
        case IDM_ABOUT:
            MessageBoxW(hwnd, L"LIS 工作台\n版本 v2026.05.07\n\n作者：Zhao Wang", L"关于", MB_ICONINFORMATION);
            return 0;
        case ID_BTNCLOSE:  closeActiveMdiChild(); return 0;
        case IDM_CASCADE:  SendMessageW(g_ctx.mdiClient, WM_MDICASCADE, 0, 0); return 0;
        case IDM_TILE_H:   SendMessageW(g_ctx.mdiClient, WM_MDITILE, MDITILE_HORIZONTAL, 0); return 0;
        case IDM_TILE_V:   SendMessageW(g_ctx.mdiClient, WM_MDITILE, MDITILE_VERTICAL, 0); return 0;
        case IDM_ARRANGE:  SendMessageW(g_ctx.mdiClient, WM_MDIICONARRANGE, 0, 0); return 0;
        case IDM_CLOSE_ACTIVE: closeActiveMdiChild(); return 0;
    }
    return 0;
}
```

### 效果

新增模块时 `main_frame.cpp` 改动量：

```diff
+ #include "new_module.h"
  const ModuleDef g_modules[] = {
      ...
+     { L"NewMod", L"检验管理", L"新模块(&N)...", IDM_NEW_MOD, create_new_module },
  };
```

菜单自动生成、命令自动分发，无需改 `setupMenus` 或 `WM_COMMAND` 的 switch-case。

---

## 4. 新增模块检查清单

1. 新建 `src/xxx_module.cpp`
2. 从模板复制骨架，修改：`WND_CLASS`、`PROP_STATE`、`XxxState` 结构体、`wndProc` 逻辑、`create_xxx_module` 工厂函数
3. 在 `main_frame.cpp`：
   - 添加 `#include "xxx_module.h"`
   - 分配新的 `IDM_XXX` 常量
   - 在 `g_modules[]` 加一行
4. 在 `CMakeLists.txt` 的 `main_app` 源文件列表加 `src/xxx_module.cpp`
5. 私有配置全部使用 `search::load_module_int(L"Xxx", ...)` / `search::save_module_int(L"Xxx", ...)`

当前已接入的真实模块包括：

- `Query`：检验结果查询
- `Blood`：输血结果查询
- `Barcode`：已签收条码查询
- `RegularReport`：常规报告
- `Settings`：系统设置

### Win32 长表单滚动绘制经验

`RegularReport` 左侧长表单曾出现拖动滚动条后组件残影停留。最终采用的方案是：

- `leftPanel` / `leftContent` 保持容器级 `WS_CLIPCHILDREN | WS_CLIPSIBLINGS`。
- 表单内部控件统一使用 `WS_CLIPSIBLINGS`，减少兄弟窗口互相覆盖绘制。
- 不再创建真实 `GROUPBOX` 子窗口；真实 `GROUPBOX` 会覆盖整个分组区域，内部控件作为兄弟窗口启用 `WS_CLIPSIBLINGS` 后会被它裁剪掉。
- 分组边框和标题改由内容容器在 `WM_PAINT` 中自绘，标题字体跟随模块字体重新生成。
- 对会随拖条宽度变化而换行的装饰/摘要文字，优先由父面板在 `WM_PAINT` 中自绘并自行计算高度，避免透明 `STATIC` 控件在频繁 `MoveWindow` 时留下残影。

后续遇到“滚动大量 Win32 子控件残影”时，优先避免让大面积装饰性控件参与子窗口裁剪；对仅用于视觉分组的边框/标题，优先考虑父窗口自绘，而不是创建真实子窗口。

---

## 5. 实施步骤

| 步骤 | 内容 | 影响文件 |
|------|------|----------|
| 5.1 | 新建 `module_registry.h` | 新增 |
| 5.2 | `app_settings_io` 新增 `save/load_module_int/str` | 修改 |
| 5.3 | 抽出 `query_module.cpp`，遵循模板 + `ModuleContext` | 重写 |
| 5.4 | 提取设置子窗口为 `settings_module.cpp`，从 `main_frame.cpp` 移出 | 新增+清理 |
| 5.5 | `main_frame.cpp` 引入 `g_modules[]`，自动菜单+自动分发 | 重构 |
| 5.6 | 各模块 INI 读写切换到模块 API | 迁移 |
| 5.7 | `CMakeLists.txt` 更新源文件列表 | 修改 |

---

## 6. 常见问题

**Q：模块需要额外的共享状态怎么办？**
在 `ModuleContext` 加字段。所有模块的 `create` 自动获取。

**Q：单实例模块（如设置窗口）怎么处理？**
模块自己的工厂函数里调用 `activate_existing_mdi_child_by_title(ctx.mdiClient, WINDOW_TITLE)`。该 helper 在 `module_registry.h` 中统一遍历 MDI 子窗口标题，已存在则 `WM_MDIACTIVATE`，不创建新的。主窗口注册表不关心这个。

**Q：模块的菜单项需要放在两个不同的 parent 下？**
注册两行 `ModuleDef`，用同一个 `create` 但不同 `menuParent`/`menuLabel`/`menuId`。

**Q：模块需要额外链接库？**
在 `CMakeLists.txt` 添加。模块多了可建 `add_library(module_common STATIC ...)` 统一管理公共依赖。

**Q：独立调试模块？**
在模块 `.cpp` 底部加 `#ifdef STANDALONE` 块，提供 `WinMain` 注册顶层窗口直接调用 `create_xxx_module`，调试完切回主程序编译。

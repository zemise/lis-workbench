# cpp_search Qt 引入准备

## 当前已完成

- `search_core.*`
  - 保持为数据库访问层。
  - 不依赖 Win32 控件。
- `app_settings.*`
  - 负责本地配置读写与数据库连接串生成。
  - 后续 Qt 可直接复用。
- `search_app.*`
  - 负责界面无关的应用层规则：
    - 查询输入组装
    - 报告状态显示词
    - 打印状态显示词
    - 查询总数状态文案
    - 报告行/结果行语义状态
  - 后续 Qt 可直接复用。
- `search_controller.*`
  - 负责应用控制层：
    - 测试连接
    - 加载检验科室/病人类型/检验仪器
    - 执行主查询
    - 按 `REP_NO` 加载右侧项目明细
  - 后续 Qt 可直接复用。
- `search_input_view_model.*`
  - 负责当前 Win32 输入层：
    - 控件值读取
    - 下拉框回填
    - 查询输入组装
  - 后续 Qt 可保留接口语义，替换具体实现。
- `search_ui_context.*`
  - 负责 UI 上下文边界。
  - 后续 Qt 可替换为 Qt widget/context 持有结构。
- `search_ui_events.*`
  - 负责 Win32 事件分发。
  - 后续 Qt 可替换为 signal/slot 或 Qt event 绑定层。
- `search_ui_presenter.*`
  - 负责列表和状态呈现。
  - 后续 Qt 可替换为 model/view 或 presenter 适配。

## 当前仍绑定 Win32 的部分

- `search_ui_layout.*`
  - Win32 主界面控件创建
  - `ListView` 列定义
  - 自适应布局
  - splitter 所在的布局层
- `search_settings_dialog.*`
  - Win32 设置窗口
  - 设置页控件创建与消息处理
- `main.cpp`
  - splitter 拖动消息
  - 字体应用
  - 消息循环与消息分发

## 下一步建议拆分顺序

1. `search_view_state.*`
   - 聚合当前全局状态：
     - 当前查询条件
     - 当前报告列表
     - 当前项目列表
     - 当前字典下拉数据
   - 减少 `main.cpp` 里的全局变量数量。

2. 继续把少量剩余窗口过程胶水收口到更明确的 event/presenter 协作
   - 统一管理当前字典数据、查询结果、选择状态。
   - 继续收口 Win32 全局变量。

3. Qt UI 单独新建 `qt/` 或 `src_qt/`
   - 不直接修改现有 Win32 入口。
   - 先并行做 `cpp_search` 的 Qt 试验版，再决定是否替换默认入口。

## 不建议的做法

- 不建议在现有 `main.cpp` 里直接混入 Qt 条件编译。
- 不建议先改数据库层再改界面层。
- 不建议为了 Qt 迁移去重写 `search_core.*`。

## 判断标准

如果未来某天要接 Qt，理想状态应该是：

- `search_core.*`、`app_settings.*`、`search_app.*` 原样复用
- Win32 和 Qt 仅分别维护自己的窗口层
- 查询条件、结果映射、状态判断不需要在两套 GUI 中重复写一份

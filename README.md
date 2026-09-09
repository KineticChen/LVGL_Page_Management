# LVGL 页面管理框架 使用说明

一套基于 LVGL 9.2 的通用页面管理框架，提供**栈式导航**、**每页可配的生命周期（离开即销毁 /
常驻缓存）**、**create/destroy/event 钩子** 和 **可配置切换动画**。核心仅依赖 `lvgl.h`，
可在 ESP32 / STM32 / Linux 间复用。

## 目录结构

```
ui/
  lv_page_manager.h   页面类接口 + 管理器 API
  lv_page_manager.c   实现
  pages/
    page_demo.*       示例页面 1（仪表盘 + 跳转按钮）
    page_second.*     示例页面 2（返回按钮）
```

## 核心概念

- **页面类 `lv_page_class_t`**：一个 `const` 静态描述符，定义页面「如何创建、销毁、处理事件」。
  同一个页面类可以被多次实例化（每次 push/pop 重建）。
- **页面实例 `lv_page_t *`**：管理器内部持有的句柄，代表栈上的一层。回调里能拿到它，用来存取
  自定义数据、获取 root 对象。
- **root 对象**：每个页面拥有一个独立的屏幕对象（`lv_obj_create(NULL)`），所有控件挂在它下面。
- **离开即销毁**：栈中只有**栈顶**页面持有存活的 root；被压在下面的页面对象会被删除以省内存，
  返回时按页面类**重新创建**。因此 `on_create` 可能被调用多次，不要假设只创建一次。

## 三步接入（已在本项目 main.c 完成）

```c
#include "lv_page_manager.h"
#include "pages/page_demo.h"

// 在持有 LVGL 锁的上下文中：
_lock_acquire(&lvgl_api_lock);
lv_page_manager_init(display);                                  // 1. 初始化，传入 display
lv_page_manager_push(&page_demo_class, NULL, &LV_PAGE_ANIM_NONE); // 2. 压入首页
_lock_release(&lvgl_api_lock);
```

## 写一个新页面

新建 `pages/page_foo.h` 和 `pages/page_foo.c`，然后把 `.c` 加进 `main/CMakeLists.txt` 的 `SRCS`。

**page_foo.h**
```c
#pragma once
#include "lv_page_manager.h"
extern const lv_page_class_t page_foo_class;
```

**page_foo.c**
```c
#include "page_foo.h"

// 在 root 上构建 UI；arg 是 push 时传入的参数
static void foo_on_create(lv_page_t *page, lv_obj_t *root, void *arg)
{
    lv_obj_t *label = lv_label_create(root);
    lv_label_set_text(label, "Hello Foo");
    lv_obj_center(label);

    // 如需在其它回调里访问自己的数据，存到 page 上：
    // lv_page_set_user_data(page, my_ctx);
}

// 页面对象删除前调用（可选）：释放非 LVGL 资源，如 lv_timer、malloc 的内存。
// 注意：页面内的 LVGL 对象（含挂在它们上的 lv_anim）会被自动删除，无需手动清理。
static void foo_on_destroy(lv_page_t *page)
{
    // free(lv_page_get_user_data(page));
}

// 统一事件入口（可选），配合 lv_page_manager_send_event 使用
static void foo_on_event(lv_page_t *page, uint32_t event_id, void *param)
{
}

const lv_page_class_t page_foo_class = {
    .name       = "foo",
    .keep_alive = false,           // false=离开即销毁(默认); true=常驻缓存
    .on_create  = foo_on_create,
    .on_destroy = foo_on_destroy,  // 不需要可填 NULL
    .on_event   = foo_on_event,    // 不需要可填 NULL
};
```

## 生命周期策略：离开即销毁 vs 常驻缓存

每个页面类用 `keep_alive` 字段单独选择策略，这是为跨平台复用设计的核心开关：

| 策略 | keep_alive | 被压到下层时 | 返回时 | 适用 |
|------|-----------|------------|--------|------|
| 离开即销毁（默认） | `false` | 删除对象，`on_destroy` | 重新 `on_create` | RAM 紧张（STM32）、一次性详情/弹窗页 |
| 常驻缓存 | `true`  | 保留对象、不销毁 | 直接复用、状态保留 | 内存充裕（Linux/带 PSRAM）、频繁切换的主页/导航页 |

要点：
- `keep_alive` 只影响页面**被压栈到下层**时的行为。页面一旦被 `pop`/`replace`/`pop_to_root`
  **移出栈**，无论该标志如何都会被销毁。
- 常驻页在后台仍是一个存活的屏幕对象——如果它上面挂了无限动画/`lv_timer`，后台会继续跑并占用
  CPU。需要的话可在切走时自行暂停（可借助 `on_event` 或标准 LVGL 屏幕事件）。

## 日志：宏形式，一键切后端

框架内部与示例页统一用 `LV_PAGE_LOGI` / `LV_PAGE_LOGW`，默认走 LVGL 自带的 `LV_LOG_*`（跨平台）。
在 ESP-IDF 上若想用 `ESP_LOG`，只需在编译期定义宏：

```cmake
# main/CMakeLists.txt
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_PAGE_LOG_USE_ESP=1)
```

或在包含头文件前 `#define LV_PAGE_LOG_USE_ESP 1`。移植到 STM32/Linux 时保持默认即可，
无需改任何业务代码。

## 页面切换 API

在任意 LVGL 事件回调（如按钮 `LV_EVENT_CLICKED`）里调用即可：

```c
// 进入新页（压栈）
lv_page_manager_push(&page_foo_class, arg, &anim);

// 返回上一页（出栈，当前页销毁，下层页重建）
lv_page_manager_pop(&anim);

// 一直返回到根页
lv_page_manager_pop_to_root(&anim);

// 替换当前栈顶（栈深不变，旧页销毁）
lv_page_manager_replace(&page_foo_class, arg, &anim);

// 查询
lv_page_t *top = lv_page_manager_top();
uint32_t   n   = lv_page_manager_depth();

// 主动给栈顶页发事件（触发其 on_event）
lv_page_manager_send_event(MY_EVENT_ID, param);
```

`arg` 会原样传给目标页面的 `on_create`，用于向新页面传参（如列表项 id）。

## 切换动画

```c
typedef struct {
    lv_screen_load_anim_t anim; // LV_SCR_LOAD_ANIM_NONE / MOVE_LEFT / MOVE_RIGHT / FADE_IN / OVER_* ...
    uint32_t time;              // 动画时长 ms（0 = 瞬时）
    uint32_t delay;             // 延迟 ms
} lv_page_anim_t;
```

两个预设宏可直接取址传入：
- `&LV_PAGE_ANIM_NONE` —— 无动画硬切
- `&LV_PAGE_ANIM_DEFAULT` —— 淡入 250ms

自定义示例（进右滑、退左滑）：
```c
lv_page_anim_t enter = { LV_SCR_LOAD_ANIM_MOVE_LEFT,  300, 0 };
lv_page_anim_t back  = { LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0 };
lv_page_manager_push(&page_foo_class, NULL, &enter);
lv_page_manager_pop(&back);
```

## 生命周期时序

以 A（下层）默认策略为例：

```
push(B) over A:
  B.on_create  →  切换动画  →  (动画结束) A.on_destroy → 删除 A 对象
                                （A 仍留在栈里，只是对象被释放）

pop() 回到 A:
  A.on_create（重建）→ 切换动画 → (动画结束) B.on_destroy → 删除 B 对象 → B 出栈
```

若 A 声明了 `keep_alive = true`：push(B) 时 **不会** 触发 `A.on_destroy`（A 对象保留）；
pop() 回到 A 时 **不会** 再次 `A.on_create`（直接复用）。B 作为出栈页仍照常销毁。

要点：`on_destroy` 在对象**被删除前**调用，所以里面仍可访问页面控件。

## 线程安全

LVGL 非线程安全，本框架**不自带加锁**。规则：所有 `lv_page_manager_*` 调用必须在持有
LVGL 锁的上下文中执行。

- ✅ 在 LVGL 事件 / 动画回调里调用 —— 已在 `lv_timer_handler` 的锁内，安全。
- ⚠️ 从其它 FreeRTOS 任务调用 —— 需自己用 main.c 里的 `lvgl_api_lock` 包裹：
  ```c
  _lock_acquire(&lvgl_api_lock);
  lv_page_manager_push(...);
  _lock_release(&lvgl_api_lock);
  ```

## 注意事项

- 默认（`keep_alive=false`）页面的 `on_create` 可能被多次调用（每次进入/返回都重建），
  初始化逻辑要可重入，不要依赖「只创建一次」；常驻页（`keep_alive=true`）在栈内只创建一次。
- 页面内的 LVGL 对象与其上的 `lv_anim` 会随页面销毁自动清理；只有非 LVGL 资源（`lv_timer`、
  `malloc`、外设句柄等）才需要在 `on_destroy` 里手动释放。
- 栈最大深度由 `LV_PAGE_MANAGER_MAX_DEPTH` 决定（默认 16），可在编译期用宏覆盖。
- **切换动画进行中会拒绝一切导航操作**：`push`/`pop`/`pop_to_root`/`replace` 若在动画未结束时被调用，
  会直接返回（不执行）并打印 `LV_PAGE_LOGW` 警告，避免重入导致状态错乱。动画结束（新屏
  `SCREEN_LOADED` 事件）后自动解锁；无动画的瞬时切换同步完成、不会拦截后续操作。
  注意：`lv_page_manager_send_event` 不受此封锁影响（它只转发事件，不改变导航栈）。

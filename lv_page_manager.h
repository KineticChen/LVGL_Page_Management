/*
 * LVGL 通用页面管理框架
 *
 * 栈式导航 + 离开即销毁的生命周期 + 可配置切换动画。
 *
 * 线程安全：所有 lv_page_manager_* 调用必须在持有 LVGL 锁的上下文中执行
 * （LVGL 事件/动画回调天然满足；其他任务调用需自行加锁）。
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 日志后端（宏形式，方便切换）：
 *   默认使用 LVGL 自带日志 LV_LOG_*（跨平台，ESP32/STM32/Linux 通用）。
 *   在编译期定义 LV_PAGE_LOG_USE_ESP=1 即切换到 ESP-IDF 的 ESP_LOG。
 */
#ifndef LV_PAGE_LOG_USE_ESP
#define LV_PAGE_LOG_USE_ESP 1
#endif

#if LV_PAGE_LOG_USE_ESP
#include "esp_log.h"
#define LV_PAGE_LOGI(fmt, ...) ESP_LOGI("lv_page", fmt, ##__VA_ARGS__)
#define LV_PAGE_LOGW(fmt, ...) ESP_LOGW("lv_page", fmt, ##__VA_ARGS__)
#else
#define LV_PAGE_LOGI(fmt, ...) LV_LOG_USER(fmt, ##__VA_ARGS__)
#define LV_PAGE_LOGW(fmt, ...) LV_LOG_WARN(fmt, ##__VA_ARGS__)
#endif

/* 不透明页面句柄，由管理器内部持有 */
typedef struct lv_page lv_page_t;

/* 页面类：描述一个页面如何创建、销毁与处理事件 */
typedef struct {
    const char *name;
    /*
     * 生命周期策略：
     *   false（默认）= 离开即销毁：被压栈到下层时删除对象，返回时重建（省内存）。
     *   true         = 常驻缓存：被压栈到下层时保留对象，返回时直接复用（切换快、状态保留）。
     * 注意：无论该标志如何，页面一旦被 pop/replace 移出栈都会被销毁。
     */
    bool keep_alive;
    /* 在 root 屏幕对象上构建控件；arg 为 push 时传入的参数 */
    void (*on_create)(lv_page_t *page, lv_obj_t *root, void *arg);
    /* 页面对象被删除前调用，用于释放非 LVGL 资源（timer/buffer 等），可为 NULL */
    void (*on_destroy)(lv_page_t *page);
    /* 统一事件入口（按键/自定义事件），可为 NULL */
    void (*on_event)(lv_page_t *page, uint32_t event_id, void *param);
} lv_page_class_t;

/* 切换动画配置 */
typedef struct {
    lv_screen_load_anim_t anim; /* LV_SCR_LOAD_ANIM_NONE / MOVE_LEFT / FADE_IN ... */
    uint32_t time;              /* 动画时长 ms */
    uint32_t delay;             /* 延迟 ms */
} lv_page_anim_t;

/* 预设动画：无动画 / 默认淡入 */
#define LV_PAGE_ANIM_NONE    ((lv_page_anim_t){ LV_SCR_LOAD_ANIM_NONE, 0, 0 })
#define LV_PAGE_ANIM_DEFAULT ((lv_page_anim_t){ LV_SCR_LOAD_ANIM_FADE_IN, 250, 0 })

/* --- 页面自身数据存取 --- */
void      lv_page_set_user_data(lv_page_t *page, void *data);
void     *lv_page_get_user_data(lv_page_t *page);
lv_obj_t *lv_page_get_root(lv_page_t *page);

/* --- 管理器生命周期 --- */
void lv_page_manager_init(lv_display_t *disp);
void lv_page_manager_deinit(void);

/* --- 导航 --- */
/* 入栈并切换到新页 */
lv_page_t *lv_page_manager_push(const lv_page_class_t *cls, void *arg, const lv_page_anim_t *anim);
/* 出栈，重建下层页并返回 */
void lv_page_manager_pop(const lv_page_anim_t *anim);
/* 一直出栈到根页 */
void lv_page_manager_pop_to_root(const lv_page_anim_t *anim);
/* 替换栈顶（栈深不变） */
lv_page_t *lv_page_manager_replace(const lv_page_class_t *cls, void *arg, const lv_page_anim_t *anim);

/* --- 查询与事件 --- */
lv_page_t *lv_page_manager_top(void);
uint32_t   lv_page_manager_depth(void);
/* 转发事件给栈顶页 */
void       lv_page_manager_send_event(uint32_t event_id, void *param);

#ifdef __cplusplus
}
#endif

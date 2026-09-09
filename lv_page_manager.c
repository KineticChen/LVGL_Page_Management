/*
 * LVGL 通用页面管理框架 —— 实现
 *
 * 内存策略：每页可配（lv_page_class_t.keep_alive）。
 *   默认离开即销毁——被压到下层的页面删除对象、仅保留类描述符与 arg，返回时重建；
 *   常驻缓存（keep_alive=true）——被压到下层时保留对象，返回时直接复用。
 * 无论策略如何，页面被 pop/replace 移出栈时都会销毁。
 *
 * 销毁时序：切换用 lv_screen_load_anim(auto_del=false)，在旧屏的
 * LV_EVENT_SCREEN_UNLOADED 回调里先执行页面 on_destroy 钩子，再删除对象，
 * 从而保证钩子能访问到即将删除的控件。该事件在瞬时与动画两种路径下都会触发。
 */
#include "lv_page_manager.h"

#ifndef LV_PAGE_MANAGER_MAX_DEPTH
#define LV_PAGE_MANAGER_MAX_DEPTH 16
#endif

struct lv_page {
    const lv_page_class_t *cls;
    void     *arg;
    void     *user_data;
    lv_obj_t *root;
    bool      pending_free; /* UNLOADED 回调销毁后是否释放本结构（pop/replace 为 true） */
};

static lv_display_t *s_disp;
static lv_page_t    *s_stack[LV_PAGE_MANAGER_MAX_DEPTH];
static uint32_t      s_depth;
static bool          s_transitioning; /* 切换动画进行中：期间拒绝一切导航操作 */

/* 导航前置检查：动画进行中则拒绝并打日志，返回 false 表示应中止操作 */
static bool nav_allowed(const char *op)
{
    if (s_transitioning) {
        LV_PAGE_LOGW("navigation '%s' rejected: page transition in progress", op);
        return false;
    }
    return true;
}

/* --- 页面数据存取 --- */
void lv_page_set_user_data(lv_page_t *page, void *data) { if (page) page->user_data = data; }
void *lv_page_get_user_data(lv_page_t *page) { return page ? page->user_data : NULL; }
lv_obj_t *lv_page_get_root(lv_page_t *page) { return page ? page->root : NULL; }

/* --- 销毁回调 --- */
static void on_page_unloaded(lv_event_t *e)
{
    lv_page_t *page = lv_event_get_user_data(e);
    if (page->cls->on_destroy) {
        page->cls->on_destroy(page);
    }
    lv_obj_delete(page->root);
    page->root = NULL;
    if (page->pending_free) {
        lv_free(page);
    }
}

/* 首次加载时挂在 LVGL 默认屏上，仅删除、不涉及页面钩子 */
static void on_orphan_unloaded(lv_event_t *e)
{
    lv_obj_delete(lv_event_get_target(e));
}

/* 新屏加载完成 → 切换动画结束，解除导航封锁。
 * 瞬时切换（time=0）时该事件同步触发，故不会误拦后续操作。 */
static void on_page_loaded(lv_event_t *e)
{
    LV_UNUSED(e);
    s_transitioning = false;
}

/* 构建页面的 root 对象并调用 on_create */
static void page_build(lv_page_t *page)
{
    page->root = lv_obj_create(NULL);
    /* 每个 root 仅注册一次；常驻页复用同一对象也不会重复累加 */
    lv_obj_add_event_cb(page->root, on_page_loaded, LV_EVENT_SCREEN_LOADED, NULL);
    if (page->cls->on_create) {
        page->cls->on_create(page, page->root, page->arg);
    }
}

/*
 * 把 new_page 载入前台。
 * old_page 非空且 destroy_old 为真时，其对象在卸载后经 on_destroy 钩子销毁；
 * destroy_old 为假（常驻缓存）时，旧对象保留、切走后仍存活。
 */
static void do_switch(lv_page_t *new_page, lv_page_t *old_page, bool destroy_old, const lv_page_anim_t *anim)
{
    lv_page_anim_t a = anim ? *anim : LV_PAGE_ANIM_NONE;
    lv_obj_t *cur = lv_display_get_screen_active(s_disp);

    if (old_page && old_page->root && destroy_old) {
        lv_obj_add_event_cb(old_page->root, on_page_unloaded, LV_EVENT_SCREEN_UNLOADED, old_page);
    } else if (!old_page && cur && cur != new_page->root) {
        /* 首个页面：卸载 LVGL 创建的默认空屏 */
        lv_obj_add_event_cb(cur, on_orphan_unloaded, LV_EVENT_SCREEN_UNLOADED, NULL);
    }

    s_transitioning = true; /* 由 new_page 的 SCREEN_LOADED 事件清除 */
    lv_screen_load_anim(new_page->root, a.anim, a.time, a.delay, false);
}

/* 直接销毁一个非当前活动页面的对象（用于移出栈的中间页/缓存页） */
static void destroy_page_now(lv_page_t *page, bool free_struct)
{
    if (page->root) {
        if (page->cls->on_destroy) {
            page->cls->on_destroy(page);
        }
        lv_obj_delete(page->root);
        page->root = NULL;
    }
    if (free_struct) {
        lv_free(page);
    }
}

/* --- 管理器生命周期 --- */
void lv_page_manager_init(lv_display_t *disp)
{
    s_disp          = disp;
    s_depth         = 0;
    s_transitioning = false;
}

void lv_page_manager_deinit(void)
{
    for (uint32_t i = 0; i < s_depth; i++) {
        destroy_page_now(s_stack[i], true);
        s_stack[i] = NULL;
    }
    s_depth = 0;
    s_disp  = NULL;
}

/* --- 导航 --- */
lv_page_t *lv_page_manager_push(const lv_page_class_t *cls, void *arg, const lv_page_anim_t *anim)
{
    if (!nav_allowed("push")) {
        return NULL;
    }
    if (!cls || s_depth >= LV_PAGE_MANAGER_MAX_DEPTH) {
        LV_PAGE_LOGW("page push rejected (null cls or stack full)");
        return NULL;
    }
    lv_page_t *page = lv_malloc(sizeof(lv_page_t));
    LV_ASSERT_MALLOC(page);
    lv_memzero(page, sizeof(lv_page_t));
    page->cls = cls;
    page->arg = arg;
    page_build(page);

    lv_page_t *old = s_depth > 0 ? s_stack[s_depth - 1] : NULL;
    s_stack[s_depth++] = page;
    /* old 留在栈中：非常驻页销毁其 root，常驻页保留以便返回时复用 */
    bool destroy_old = old && !old->cls->keep_alive;
    do_switch(page, old, destroy_old, anim);
    return page;
}

void lv_page_manager_pop(const lv_page_anim_t *anim)
{
    if (!nav_allowed("pop")) {
        return;
    }
    if (s_depth <= 1) {
        return; /* 根页不出栈 */
    }
    lv_page_t *top   = s_stack[s_depth - 1];
    lv_page_t *below = s_stack[s_depth - 2];

    if (!below->root) {
        page_build(below); /* 非常驻页此前已被销毁，重建；常驻页 root 仍在，直接复用 */
    }
    s_depth--;
    top->pending_free = true;         /* top 移出栈，销毁后释放结构 */
    do_switch(below, top, true, anim);
}

void lv_page_manager_pop_to_root(const lv_page_anim_t *anim)
{
    if (!nav_allowed("pop_to_root")) {
        return;
    }
    if (s_depth <= 1) {
        return;
    }
    lv_page_t *top  = s_stack[s_depth - 1];
    lv_page_t *root = s_stack[0];

    /* 释放根与栈顶之间的中间页（常驻页可能仍持有活动对象，一并销毁） */
    for (uint32_t i = 1; i < s_depth - 1; i++) {
        destroy_page_now(s_stack[i], true);
        s_stack[i] = NULL;
    }

    if (!root->root) {
        page_build(root); /* 常驻根页直接复用，否则重建 */
    }
    s_depth = 1;
    top->pending_free = true;
    do_switch(root, top, true, anim);
}

lv_page_t *lv_page_manager_replace(const lv_page_class_t *cls, void *arg, const lv_page_anim_t *anim)
{
    if (!nav_allowed("replace")) {
        return NULL;
    }
    if (!cls) {
        return NULL;
    }
    if (s_depth == 0) {
        return lv_page_manager_push(cls, arg, anim);
    }
    lv_page_t *page = lv_malloc(sizeof(lv_page_t));
    LV_ASSERT_MALLOC(page);
    lv_memzero(page, sizeof(lv_page_t));
    page->cls = cls;
    page->arg = arg;
    page_build(page);

    lv_page_t *old = s_stack[s_depth - 1];
    s_stack[s_depth - 1] = page; /* 替换栈顶槽，栈深不变 */
    old->pending_free = true;    /* 旧栈顶移出栈，销毁后释放结构 */
    do_switch(page, old, true, anim);
    return page;
}

/* --- 查询与事件 --- */
lv_page_t *lv_page_manager_top(void)
{
    return s_depth > 0 ? s_stack[s_depth - 1] : NULL;
}

uint32_t lv_page_manager_depth(void)
{
    return s_depth;
}

void lv_page_manager_send_event(uint32_t event_id, void *param)
{
    lv_page_t *top = lv_page_manager_top();
    if (top && top->cls->on_event) {
        top->cls->on_event(top, event_id, param);
    }
}

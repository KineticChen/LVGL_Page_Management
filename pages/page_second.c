/*
 * 示例页面 2：演示 pop 返回导航与事件钩子。
 */
#include "page_second.h"

static void back_btn_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_page_anim_t anim = { LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0 };
    lv_page_manager_pop(&anim);
}

static void second_on_create(lv_page_t *page, lv_obj_t *root, void *arg)
{
    LV_UNUSED(page);
    LV_UNUSED(arg);
    LV_PAGE_LOGI("page_second on_create");

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "Second Page");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *btn = lv_button_create(root);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 40);
    lv_obj_add_event_cb(btn, back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "< Back");
    lv_obj_center(btn_label);
}

static void second_on_destroy(lv_page_t *page)
{
    LV_UNUSED(page);
    LV_PAGE_LOGI("page_second on_destroy");
}

const lv_page_class_t page_second_class = {
    .name       = "second",
    .on_create  = second_on_create,
    .on_destroy = second_on_destroy,
    .on_event   = NULL,
};

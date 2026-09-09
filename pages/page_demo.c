/*
 * 示例页面 1：把原 lvgl_demo_ui.c 的仪表盘 UI 迁移为一个受管页面。
 * 额外增加「下一页」按钮，演示 push 导航。
 */
#include "page_demo.h"
#include "page_second.h"

static lv_style_t style_bullet;
static bool       style_bullet_inited;
static const lv_font_t *font_normal = &lv_font_montserrat_16;

/* 每个 arc 的对应 label 存在 arc 的 user_data 里，回调直接取，
 * 避免用「负索引数子对象」这种会被其它子对象（如按钮）打乱的脆弱定位。 */
static void indic1_anim_cb(void *var, int32_t v)
{
    lv_arc_set_value(var, v);
    lv_label_set_text_fmt(lv_obj_get_user_data(var), "Revenue: %" LV_PRId32 " %%", v);
}

static void indic2_anim_cb(void *var, int32_t v)
{
    lv_arc_set_value(var, v);
    lv_label_set_text_fmt(lv_obj_get_user_data(var), "Sales: %" LV_PRId32 " %%", v);
}

static void indic3_anim_cb(void *var, int32_t v)
{
    lv_arc_set_value(var, v);
    lv_label_set_text_fmt(lv_obj_get_user_data(var), "Costs: %" LV_PRId32 " %%", v);
}

static lv_obj_t *create_scale_box(lv_obj_t *parent, const char *text1, const char *text2, const char *text3,
                                  lv_obj_t *out_labels[3])
{
    lv_obj_t *scale = lv_scale_create(parent);
    lv_obj_center(scale);
    lv_obj_set_size(scale, 600, 600);
    lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_OUTER);
    lv_scale_set_label_show(scale, false);
    lv_scale_set_post_draw(scale, true);
    lv_obj_set_width(scale, LV_PCT(100));
    lv_obj_set_style_pad_all(scale, 30, 0);

    lv_obj_t *bullet1 = lv_obj_create(parent);
    lv_obj_set_size(bullet1, 13, 13);
    lv_obj_remove_style(bullet1, NULL, LV_PART_SCROLLBAR);
    lv_obj_add_style(bullet1, &style_bullet, 0);
    lv_obj_set_style_bg_color(bullet1, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_t *label1 = lv_label_create(parent);
    lv_label_set_text(label1, text1);

    lv_obj_t *bullet2 = lv_obj_create(parent);
    lv_obj_set_size(bullet2, 13, 13);
    lv_obj_remove_style(bullet2, NULL, LV_PART_SCROLLBAR);
    lv_obj_add_style(bullet2, &style_bullet, 0);
    lv_obj_set_style_bg_color(bullet2, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_t *label2 = lv_label_create(parent);
    lv_label_set_text(label2, text2);

    lv_obj_t *bullet3 = lv_obj_create(parent);
    lv_obj_set_size(bullet3, 13, 13);
    lv_obj_remove_style(bullet3, NULL, LV_PART_SCROLLBAR);
    lv_obj_add_style(bullet3, &style_bullet, 0);
    lv_obj_set_style_bg_color(bullet3, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_t *label3 = lv_label_create(parent);
    lv_label_set_text(label3, text3);

    static int32_t grid_col_dsc[] = {LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static int32_t grid_row_dsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(parent, grid_col_dsc, grid_row_dsc);
    lv_obj_set_grid_cell(scale, LV_GRID_ALIGN_START, 0, 2, LV_GRID_ALIGN_START, 1, 1);
    lv_obj_set_grid_cell(bullet1, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_START, 2, 1);
    lv_obj_set_grid_cell(bullet2, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_START, 3, 1);
    lv_obj_set_grid_cell(bullet3, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_START, 4, 1);
    lv_obj_set_grid_cell(label1, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_START, 2, 1);
    lv_obj_set_grid_cell(label2, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_START, 3, 1);
    lv_obj_set_grid_cell(label3, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_START, 4, 1);

    out_labels[0] = label1;
    out_labels[1] = label2;
    out_labels[2] = label3;
    return scale;
}

static void start_arc_anim(lv_obj_t *arc, lv_anim_exec_xcb_t cb, uint32_t dur, uint32_t playback)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_values(&a, 20, 100);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_var(&a, arc);
    lv_anim_set_duration(&a, dur);
    lv_anim_set_playback_duration(&a, playback);
    lv_anim_start(&a);
}

static lv_obj_t *create_arc(lv_obj_t *scale, lv_color_t color, int32_t margin)
{
    lv_obj_t *arc = lv_arc_create(scale);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    if (margin == 0) {
        lv_obj_remove_style(arc, NULL, LV_PART_MAIN);
    }
    lv_obj_set_size(arc, lv_pct(100), lv_pct(100));
    if (margin > 0) {
        lv_obj_set_style_margin_all(arc, margin, 0);
    }
    lv_obj_set_style_arc_opa(arc, 0, 0);
    lv_obj_set_style_arc_width(arc, 15, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(arc);
    return arc;
}

static void next_btn_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_page_anim_t anim = { LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0 };
    lv_page_manager_push(&page_second_class, NULL, &anim);
}

static void demo_on_create(lv_page_t *page, lv_obj_t *root, void *arg)
{
    LV_UNUSED(arg);
    LV_PAGE_LOGI("page_demo on_create");

    lv_display_t *disp = lv_obj_get_display(root);
    lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
                          LV_THEME_DEFAULT_DARK, font_normal);

    if (!style_bullet_inited) {
        lv_style_init(&style_bullet);
        lv_style_set_border_width(&style_bullet, 0);
        lv_style_set_radius(&style_bullet, LV_RADIUS_CIRCLE);
        style_bullet_inited = true;
    }

    lv_obj_t *labels[3];
    lv_obj_t *scale = create_scale_box(root, "Revenue", "Sales", "Costs", labels);
    lv_page_set_user_data(page, scale);

    lv_obj_t *arc1 = create_arc(scale, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_user_data(arc1, labels[0]);
    start_arc_anim(arc1, indic1_anim_cb, 4100, 2700);

    lv_obj_t *arc2 = create_arc(scale, lv_palette_main(LV_PALETTE_RED), 20);
    lv_obj_set_user_data(arc2, labels[1]);
    start_arc_anim(arc2, indic2_anim_cb, 2600, 3200);

    lv_obj_t *arc3 = create_arc(scale, lv_palette_main(LV_PALETTE_GREEN), 40);
    lv_obj_set_user_data(arc3, labels[2]);
    start_arc_anim(arc3, indic3_anim_cb, 2800, 1800);

    lv_obj_t *btn = lv_button_create(root);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(btn, next_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Next >");
    lv_obj_center(btn_label);
}

static void demo_on_destroy(lv_page_t *page)
{
    LV_UNUSED(page);
    LV_PAGE_LOGI("page_demo on_destroy");
}

const lv_page_class_t page_demo_class = {
    .name       = "demo",
    .on_create  = demo_on_create,
    .on_destroy = demo_on_destroy,
    .on_event   = NULL,
};

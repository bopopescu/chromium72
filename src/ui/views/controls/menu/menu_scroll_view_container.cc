// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/menu/menu_scroll_view_container.h"

#include "base/macros.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_palette.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/controls/menu/menu_config.h"
#include "ui/views/controls/menu/menu_controller.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/submenu_view.h"
#include "ui/views/round_rect_painter.h"

using ui::NativeTheme;

namespace views {

namespace {

static constexpr int kBorderPaddingDueToRoundedCorners = 1;

// MenuScrollButton ------------------------------------------------------------

// MenuScrollButton is used for the scroll buttons when not all menu items fit
// on screen. MenuScrollButton forwards appropriate events to the
// MenuController.

class MenuScrollButton : public View {
 public:
  MenuScrollButton(SubmenuView* host, bool is_up)
      : host_(host),
        is_up_(is_up),
        // Make our height the same as that of other MenuItemViews.
        pref_height_(MenuItemView::pref_menu_height()) {
  }

  gfx::Size CalculatePreferredSize() const override {
    return gfx::Size(MenuConfig::instance().scroll_arrow_height * 2 - 1,
                     pref_height_);
  }

  bool CanDrop(const OSExchangeData& data) override {
    DCHECK(host_->GetMenuItem()->GetMenuController());
    return true;  // Always return true so that drop events are targeted to us.
  }

  void OnDragEntered(const ui::DropTargetEvent& event) override {
    DCHECK(host_->GetMenuItem()->GetMenuController());
    host_->GetMenuItem()->GetMenuController()->OnDragEnteredScrollButton(
        host_, is_up_);
  }

  int OnDragUpdated(const ui::DropTargetEvent& event) override {
    return ui::DragDropTypes::DRAG_NONE;
  }

  void OnDragExited() override {
    DCHECK(host_->GetMenuItem()->GetMenuController());
    host_->GetMenuItem()->GetMenuController()->OnDragExitedScrollButton(host_);
  }

  int OnPerformDrop(const ui::DropTargetEvent& event) override {
    return ui::DragDropTypes::DRAG_NONE;
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const MenuConfig& config = MenuConfig::instance();

    // The background.
    gfx::Rect item_bounds(0, 0, width(), height());
    NativeTheme::ExtraParams extra;
    GetNativeTheme()->Paint(canvas->sk_canvas(),
                            NativeTheme::kMenuItemBackground,
                            NativeTheme::kNormal, item_bounds, extra);

    // Then the arrow.
    int x = width() / 2;
    int y = (height() - config.scroll_arrow_height) / 2;

    int x_left = x - config.scroll_arrow_height;
    int x_right = x + config.scroll_arrow_height;
    int y_bottom;

    if (!is_up_) {
      y_bottom = y;
      y = y_bottom + config.scroll_arrow_height;
    } else {
      y_bottom = y + config.scroll_arrow_height;
    }
    SkPath path;
    path.setFillType(SkPath::kWinding_FillType);
    path.moveTo(SkIntToScalar(x), SkIntToScalar(y));
    path.lineTo(SkIntToScalar(x_left), SkIntToScalar(y_bottom));
    path.lineTo(SkIntToScalar(x_right), SkIntToScalar(y_bottom));
    path.lineTo(SkIntToScalar(x), SkIntToScalar(y));
    cc::PaintFlags flags;
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setAntiAlias(true);
    flags.setColor(config.arrow_color);
    canvas->DrawPath(path, flags);
  }

 private:
  // SubmenuView we were created for.
  SubmenuView* host_;

  // Direction of the button.
  bool is_up_;

  // Preferred height.
  int pref_height_;

  DISALLOW_COPY_AND_ASSIGN(MenuScrollButton);
};

}  // namespace

// MenuScrollView --------------------------------------------------------------

// MenuScrollView is a viewport for the SubmenuView. It's reason to exist is so
// that ScrollRectToVisible works.
//
// NOTE: It is possible to use ScrollView directly (after making it deal with
// null scrollbars), but clicking on a child of ScrollView forces the window to
// become active, which we don't want. As we really only need a fraction of
// what ScrollView does, so we use a one off variant.

class MenuScrollViewContainer::MenuScrollView : public View {
 public:
  explicit MenuScrollView(View* child) {
    AddChildView(child);
  }

  void ScrollRectToVisible(const gfx::Rect& rect) override {
    // NOTE: this assumes we only want to scroll in the y direction.

    // If the rect is already visible, do not scroll.
    if (GetLocalBounds().Contains(rect))
      return;

    // Scroll just enough so that the rect is visible.
    int dy = 0;
    if (rect.bottom() > GetLocalBounds().bottom())
      dy = rect.bottom() - GetLocalBounds().bottom();
    else
      dy = rect.y();

    // Convert rect.y() to view's coordinates and make sure we don't show past
    // the bottom of the view.
    View* child = GetContents();
    child->SetY(-std::max(0, std::min(
        child->GetPreferredSize().height() - this->height(),
        dy - child->y())));
  }

  // Returns the contents, which is the SubmenuView.
  View* GetContents() {
    return child_at(0);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MenuScrollView);
};

// MenuScrollViewContainer ----------------------------------------------------

MenuScrollViewContainer::MenuScrollViewContainer(SubmenuView* content_view)
    : content_view_(content_view) {
  scroll_up_button_ = new MenuScrollButton(content_view, true);
  scroll_down_button_ = new MenuScrollButton(content_view, false);
  AddChildView(scroll_up_button_);
  AddChildView(scroll_down_button_);

  scroll_view_ = new MenuScrollView(content_view);
  AddChildView(scroll_view_);

  arrow_ = BubbleBorderTypeFromAnchor(
      content_view_->GetMenuItem()->GetMenuController()->GetAnchorPosition());

  CreateBorder();
}

bool MenuScrollViewContainer::HasBubbleBorder() {
  return arrow_ != BubbleBorder::NONE;
}

void MenuScrollViewContainer::SetBubbleArrowOffset(int offset) {
  DCHECK(HasBubbleBorder());
  bubble_border_->set_arrow_offset(offset);
}

MenuItemView* MenuScrollViewContainer::GetFootnote() const {
  MenuItemView* footnote = content_view_->GetLastItem();
  if (!footnote || footnote->GetType() != MenuItemView::HIGHLIGHTED)
    return nullptr;
  return footnote;
}

gfx::Size MenuScrollViewContainer::CalculatePreferredSize() const {
  gfx::Size prefsize = scroll_view_->GetContents()->GetPreferredSize();
  gfx::Insets insets = GetInsets();
  prefsize.Enlarge(insets.width(), insets.height());
  if (GetFootnote() && bubble_border_)
    prefsize.Enlarge(0, bubble_border_->GetBorderCornerRadius());
  return prefsize;
}

void MenuScrollViewContainer::Layout() {
  gfx::Insets insets = GetInsets();
  int x = insets.left();
  int y = insets.top();
  int width = View::width() - insets.width();
  int content_height = height() - insets.height();
  MenuItemView* footnote = GetFootnote();
  if (!scroll_up_button_->visible()) {
    if (footnote && bubble_border_)
      footnote->SetCornerRadius(bubble_border_->GetBorderCornerRadius());
    scroll_view_->SetBounds(x, y, width, content_height);
    scroll_view_->Layout();
    return;
  }

  // Don't round the footnote when the scroll button is visible.
  if (footnote)
    footnote->SetCornerRadius(0);

  gfx::Size pref = scroll_up_button_->GetPreferredSize();
  scroll_up_button_->SetBounds(x, y, width, pref.height());
  content_height -= pref.height();

  const int scroll_view_y = y + pref.height();

  pref = scroll_down_button_->GetPreferredSize();
  scroll_down_button_->SetBounds(x, height() - pref.height() - insets.top(),
                                 width, pref.height());
  content_height -= pref.height();

  scroll_view_->SetBounds(x, scroll_view_y, width, content_height);
  scroll_view_->Layout();
}

void MenuScrollViewContainer::OnNativeThemeChanged(
    const ui::NativeTheme* theme) {
  if (!HasBubbleBorder())
    CreateDefaultBorder();
}

void MenuScrollViewContainer::OnPaintBackground(gfx::Canvas* canvas) {
  if (background()) {
    View::OnPaintBackground(canvas);
    return;
  }

  gfx::Rect bounds(0, 0, width(), height());
  NativeTheme::ExtraParams extra;
  const MenuConfig& menu_config = MenuConfig::instance();
  extra.menu_background.corner_radius = menu_config.CornerRadiusForMenu(
      content_view_->GetMenuItem()->GetMenuController());
  GetNativeTheme()->Paint(canvas->sk_canvas(),
      NativeTheme::kMenuPopupBackground, NativeTheme::kNormal, bounds, extra);
}

void MenuScrollViewContainer::GetAccessibleNodeData(ui::AXNodeData* node_data) {
  // Get the name from the submenu view.
  content_view_->GetAccessibleNodeData(node_data);
  node_data->role = ax::mojom::Role::kMenuBar;
}

void MenuScrollViewContainer::OnBoundsChanged(
    const gfx::Rect& previous_bounds) {
  gfx::Size content_pref = scroll_view_->GetContents()->GetPreferredSize();
  scroll_up_button_->SetVisible(content_pref.height() > height());
  scroll_down_button_->SetVisible(content_pref.height() > height());
  Layout();
}

void MenuScrollViewContainer::CreateBorder() {
  if (HasBubbleBorder())
    CreateBubbleBorder();
  else
    CreateDefaultBorder();
}

void MenuScrollViewContainer::CreateDefaultBorder() {
  DCHECK_EQ(arrow_, BubbleBorder::NONE);
  bubble_border_ = nullptr;

  const MenuConfig& menu_config = MenuConfig::instance();
  const ui::NativeTheme* native_theme = GetNativeTheme();
  MenuController* controller =
      content_view_->GetMenuItem()->GetMenuController();
  bool use_outer_border =
      menu_config.use_outer_border ||
      (native_theme && native_theme->UsesHighContrastColors());
  int corner_radius = menu_config.CornerRadiusForMenu(controller);
  int padding = use_outer_border && corner_radius > 0
                    ? kBorderPaddingDueToRoundedCorners
                    : 0;

  const int vertical_inset =
      (corner_radius ? corner_radius : menu_config.menu_vertical_border_size) +
      padding;
  const int horizontal_inset =
      menu_config.menu_horizontal_border_size + padding;

  int bottom_inset = GetFootnote() ? 0 : vertical_inset;

  if (use_outer_border) {
    SkColor color = GetNativeTheme()
                        ? GetNativeTheme()->GetSystemColor(
                              ui::NativeTheme::kColorId_MenuBorderColor)
                        : gfx::kPlaceholderColor;
    SetBorder(views::CreateBorderPainter(
        std::make_unique<views::RoundRectPainter>(color, corner_radius),
        gfx::Insets(vertical_inset, horizontal_inset, bottom_inset,
                    horizontal_inset)));
  } else {
    SetBorder(CreateEmptyBorder(vertical_inset, horizontal_inset, bottom_inset,
                                horizontal_inset));
  }
}

void MenuScrollViewContainer::CreateBubbleBorder() {
  bubble_border_ =
      new BubbleBorder(arrow_, BubbleBorder::SMALL_SHADOW, SK_ColorWHITE);
  if (content_view_->GetMenuItem()
          ->GetMenuController()
          ->use_touchable_layout()) {
    const MenuConfig& menu_config = MenuConfig::instance();
    bubble_border_->SetCornerRadius(menu_config.touchable_corner_radius);
    bubble_border_->set_md_shadow_elevation(
        menu_config.touchable_menu_shadow_elevation);
    gfx::Insets insets(menu_config.vertical_touchable_menu_item_padding, 0);
    if (GetFootnote())
      insets.Set(menu_config.vertical_touchable_menu_item_padding, 0, 0, 0);
    scroll_view_->GetContents()->SetBorder(CreateEmptyBorder(insets));
  }

  SetBorder(std::unique_ptr<Border>(bubble_border_));
  SetBackground(std::make_unique<BubbleBackground>(bubble_border_));
}

BubbleBorder::Arrow MenuScrollViewContainer::BubbleBorderTypeFromAnchor(
    MenuAnchorPosition anchor) {
  switch (anchor) {
    case MENU_ANCHOR_BUBBLE_LEFT:
      return BubbleBorder::RIGHT_CENTER;
    case MENU_ANCHOR_BUBBLE_RIGHT:
      return BubbleBorder::LEFT_CENTER;
    case MENU_ANCHOR_BUBBLE_ABOVE:
      return BubbleBorder::BOTTOM_CENTER;
    case MENU_ANCHOR_BUBBLE_BELOW:
      return BubbleBorder::TOP_CENTER;
    case MENU_ANCHOR_BUBBLE_TOUCHABLE_ABOVE:
    case MENU_ANCHOR_BUBBLE_TOUCHABLE_LEFT:
    case MENU_ANCHOR_BUBBLE_TOUCHABLE_RIGHT:
      return BubbleBorder::FLOAT;
    default:
      return BubbleBorder::NONE;
  }
}

}  // namespace views

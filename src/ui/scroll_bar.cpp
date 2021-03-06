// Aseprite UI Library
// Copyright (C) 2001-2013  David Capello
//
// This source file is distributed under MIT license,
// please read LICENSE.txt for more information.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gfx/size.h"
#include "ui/message.h"
#include "ui/scroll_bar.h"
#include "ui/theme.h"
#include "ui/view.h"

namespace ui {

using namespace gfx;

// Internal stuff shared by all scroll-bars (as the user cannot move
// two scroll-bars at the same time).
int ScrollBar::m_wherepos = 0;
int ScrollBar::m_whereclick = 0;

ScrollBar::ScrollBar(int align)
  : Widget(kViewScrollbarWidget)
  , m_pos(0)
  , m_size(0)
  , m_barWidth(getTheme()->scrollbar_size)
{
  setAlign(align);
  initTheme();
}

void ScrollBar::setPos(int pos)
{
  if (m_pos != pos) {
    m_pos = pos;
    invalidate();
  }
}

void ScrollBar::setSize(int size)
{
  if (m_size != size) {
    m_size = size;
    invalidate();
  }
}

void ScrollBar::getScrollBarThemeInfo(int* pos, int* len)
{
  getScrollBarInfo(pos, len, NULL, NULL);
}

bool ScrollBar::onProcessMessage(Message* msg)
{
#define MOUSE_IN(x1, y1, x2, y2) \
  ((mousePos.x >= (x1)) && (mousePos.x <= (x2)) && \
   (mousePos.y >= (y1)) && (mousePos.y <= (y2)))

  switch (msg->type()) {

    case kMouseDownMessage: {
      gfx::Point mousePos = static_cast<MouseMessage*>(msg)->position();
      View* view = static_cast<View*>(getParent());
      int x1, y1, x2, y2;
      int u1, v1, u2, v2;
      bool ret = false;
      int pos, len;

      getScrollBarThemeInfo(&pos, &len);

      m_wherepos = pos;
      m_whereclick = getAlign() & JI_HORIZONTAL ?
        mousePos.x:
        mousePos.y;

      x1 = getBounds().x;
      y1 = getBounds().y;
      x2 = getBounds().x2()-1;
      y2 = getBounds().y2()-1;

      u1 = x1 + this->border_width.l;
      v1 = y1 + this->border_width.t;
      u2 = x2 - this->border_width.r;
      v2 = y2 - this->border_width.b;

      Point scroll = view->getViewScroll();

      if (this->getAlign() & JI_HORIZONTAL) {
        // in the bar
        if (MOUSE_IN(u1+pos, v1, u1+pos+len-1, v2)) {
          // capture mouse
        }
        // left
        else if (MOUSE_IN(x1, y1, u1+pos-1, y2)) {
          scroll.x -= view->getViewport()->getBounds().w/2;
          ret = true;
        }
        // right
        else if (MOUSE_IN(u1+pos+len, y1, x2, y2)) {
          scroll.x += view->getViewport()->getBounds().w/2;
          ret = true;
        }
      }
      else {
        // in the bar
        if (MOUSE_IN(u1, v1+pos, u2, v1+pos+len-1)) {
          // capture mouse
        }
        // left
        else if (MOUSE_IN(x1, y1, x2, v1+pos-1)) {
          scroll.y -= view->getViewport()->getBounds().h/2;
          ret = true;
        }
        // right
        else if (MOUSE_IN(x1, v1+pos+len, x2, y2)) {
          scroll.y += view->getViewport()->getBounds().h/2;
          ret = true;
        }
      }

      if (ret) {
        view->setViewScroll(scroll);
        return ret;
      }

      setSelected(true);
      captureMouse();

      // continue to kMouseMoveMessage handler...
    }

    case kMouseMoveMessage:
      if (hasCapture()) {
        gfx::Point mousePos = static_cast<MouseMessage*>(msg)->position();
        View* view = static_cast<View*>(getParent());
        int pos, len, bar_size, viewport_size;
        int old_pos;

        getScrollBarInfo(&pos, &len, &bar_size, &viewport_size);
        old_pos = pos;

        if (bar_size > len) {
          Point scroll = view->getViewScroll();

          if (this->getAlign() & JI_HORIZONTAL) {
            pos = (m_wherepos + mousePos.x - m_whereclick);
            pos = MID(0, pos, bar_size - len);

            scroll.x = (m_size - viewport_size) * pos / (bar_size - len);
            view->setViewScroll(scroll);
          }
          else {
            pos = (m_wherepos + mousePos.y - m_whereclick);
            pos = MID(0, pos, bar_size - len);

            scroll.y = (m_size - viewport_size) * pos / (bar_size - len);
            view->setViewScroll(scroll);
          }
        }
      }
      break;

    case kMouseUpMessage:
      setSelected(false);
      releaseMouse();
      break;

    case kMouseEnterMessage:
    case kMouseLeaveMessage:
      // TODO add something to avoid this (theme specific stuff)
      invalidate();
      break;
  }

  return Widget::onProcessMessage(msg);
}

void ScrollBar::onPaint(PaintEvent& ev)
{
  getTheme()->paintViewScrollbar(ev);
}

void ScrollBar::getScrollBarInfo(int *_pos, int *_len, int *_bar_size, int *_viewport_size)
{
  View* view = static_cast<View*>(getParent());
  int bar_size, viewport_size;
  int pos, len;
  int border_width;

  if (this->getAlign() & JI_HORIZONTAL) {
    bar_size = getBounds().w;
    viewport_size = view->getVisibleSize().w;
    border_width = this->border_width.t + this->border_width.b;
  }
  else {
    bar_size = getBounds().h;
    viewport_size = view->getVisibleSize().h;
    border_width = this->border_width.l + this->border_width.r;
  }

  if (m_size <= viewport_size) {
    len = bar_size;
    pos = 0;
  }
  else {
    len = bar_size * viewport_size / m_size;
    len = MID(getTheme()->scrollbar_size*2-border_width, len, bar_size);
    pos = (bar_size-len) * m_pos / (m_size-viewport_size);
    pos = MID(0, pos, bar_size-len);
  }

  if (_pos) *_pos = pos;
  if (_len) *_len = len;
  if (_bar_size) *_bar_size = bar_size;
  if (_viewport_size) *_viewport_size = viewport_size;
}

} // namespace ui

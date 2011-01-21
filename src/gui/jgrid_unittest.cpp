/* ASE - Allegro Sprite Editor
 * Copyright (C) 2001-2011  David Capello
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define TEST_GUI
#include "tests/test.h"
#include "gfx/size.h"

using namespace gfx;

/**
 * Tests two widgets in a row, each one of 10x10 pixels.
 */
TEST(JGrid, Simple2x1Grid)
{
  JWidget grid = jgrid_new(2, false);
  JWidget w1 = new Widget(JI_WIDGET);
  JWidget w2 = new Widget(JI_WIDGET);

  jwidget_set_min_size(w1, 10, 10);
  jwidget_set_min_size(w2, 10, 10);

  jgrid_add_child(grid, w1, 1, 1, 0);
  jgrid_add_child(grid, w2, 1, 1, 0);

  // Test request-size
  Size reqSize = grid->getPreferredSize();
  EXPECT_EQ(20, reqSize.w);
  EXPECT_EQ(10, reqSize.h);

  // Test child-spacing
  grid->child_spacing = 2;
  reqSize = grid->getPreferredSize();
  EXPECT_EQ(22, reqSize.w);
  EXPECT_EQ(10, reqSize.h);

  // Test borders
  grid->border_width.l = 3;
  grid->border_width.b = 3;
  reqSize = grid->getPreferredSize();
  EXPECT_EQ(25, reqSize.w);
  EXPECT_EQ(13, reqSize.h);

  grid->border_width.r = 5;
  grid->border_width.t = 2;
  reqSize = grid->getPreferredSize();
  EXPECT_EQ(30, reqSize.w);
  EXPECT_EQ(15, reqSize.h);

  delete grid;
}

TEST(JGrid, Expand2ndWidget)
{
  Widget* grid = jgrid_new(2, false);
  Widget* w1 = new Widget(JI_WIDGET);
  Widget* w2 = new Widget(JI_WIDGET);
  JRect rect;

  jwidget_set_min_size(w1, 20, 20);
  jwidget_set_min_size(w2, 10, 10);

  jgrid_add_child(grid, w1, 1, 1, 0);
  jgrid_add_child(grid, w2, 1, 1, JI_HORIZONTAL | JI_TOP);

  // Test request size
  Size reqSize = grid->getPreferredSize();
  EXPECT_EQ(30, reqSize.w);
  EXPECT_EQ(20, reqSize.h);

  // Test layout
  rect = jrect_new(0, 0, 40, 20);
  jwidget_set_rect(grid, rect);
  jrect_free(rect);

  EXPECT_EQ(0, w1->rc->x1);
  EXPECT_EQ(0, w1->rc->y1);
  EXPECT_EQ(20, jrect_w(w1->rc));
  EXPECT_EQ(20, jrect_h(w1->rc));

  EXPECT_EQ(20, w2->rc->x1);
  EXPECT_EQ(0, w2->rc->y1);
  EXPECT_EQ(20, jrect_w(w2->rc));
  EXPECT_EQ(10, jrect_h(w2->rc));

  delete grid;
}

TEST(JGrid, SameWidth2x1Grid)
{
  JWidget grid = jgrid_new(2, true);
  JWidget w1 = new Widget(JI_WIDGET);
  JWidget w2 = new Widget(JI_WIDGET);
  JRect rect;

  jwidget_set_min_size(w1, 20, 20);
  jwidget_set_min_size(w2, 10, 10);

  jgrid_add_child(grid, w1, 1, 1, 0);
  jgrid_add_child(grid, w2, 1, 1, 0);

  // Test request size
  Size reqSize = grid->getPreferredSize();
  EXPECT_EQ(40, reqSize.w);
  EXPECT_EQ(20, reqSize.h);

  // Test layout
  rect = jrect_new(0, 0, 60, 20);
  jwidget_set_rect(grid, rect);
  jrect_free(rect);

  EXPECT_EQ(0, w1->rc->x1);
  EXPECT_EQ(30, w2->rc->x1);
  EXPECT_EQ(30, jrect_w(w1->rc));
  EXPECT_EQ(30, jrect_w(w2->rc));

  delete grid;
}

/**
 * Tests the next layout (a grid of 3x3 cells):
 * 
 * <pre>
 *        2 (separator)
 *       _|_
 *      /   \
 *   10 2 0 2 10
 *  +---+-------+
 *  | 1 | 2     | 10
 *  +---+---+---+ --- 2 (separator)
 *  | 3     | 4 | 4
 *  |       |   | --- 2 (separator)
 *  |       |   | 4
 *  +-------+---+
 *
 *  1.align = 0
 *  2.align = JI_HORIZONTAL
 *  3.align = JI_HORIZONTAL | JI_VERTICAL
 *  4.align = JI_VERTICAL
 * </pre>
 *
 * When expand the grid:
 *
 * <pre>
 *  +---+----------------------------+
 *  | 1 | 2       		     |
 *  +---+------------------------+---+
 *  | 3                	       	 | 4 |
 *  |            		 |   |
 *  |            		 |   |
 *  |            		 |   |
 *  |				 |   |
 *  |				 |   |
 *  |				 |   |
 *  +----------------------------+---+
 * </pre>
 */
TEST(JGrid, Intrincate3x3Grid)
{
  Widget* grid = jgrid_new(3, false);
  Widget* w1 = new Widget(JI_WIDGET);
  Widget* w2 = new Widget(JI_WIDGET);
  Widget* w3 = new Widget(JI_WIDGET);
  Widget* w4 = new Widget(JI_WIDGET);
  JRect rect;

  jwidget_set_min_size(w1, 10, 10);
  jwidget_set_min_size(w2, 10, 10);
  jwidget_set_min_size(w3, 10, 10);
  jwidget_set_min_size(w4, 10, 10);

  jgrid_add_child(grid, w1, 1, 1, 0);
  jgrid_add_child(grid, w2, 2, 1, JI_HORIZONTAL);
  jgrid_add_child(grid, w3, 2, 2, JI_HORIZONTAL | JI_VERTICAL);
  jgrid_add_child(grid, w4, 1, 2, JI_VERTICAL);

  // Test request size
  grid->child_spacing = 2;
  Size reqSize = grid->getPreferredSize();
  EXPECT_EQ(22, reqSize.w);
  EXPECT_EQ(22, reqSize.h);

  // Test layout
  rect = jrect_new(0, 0, 100, 100);
  jwidget_set_rect(grid, rect);
  jrect_free(rect);

  EXPECT_EQ(0, w1->rc->x1);
  EXPECT_EQ(0, w1->rc->y1);
  EXPECT_EQ(12, w2->rc->x1);
  EXPECT_EQ(0, w2->rc->y1);
  EXPECT_EQ(0, w3->rc->x1);
  EXPECT_EQ(12, w3->rc->y1);
  EXPECT_EQ(90, w4->rc->x1);
  EXPECT_EQ(12, w4->rc->y1);

  EXPECT_EQ(10, jrect_w(w1->rc));
  EXPECT_EQ(10, jrect_h(w1->rc));
  EXPECT_EQ(88, jrect_w(w2->rc));
  EXPECT_EQ(10, jrect_h(w2->rc));
  EXPECT_EQ(88, jrect_w(w3->rc));
  EXPECT_EQ(88, jrect_h(w3->rc));
  EXPECT_EQ(10, jrect_w(w4->rc));
  EXPECT_EQ(88, jrect_h(w4->rc));

  delete grid;
}
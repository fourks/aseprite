/* Aseprite
 * Copyright (C) 2001-2013  David Capello
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#include <allegro.h>

#include "ui/ui.h"

#include "app/app.h"
#include "app/commands/command.h"
#include "app/commands/commands.h"
#include "app/context.h"
#include "app/modules/editors.h"
#include "app/modules/gfx.h"
#include "app/modules/gui.h"
#include "app/settings/document_settings.h"
#include "app/settings/settings.h"
#include "app/ui/editor/editor.h"
#include "app/ui/status_bar.h"
#include "app/util/render.h"
#include "raster/conversion_alleg.h"
#include "raster/image.h"
#include "raster/palette.h"
#include "raster/primitives.h"
#include "raster/sprite.h"

#define PREVIEW_TILED           1
#define PREVIEW_FIT_ON_SCREEN   2

namespace app {

using namespace ui;
using namespace raster;
using namespace filters;

class PreviewCommand : public Command {
public:
  PreviewCommand();
  Command* clone() { return new PreviewCommand(*this); }

protected:
  bool onEnabled(Context* context);
  void onExecute(Context* context);
};

PreviewCommand::PreviewCommand()
  : Command("Preview",
            "Preview",
            CmdUIOnlyFlag)
{
}

bool PreviewCommand::onEnabled(Context* context)
{
  return context->checkFlags(ContextFlags::ActiveDocumentIsWritable |
                             ContextFlags::HasActiveSprite);
}

// Shows the sprite using the complete screen.
void PreviewCommand::onExecute(Context* context)
{
  Editor* editor = current_editor;

  // Cancel operation if current editor does not have a sprite
  if (!editor || !editor->getSprite())
    return;

  // Do not use DocumentWriter (do not lock the document) because we
  // will call other sub-commands (e.g. previous frame, next frame,
  // etc.).
  Document* document = editor->getDocument();
  Sprite* sprite = editor->getSprite();
  const Palette* pal = sprite->getPalette(editor->getFrame());
  View* view = View::getView(editor);
  int u, v, x, y;
  int index_bg_color = -1;
  IDocumentSettings* docSettings = context->getSettings()->getDocumentSettings(document);
  filters::TiledMode tiled = docSettings->getTiledMode();

  // Free mouse
  editor->getManager()->freeMouse();

  // Clear extras (e.g. pen preview)
  document->destroyExtraCel();

  gfx::Rect vp = view->getViewportBounds();
  gfx::Point scroll = view->getViewScroll();

  int old_mouse_x = jmouse_x(0);
  int old_mouse_y = jmouse_y(0);

  jmouse_set_cursor(kNoCursor);
  ui::set_mouse_position(gfx::Point(JI_SCREEN_W/2, JI_SCREEN_H/2));

  int pos_x = - scroll.x + vp.x + editor->getOffsetX();
  int pos_y = - scroll.y + vp.y + editor->getOffsetY();
  int delta_x = 0;
  int delta_y = 0;

  int zoom = editor->getZoom();
  int w = sprite->getWidth() << zoom;
  int h = sprite->getHeight() << zoom;

  bool redraw = true;

  // Render the sprite
  base::UniquePtr<Image> render;
  base::UniquePtr<Image> doublebuf(Image::create(IMAGE_RGB, JI_SCREEN_W, JI_SCREEN_H));

  do {
    // Update scroll
    if (jmouse_poll()) {
      delta_x += (jmouse_x(0) - JI_SCREEN_W/2);
      delta_y += (jmouse_y(0) - JI_SCREEN_H/2);
      ui::set_mouse_position(gfx::Point(JI_SCREEN_W/2, JI_SCREEN_H/2));
      jmouse_poll();

      redraw = true;
    }

    // Render sprite and leave the result in 'render' variable
    if (render == NULL) {
      RenderEngine renderEngine(document, sprite,
                                editor->getLayer(),
                                editor->getFrame());
      render.reset(renderEngine.renderSprite(0, 0, sprite->getWidth(), sprite->getHeight(),
                                             editor->getFrame(), 0, false));
    }

    // Redraw the screen
    if (redraw) {
      redraw = false;
      dirty_display_flag = true;

      x = pos_x + ((delta_x >> zoom) << zoom);
      y = pos_y + ((delta_y >> zoom) << zoom);

      if (tiled & TILED_X_AXIS) x = SGN(x) * (ABS(x)%w);
      if (tiled & TILED_Y_AXIS) y = SGN(y) * (ABS(y)%h);

      if (index_bg_color == -1)
        RenderEngine::renderCheckedBackground(doublebuf, -pos_x, -pos_y, zoom);
      else
        raster::clear_image(doublebuf, pal->getEntry(index_bg_color));

      switch (tiled) {
        case TILED_NONE:
          RenderEngine::renderImage(doublebuf, render, pal, x, y, zoom);
          break;
        case TILED_X_AXIS:
          for (u=x-w; u<JI_SCREEN_W+w; u+=w)
            RenderEngine::renderImage(doublebuf, render, pal, u, y, zoom);
          break;
        case TILED_Y_AXIS:
          for (v=y-h; v<JI_SCREEN_H+h; v+=h)
            RenderEngine::renderImage(doublebuf, render, pal, x, v, zoom);
          break;
        case TILED_BOTH:
          for (v=y-h; v<JI_SCREEN_H+h; v+=h)
            for (u=x-w; u<JI_SCREEN_W+w; u+=w)
              RenderEngine::renderImage(doublebuf, render, pal, u, v, zoom);
          break;
      }

      raster::convert_image_to_allegro(doublebuf, ji_screen, 0, 0, pal);
    }

    // It is necessary in case ji_screen is double-bufferred
    gui_feedback();

    if (keypressed()) {
      int readkey_value = readkey();
      Message* msg = create_message_from_readkey_value(kKeyDownMessage, readkey_value);
      Command* command = NULL;
      get_command_from_key_message(msg, &command, NULL);
      delete msg;

      // Change frame
      if (command != NULL &&
          (strcmp(command->short_name(), CommandId::GotoFirstFrame) == 0 ||
           strcmp(command->short_name(), CommandId::GotoPreviousFrame) == 0 ||
           strcmp(command->short_name(), CommandId::GotoNextFrame) == 0 ||
           strcmp(command->short_name(), CommandId::GotoLastFrame) == 0)) {
        // Execute the command
        context->executeCommand(command);

        // Redraw
        redraw = true;

        // Re-render
        render.reset(NULL);
      }
      // Play the animation
      else if (command != NULL &&
               strcmp(command->short_name(), CommandId::PlayAnimation) == 0) {
        // TODO
      }
      // Change background color
      else if ((readkey_value>>8) == KEY_PLUS_PAD ||
               (readkey_value&0xff) == '+') {
        if (index_bg_color == -1 ||
            index_bg_color < pal->size()-1) {
          ++index_bg_color;
          redraw = true;
        }
      }
      else if ((readkey_value>>8) == KEY_MINUS_PAD ||
               (readkey_value&0xff) == '-') {
        if (index_bg_color >= 0) {
          --index_bg_color;     // can be -1 which is the checked background
          redraw = true;
        }
      }
      else
        break;
    }
  } while (jmouse_b(0) == kButtonNone);

  do {
    jmouse_poll();
    gui_feedback();
  } while (jmouse_b(0) != kButtonNone);
  clear_keybuf();

  ui::set_mouse_position(gfx::Point(old_mouse_x, old_mouse_y));
  jmouse_set_cursor(kArrowCursor);

  ui::Manager::getDefault()->invalidate();
}

Command* CommandFactory::createPreviewCommand()
{
  return new PreviewCommand;
}

} // namespace app

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

#include "app/ui/file_selector.h"

#include "app/app.h"
#include "app/file/file.h"
#include "app/find_widget.h"
#include "app/ini_file.h"
#include "app/modules/gfx.h"
#include "app/modules/gui.h"
#include "app/recent_files.h"
#include "app/ui/file_list.h"
#include "app/ui/skin/skin_parts.h"
#include "app/widget_loader.h"
#include "base/bind.h"
#include "base/path.h"
#include "base/split_string.h"
#include "ui/ui.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include <allegro.h>
#include <allegro/internal/aintern.h>
#include <cerrno>

#if (DEVICE_SEPARATOR != 0) && (DEVICE_SEPARATOR != '\0')
#  define HAVE_DRIVES
#endif

#ifndef MAX_PATH
#  define MAX_PATH 4096         /* TODO this is needed for Linux, is it correct? */
#endif

namespace app {

using namespace app::skin;
using namespace ui;

template<class Container>
class NullableIterator {
public:
  typedef typename Container::iterator iterator;

  NullableIterator() : m_isNull(true) { }

  void reset() { m_isNull = true; }

  bool isNull() const { return m_isNull; }
  bool isValid() const { return !m_isNull; }

  iterator getIterator() {
    ASSERT(!m_isNull);
    return m_iterator;
  }

  void setIterator(const iterator& it) {
    m_isNull = false;
    m_iterator = it;
  }

private:
  bool m_isNull;
  typename Container::iterator m_iterator;
};

// Variables used only to maintain the history of navigation.
static FileItemList* navigation_history = NULL; // Set of FileItems navigated
static NullableIterator<FileItemList> navigation_position; // Current position in the navigation history
static bool navigation_locked = false;  // If true the navigation_history isn't
                                        // modified if the current folder changes
                                        // (used when the back/forward buttons
                                        // are pushed)

// Slot for App::Exit signal
static void on_exit_delete_navigation_history()
{
  delete navigation_history;
}

class CustomFileNameEntry : public Entry
{
public:
  CustomFileNameEntry() : Entry(256, ""), m_fileList(NULL) {
  }

  void setAssociatedFileList(FileList* fileList) {
    m_fileList = fileList;
  }

protected:
  virtual bool onProcessMessage(Message* msg) OVERRIDE {
    if (msg->type() == kKeyUpMessage &&
        static_cast<KeyMessage*>(msg)->unicodeChar() >= 32) {
      // Check if all keys are released
      for (int c=0; c<KEY_MAX; ++c) {
        if (key[c])
          return false;
      }

      // String to be autocompleted
      base::string left_part = getText();
      if (left_part.empty())
        return false;

      const FileItemList& children = m_fileList->getFileList();

      for (FileItemList::const_iterator
             it=children.begin(); it!=children.end(); ++it) {
        IFileItem* child = *it;
        base::string child_name = child->getDisplayName();

        base::string::iterator it1, it2;

        for (it1 = child_name.begin(), it2 = left_part.begin();
             it1!=child_name.end() && it2!=left_part.end();
             ++it1, ++it2) {
          if (std::tolower(*it1) != std::tolower(*it2))
            break;
        }

        // Is the pattern (left_part) in the child_name's beginning?
        if (it2 == left_part.end()) {
          setText(child_name.c_str());
          selectText(child_name.size(), left_part.size());
          clear_keybuf();
          return true;
        }
      }
    }
    return Entry::onProcessMessage(msg);
  }

private:
  FileList* m_fileList;
};

// Class to create CustomFileNameEntries.
class CustomFileNameEntryCreator : public app::WidgetLoader::IWidgetTypeCreator {
public:
  ~CustomFileNameEntryCreator() { }
  void dispose() OVERRIDE { delete this; }
  Widget* createWidgetFromXml(const TiXmlElement* xmlElem) OVERRIDE {
    return new CustomFileNameEntry();
  }
};

class CustomFileNameItem : public ListItem
{
public:
  CustomFileNameItem(const char* text, IFileItem* fileItem)
    : ListItem(text)
    , m_fileItem(fileItem)
  {
  }

  IFileItem* getFileItem() { return m_fileItem; }

private:
  IFileItem* m_fileItem;
};

class CustomFolderNameItem : public ListItem
{
public:
  CustomFolderNameItem(const char* text)
    : ListItem(text)
  {
  }
};

FileSelector::FileSelector()
  : Window(WithTitleBar, "")
{
  app::WidgetLoader loader;
  loader.addWidgetType("filenameentry", new CustomFileNameEntryCreator);

  // Load the main widget.
  Box* box = loader.loadWidgetT<Box>("file_selector.xml", "main");
  addChild(box);

  View* view = app::find_widget<View>(this, "fileview_container");
  m_goBack = app::find_widget<Button>(this, "goback");
  m_goForward = app::find_widget<Button>(this, "goforward");
  m_goUp = app::find_widget<Button>(this, "goup");
  m_location = app::find_widget<ComboBox>(this, "location");
  m_fileType = app::find_widget<ComboBox>(this, "filetype");
  m_fileName = app::find_widget<CustomFileNameEntry>(this, "filename");

  m_goBack->setFocusStop(false);
  m_goForward->setFocusStop(false);
  m_goUp->setFocusStop(false);

  set_gfxicon_to_button(m_goBack,
                        PART_COMBOBOX_ARROW_LEFT,
                        PART_COMBOBOX_ARROW_LEFT_SELECTED,
                        PART_COMBOBOX_ARROW_LEFT_DISABLED,
                        JI_CENTER | JI_MIDDLE);
  set_gfxicon_to_button(m_goForward,
                        PART_COMBOBOX_ARROW_RIGHT,
                        PART_COMBOBOX_ARROW_RIGHT_SELECTED,
                        PART_COMBOBOX_ARROW_RIGHT_DISABLED,
                        JI_CENTER | JI_MIDDLE);
  set_gfxicon_to_button(m_goUp,
                        PART_COMBOBOX_ARROW_UP,
                        PART_COMBOBOX_ARROW_UP_SELECTED,
                        PART_COMBOBOX_ARROW_UP_DISABLED,
                        JI_CENTER | JI_MIDDLE);

  setup_mini_look(m_goBack);
  setup_mini_look(m_goForward);
  setup_mini_look(m_goUp);

  m_fileList = new FileList();
  m_fileList->setId("fileview");
  view->attachToView(m_fileList);
  m_fileName->setAssociatedFileList(m_fileList);

  m_goBack->Click.connect(Bind<void>(&FileSelector::onGoBack, this));
  m_goForward->Click.connect(Bind<void>(&FileSelector::onGoForward, this));
  m_goUp->Click.connect(Bind<void>(&FileSelector::onGoUp, this));
  m_location->Change.connect(Bind<void>(&FileSelector::onLocationChange, this));
  m_fileType->Change.connect(Bind<void>(&FileSelector::onFileTypeChange, this));
  m_fileList->FileSelected.connect(Bind<void>(&FileSelector::onFileListFileSelected, this));
  m_fileList->FileAccepted.connect(Bind<void>(&FileSelector::onFileListFileAccepted, this));
  m_fileList->CurrentFolderChanged.connect(Bind<void>(&FileSelector::onFileListCurrentFolderChanged, this));
}

base::string FileSelector::show(const base::string& title,
                                const base::string& initialPath,
                                const base::string& showExtensions)
{
  base::string result;

  FileSystemModule::instance()->refresh();

  if (!navigation_history) {
    navigation_history = new FileItemList();
    App::instance()->Exit.connect(&on_exit_delete_navigation_history);
  }

  // we have to find where the user should begin to browse files (start_folder)
  base::string start_folder_path;
  IFileItem* start_folder = NULL;

  // If initialPath doesn't contain a path.
  if (base::get_file_path(initialPath).empty()) {
    // Get the saved `path' in the configuration file.
    base::string path = get_config_string("FileSelect", "CurrentDirectory", "");
    start_folder = FileSystemModule::instance()->getFileItemFromPath(path);

    // Is the folder find?
    if (!start_folder) {
      // If the `path' doesn't exist.
      if (path.empty() || (!FileSystemModule::instance()->dirExists(path))) {
        // We can get the current path from the system.
#ifdef HAVE_DRIVES
        int drive = _al_getdrive();
#else
        int drive = 0;
#endif
        char tmp[1024];
        _al_getdcwd(drive, tmp, sizeof(tmp) - ucwidth(OTHER_PATH_SEPARATOR));
        path = tmp;
      }

      start_folder_path = base::join_path(path, initialPath);
    }
  }
  else {
    // Remove the filename.
    start_folder_path = base::join_path(base::get_file_path(initialPath), "");
  }
  start_folder_path = base::fix_path_separators(start_folder_path);

  if (!start_folder)
    start_folder = FileSystemModule::instance()->getFileItemFromPath(start_folder_path);

  PRINTF("start_folder_path = %s (%p)\n", start_folder_path.c_str(), start_folder);

  jwidget_set_min_size(this, JI_SCREEN_W*9/10, JI_SCREEN_H*9/10);
  remapWindow();
  centerWindow();

  m_fileList->setExtensions(showExtensions.c_str());
  if (start_folder)
    m_fileList->setCurrentFolder(start_folder);

  // current location
  navigation_position.reset();
  addInNavigationHistory(m_fileList->getCurrentFolder());

  // fill the location combo-box
  updateLocation();
  updateNavigationButtons();

  // fill file-type combo-box
  m_fileType->removeAllItems();

  std::vector<base::string> tokens;
  std::vector<base::string>::iterator tok;

  base::split_string(showExtensions, tokens, ",");
  for (tok=tokens.begin(); tok!=tokens.end(); ++tok)
    m_fileType->addItem(tok->c_str());

  // file name entry field
  m_fileName->setText(base::get_file_name(initialPath).c_str());
  selectFileTypeFromFileName();
  m_fileName->selectText(0, -1);

  // setup the title of the window
  setText(title.c_str());

  // get the ok-button
  Widget* ok = this->findChild("ok");

  // update the view
  View::getView(m_fileList)->updateView();

  // open the window and run... the user press ok?
again:
  openWindowInForeground();
  if (getKiller() == ok ||
      getKiller() == m_fileList) {
    // open the selected file
    IFileItem* folder = m_fileList->getCurrentFolder();
    ASSERT(folder);

    base::string fn = m_fileName->getText();
    base::string buf;
    IFileItem* enter_folder = NULL;

    // up a level?
    if (fn == "..") {
      enter_folder = folder->getParent();
      if (!enter_folder)
        enter_folder = folder;
    }
    else if (!fn.empty()) {
      // check if the user specified in "fn" a item of "fileview"
      const FileItemList& children = m_fileList->getFileList();

      base::string fn2 = fn;
#ifdef WIN32
      fn2 = base::string_to_lower(fn2);
#endif

      for (FileItemList::const_iterator
             it=children.begin(); it!=children.end(); ++it) {
        IFileItem* child = *it;
        base::string child_name = child->getDisplayName();

#ifdef WIN32
        child_name = base::string_to_lower(child_name);
#endif
        if (child_name == fn2) {
          enter_folder = *it;
          buf = enter_folder->getFileName();
          break;
        }
      }

      if (!enter_folder) {
        // does the file-name entry have separators?
        if (base::is_path_separator(*fn.begin())) { // absolute path (UNIX style)
#ifdef WIN32
          // get the drive of the current folder
          base::string drive = folder->getFileName();
          if (drive.size() >= 2 && drive[1] == ':') {
            buf += drive[0];
            buf += ':';
            buf += fn;
          }
          else
            buf = base::join_path("C:", fn);
#else
          buf = fn;
#endif
        }
#ifdef WIN32
        // does the file-name entry have colon?
        else if (fn.find(':') != base::string::npos) { // absolute path on Windows
          if (fn.size() == 2 && fn[1] == ':') {
            buf = base::join_path(fn, "");
          }
          else {
            buf = fn;
          }
        }
#endif
        else {
          buf = folder->getFileName();
          buf = base::join_path(buf, fn);
        }
        buf = base::fix_path_separators(buf);

        // we can check if 'buf' is a folder, so we have to enter in it
        enter_folder = FileSystemModule::instance()->getFileItemFromPath(buf);
      }
    }
    else {
      // show the window again
      setVisible(true);
      goto again;
    }

    // did we find a folder to enter?
    if (enter_folder &&
        enter_folder->isFolder() &&
        enter_folder->isBrowsable()) {
      // enter in the folder that was specified in the 'm_fileName'
      m_fileList->setCurrentFolder(enter_folder);

      // clear the text of the entry widget
      m_fileName->setText("");

      // show the window again
      setVisible(true);
      goto again;
    }
    // else file-name specified in the entry is really a file to open...

    // does it not have extension? ...we should add the extension
    // selected in the filetype combo-box
    if (base::get_file_extension(buf).empty()) {
      buf += '.';
      buf += m_fileType->getItemText(m_fileType->getSelectedItemIndex());
    }

    // duplicate the buffer to return a new string
    result = buf;

    // save the path in the configuration file
    base::string lastpath = folder->getKeyName();
    set_config_string("FileSelect", "CurrentDirectory",
                      lastpath.c_str());
  }

  return result;
}

// Updates the content of the combo-box that shows the current
// location in the file-system.
void FileSelector::updateLocation()
{
  IFileItem* currentFolder = m_fileList->getCurrentFolder();
  IFileItem* fileItem = currentFolder;
  std::list<IFileItem*> locations;
  int selected_index = -1;
  int newItem;

  while (fileItem != NULL) {
    locations.push_front(fileItem);
    fileItem = fileItem->getParent();
  }

  // Clear all the items from the combo-box
  m_location->removeAllItems();

  // Add item by item (from root to the specific current folder)
  int level = 0;
  for (std::list<IFileItem*>::iterator it=locations.begin(), end=locations.end();
       it != end; ++it) {
    fileItem = *it;

    // Indentation
    base::string buf;
    for (int c=0; c<level; ++c)
      buf += "  ";

    // Location name
    buf += fileItem->getDisplayName();

    // Add the new location to the combo-box
    m_location->addItem(new CustomFileNameItem(buf.c_str(), fileItem));

    if (fileItem == currentFolder)
      selected_index = level;

    level++;
  }

  // Add paths from recent files list
  {
    newItem = m_location->addItem("");
    newItem = m_location->addItem("-------- Recent Paths --------");

    RecentFiles::const_iterator it = App::instance()->getRecentFiles()->paths_begin();
    RecentFiles::const_iterator end = App::instance()->getRecentFiles()->paths_end();
    for (; it != end; ++it)
      m_location->addItem(new CustomFolderNameItem(it->c_str()));
  }

  // Select the location
  {
    m_location->setSelectedItemIndex(selected_index);
    m_location->getEntryWidget()->setText(currentFolder->getDisplayName().c_str());
    m_location->getEntryWidget()->deselectText();
  }
}

void FileSelector::updateNavigationButtons()
{
  // Update the state of the go back button: if the navigation-history
  // has two elements and the navigation-position isn't the first one.
  m_goBack->setEnabled(navigation_history->size() > 1 &&
                       (navigation_position.isNull() ||
                        navigation_position.getIterator() != navigation_history->begin()));

  // Update the state of the go forward button: if the
  // navigation-history has two elements and the navigation-position
  // isn't the last one.
  m_goForward->setEnabled(navigation_history->size() > 1 &&
                          (navigation_position.isNull() ||
                           navigation_position.getIterator() != navigation_history->end()-1));

  // Update the state of the go up button: if the current-folder isn't
  // the root-item.
  IFileItem* currentFolder = m_fileList->getCurrentFolder();
  m_goUp->setEnabled(currentFolder != FileSystemModule::instance()->getRootFileItem());
}

void FileSelector::addInNavigationHistory(IFileItem* folder)
{
  ASSERT(folder != NULL);
  ASSERT(folder->isFolder());

  // Remove the history from the current position
  if (navigation_position.isValid()) {
    navigation_history->erase(navigation_position.getIterator()+1, navigation_history->end());
    navigation_position.reset();
  }

  // If the history is empty or if the last item isn't the folder that
  // we are visiting...
  if (navigation_history->empty() ||
      navigation_history->back() != folder) {
    // We can add the location in the history
    navigation_history->push_back(folder);
    navigation_position.setIterator(navigation_history->end()-1);
  }
}

void FileSelector::selectFileTypeFromFileName()
{
  base::string ext = base::get_file_extension(m_fileName->getText());

  if (!ext.empty()) {
    ext = base::string_to_lower(ext);
    m_fileType->setSelectedItemIndex(m_fileType->findItemIndex(ext.c_str()));
  }
}

void FileSelector::onGoBack()
{
  if (navigation_history->size() > 1) {
    if (navigation_position.isNull())
      navigation_position.setIterator(navigation_history->end()-1);

    if (navigation_position.getIterator() != navigation_history->begin()) {
      navigation_position.setIterator(navigation_position.getIterator()-1);

      navigation_locked = true;
      m_fileList->setCurrentFolder(*navigation_position.getIterator());
      navigation_locked = false;
    }
  }
}

void FileSelector::onGoForward()
{
  if (navigation_history->size() > 1) {
    if (navigation_position.isNull())
      navigation_position.setIterator(navigation_history->begin());

    if (navigation_position.getIterator() != navigation_history->end()-1) {
      navigation_position.setIterator(navigation_position.getIterator()+1);

      navigation_locked = true;
      m_fileList->setCurrentFolder(*navigation_position.getIterator());
      navigation_locked = false;
    }
  }
}

void FileSelector::onGoUp()
{
  m_fileList->goUp();
}

// Hook for the 'location' combo-box
void FileSelector::onLocationChange()
{
  // When the user change the location we have to set the
  // current-folder in the 'fileview' widget
  int itemIndex = m_location->getSelectedItemIndex();
  CustomFileNameItem* comboFileItem = dynamic_cast<CustomFileNameItem*>(m_location->getSelectedItem());
  IFileItem* fileItem = (comboFileItem != NULL ? comboFileItem->getFileItem(): NULL);

  // Maybe the user selected a recent file path
  if (fileItem == NULL) {
    CustomFolderNameItem* comboFolderItem =
      dynamic_cast<CustomFolderNameItem*>(m_location->getSelectedItem());

    if (comboFolderItem != NULL) {
      base::string path = comboFolderItem->getText();
      fileItem = FileSystemModule::instance()->getFileItemFromPath(path);
    }
  }

  if (fileItem != NULL) {
    m_fileList->setCurrentFolder(fileItem);

    // Refocus the 'fileview' (the focus in that widget is more
    // useful for the user)
    getManager()->setFocus(m_fileList);
  }
}

// When the user selects a new file-type (extension), we have to
// change the file-extension in the 'filename' entry widget
void FileSelector::onFileTypeChange()
{
  std::string newExtension = m_fileType->getItemText(m_fileType->getSelectedItemIndex());
  std::string fileName = m_fileName->getText();
  std::string currentExtension = base::get_file_extension(fileName);

  if (!currentExtension.empty()) {
    m_fileName->setText((fileName.substr(0, fileName.size()-currentExtension.size())+newExtension).c_str());
    m_fileName->selectText(0, -1);
  }
}

void FileSelector::onFileListFileSelected()
{
  IFileItem* fileitem = m_fileList->getSelectedFileItem();

  if (!fileitem->isFolder()) {
    base::string filename = base::get_file_name(fileitem->getFileName());

    m_fileName->setText(filename.c_str());
    selectFileTypeFromFileName();
  }
}

void FileSelector::onFileListFileAccepted()
{
  closeWindow(m_fileList);
}

void FileSelector::onFileListCurrentFolderChanged()
{
  if (!navigation_locked)
    addInNavigationHistory(m_fileList->getCurrentFolder());

  updateLocation();
  updateNavigationButtons();
}

} // namespace app

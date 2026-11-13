// Editing comfort commands (menu entries) and editor automation.
#pragma once

#include <cstdint>

class Editor;

namespace commands {

void bold();
void italic();
void strikethrough();
void inlineCode();
void codeBlock();
void headingMore();
void headingLess();
void bulletList();
void numberedList();
void task();
void quote();
void link();
void image();
void pasteImage();
void insertTable();
void formatTable();
void tableRowAbove();
void tableRowBelow();
void tableDeleteRow();
void tableColumnLeft();
void tableColumnRight();
void tableDeleteColumn();
void tableOfContents();

// Toggles the checkbox of the task list item in `line` (used by the preview).
void toggleTaskAtLine(const Editor& editor, intptr_t line);

// Continues lists / quotes after Enter was pressed (called right after the new line exists).
void smartEnter();

}  // namespace commands

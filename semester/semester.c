#include <locale.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "Scintilla.h"
#include "SciLexer.h"
#include "Lexilla.h"
#include "ScintillaTermbox.h"

#define SSM(m, w, l) scintilla_send_message(sci, m, w, l)

typedef void Scintilla;

void scnotification(Scintilla *view, int msg, SCNotification *n, void *userdata) {
  // printw("SCNotification received: %i", n->nmhdr.code);
}

int main(int argc, char **argv) {
  setlocale(LC_CTYPE, ""); // for displaying UTF-8 characters properly
  int ret = tb_init();
  if (ret) {
    fprintf(stderr, "tb_init() failed with error code %d\n", ret);
    return 1;
  }
  tb_select_output_mode(5);
  Scintilla *sci = scintilla_new(scnotification, NULL);
  SSM(SCI_STYLESETFORE, STYLE_DEFAULT, 0xd8d8d8);
  SSM(SCI_STYLESETBACK, STYLE_DEFAULT, 0x181818);
  SSM(SCI_STYLECLEARALL, 0, 0);
  SSM(SCI_SETCARETSTYLE, 2, 0);
//  SSM(SCI_SETILEXER, 0, (sptr_t)lexer("cpp"));
  ILexer5 *pLexer = CreateLexer("cpp");
  SSM(SCI_SETILEXER, 0, (sptr_t)pLexer);
  SSM(SCI_SETKEYWORDS, 0, (sptr_t) "int char");
  SSM(SCI_STYLESETFORE, SCE_C_COMMENT, 0x383838);
  SSM(SCI_STYLESETFORE, SCE_C_COMMENTLINE, 0x383838);
  SSM(SCI_STYLESETFORE, SCE_C_NUMBER, 0xdc9656);
  SSM(SCI_STYLESETFORE, SCE_C_WORD, 0xaf8bba);
  SSM(SCI_STYLESETFORE, SCE_C_STRING, 0x6cb5a1);
  SSM(SCI_STYLESETBOLD, SCE_C_OPERATOR, 1);
  // clang-format off
  SSM(SCI_INSERTTEXT, 0, (sptr_t)
      "int main(int argc, char **argv) {\n"
      "    // Start up the gnome\n"
      "    // 日本語でコメント\n"
      "    gnome_init(\"stest\", \"1.0\", argc, argv);\n}");
  // clang-format on
  SSM(SCI_SETPROPERTY, (uptr_t) "fold", (sptr_t) "1");
  SSM(SCI_SETMARGINWIDTHN, 0, 2);
  SSM(SCI_SETMARGINWIDTHN, 2, 1);
  SSM(SCI_SETMARGINMASKN, 2, SC_MASK_FOLDERS);
  SSM(SCI_SETMARGINSENSITIVEN, 2, 1);
  SSM(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_CLICK, 0);
  SSM(SCI_SETFOCUS, 1, 0);
  scintilla_refresh(sci);

struct tb_event ev;
int c;
while (tb_poll_event(&ev))
  {
    c = 0;
    switch (ev.type)
    {
    case TB_EVENT_KEY:
      c = ev.ch;
      switch (ev.key)
      {
        case TB_KEY_ESC:
          goto done;
          break;
        case TB_KEY_ARROW_UP:
          c = SCK_UP;
          break;
        case TB_KEY_ARROW_DOWN:
          c = SCK_DOWN;
          break;
        case TB_KEY_ARROW_LEFT:
          c = SCK_LEFT;
          break;
        case TB_KEY_ARROW_RIGHT:
          c = SCK_RIGHT;
          break;
        case TB_KEY_DELETE:
          c = SCK_DELETE;
          break;
        case TB_KEY_ENTER:
          c = SCK_RETURN;
          break;
        case TB_KEY_CTRL_A:
          scintilla_resize(sci, 40, 20);
          break;
        case TB_KEY_CTRL_B:
          scintilla_move(sci, 10, 5);
          break;
        case TB_KEY_CTRL_C:
          SSM(SCI_AUTOCSHOW, 0, "abc opq xyz 01234567890 xxx xxx xxx xxx xxx");
          break;
        case TB_KEY_CTRL_D:
          SSM(SCI_AUTOCSETMAXHEIGHT, 16, 0);
          break;
        default:
          break;
      }
      break;

      case TB_EVENT_RESIZE:

      break;
    }
    if (c != 0) {
      scintilla_send_key(sci, c, 0, 0, 0);
      scintilla_refresh(sci);
    }
  }

done:
  tb_shutdown();
}

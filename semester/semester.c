#include <locale.h>
#include <sys/time.h>
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
  tb_select_input_mode(1 | 4);
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
  SSM(SCI_STYLESETITALIC, SCE_C_COMMENT, true);
  SSM(SCI_STYLESETFORE, SCE_C_COMMENTLINE, 0x383838);
  SSM(SCI_STYLESETITALIC, SCE_C_COMMENTLINE, true);
  SSM(SCI_STYLESETFORE, SCE_C_NUMBER, 0xdc9656);
  SSM(SCI_STYLESETFORE, SCE_C_WORD, 0xaf8bba);
  SSM(SCI_STYLESETFORE, SCE_C_STRING, 0x6cb5a1);
  SSM(SCI_STYLESETBOLD, SCE_C_OPERATOR, 1);
  SSM(SCI_STYLESETBACK, 253, 0x0000ff);
  SSM(SCI_STYLESETFORE, 253, 0xffffff);
  // clang-format off
  SSM(SCI_INSERTTEXT, 0, (sptr_t)
      "int main(int argc, char **argv) {\n"
      "    // Start up the gnome\n"
      "    // 日本語でコメント\n"
      "\tgnome_init(\"stest\", \"1.0\", argc, argv);\n}");
  // clang-format on
  SSM(SCI_SETPROPERTY, (uptr_t) "fold", (sptr_t) "1");
  SSM(SCI_SETMARGINWIDTHN, 0, 2);
  SSM(SCI_SETMARGINWIDTHN, 2, 2);
  SSM(SCI_SETMARGINMASKN, 2, SC_MASK_FOLDERS);
  SSM(SCI_SETMARGINSENSITIVEN, 2, 1);
  SSM(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_CLICK, 0);
//  SSM(SCI_SETVSCROLLBAR, 1, 1);
//  SSM(SCI_SETHSCROLLBAR, 1, 1);
  SSM(SCI_SETINDENTATIONGUIDES, 2, 2);
  SSM(SCI_SETHIGHLIGHTGUIDE, 1, 1);
  SSM(SCI_INDICSETFORE, 0, 0x007f00);
  SSM(SCI_SETINDICATORVALUE, 0, 0);
  SSM(SCI_SETINDICATORCURRENT, 0, 0);
  SSM(SCI_INDICATORFILLRANGE, 1, 5);
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
          scintilla_move(sci, 10, 19);
          break;
        case TB_KEY_CTRL_C:
          SSM(SCI_AUTOCSHOW, 0, "abc opq xyz 01234567890 xxx xxx xxx xxx xxx");
          break;
        case TB_KEY_CTRL_D:
          SSM(SCI_AUTOCSETMAXHEIGHT, 16, 0);
          break;
        case TB_KEY_CTRL_E:
          SSM(SCI_SETVSCROLLBAR, 0, 0);
          break;
        case TB_KEY_CTRL_F:
          SSM(SCI_SETVSCROLLBAR, 1, 0);
          break;
        case TB_KEY_CTRL_G:
          SSM(SCI_CALLTIPSHOW, 40, "hoge");
          break;
        case TB_KEY_CTRL_H:
        fprintf(stderr, "ctrl-h\n");
          SSM(SCI_ANNOTATIONSETTEXT, 2, "hogehoge\n\nabc");
        SSM(SCI_ANNOTATIONSETSTYLE, 2, 253);
        SSM(SCI_ANNOTATIONSETVISIBLE, 3, 0);
          break;
        default:
          break;
      }
      if (c != 0) {
        scintilla_send_key(sci, c, 0, 0, 0);
        scintilla_refresh(sci);
      }
      break;

      case TB_EVENT_RESIZE:
      break;
      case TB_EVENT_MOUSE:
      {
        struct timeval time = {0, 0};
        gettimeofday(&time, NULL);
        int event = 1;
        int millis = time.tv_sec * 1000 + time.tv_usec / 1000;
         if (ev.mod == 2) {
          event = 2;
        } else if (ev.key == TB_KEY_MOUSE_RELEASE) {
          event = 3;
        }
        scintilla_send_mouse(sci, event, millis, 1, ev.y, ev.x, false, false, false);
        scintilla_refresh(sci);
      }
     break;
    }
  }

done:
  tb_shutdown();
}

// KILO FOR WINDOWS

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <time.h>
#include <windows.h>

/*** DEFINES ***/

#define KILO_VERSION "WINKILO:1.0.0"
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3

// Strips off 3 highest bits of input char (just like ctrl-)
#define CTRL_KEY(k) ((k) & 0x1f)
// Append buffer "constructor"
#define ABUF_INIT {NULL, 0}
// Escape code key
#define ESC '\x1b'

// Highlight flags
#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

// Highlight colors
enum colorCodes {
  BLACK=30,
  RED,
  GREEN,
  YELLOW,
  BLUE,
  PURPLE,
  CYAN,
  WHITE,
  // HIGH INTENSITY COLORS
  HI_BLACK=90,
  HI_RED,
  HI_GREEN,
  HI_YELLOW,
  HI_BLUE,
  HI_PURPLE,
  HI_CYAN,
  HI_WHITE
};



enum editorKey {
  BACKSPACE = 127, // backspace doesn't have backslash code for string literals :(
  // Multi-key escape sequences are assigned values > 256 (size of char datatype)
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

enum editorHighlight {
  HL_NORMAL = 0,
  HL_COMMENT,
  HL_MLCOMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_STRING,
  HL_NUMBER,
  HL_MATCH
};

/*** DATA ***/

struct editorSyntax {
  char *filetype;
  char **filematch;
  char **keywords;
  char *singleline_comment_start;
  char *multiline_comment_start;
  char *multiline_comment_end;
  int flags;
};

typedef struct erow {
  int idx; // row number (used to check values in previous erow)
  int size;
  int rsize;
  char *chars;   // characters typed in
  char *render; // rendered chars (tabs to spaces)
  unsigned char *hl; // highlights
  int hl_open_comment;
} erow;

// Contains editor state
struct editorConfig {
  int cx, cy; // cursor coordinates into erow.chars
  int rx;    // cursor x into erow.render: same as cx when no tabs, else rx > cx
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  int dirty; // flag for whether file has been modified since last open/save
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct editorSyntax *syntax;
  // IO handlers
  HANDLE in_handle;
  HANDLE out_handle;
  // terminal reset state
  DWORD og_terminal_in_state;
  DWORD og_terminal_out_state;
};

// abuf is just an appendable string with an easy-to-access len (instead of reading until 0)
struct abuf {
  char *b;
  int len;
};

/*** FUNCTION PROTOTYPES ***/

void clearScreen();
int editorReadKey();
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

/*** CUSTOMIZATION ***/

/* FULL FUNCTIONS */
// I know this isn't the "correct" way to use a .h file, but putting this her
//  allows for easier customization, rather than searching through the .c file

// Takes highlight enum and returns the corresponding ANSI color code value
// See tables in https://gist.github.com/JBlond/2fea43a3049b38287e5e9cefc87b2124
int editorSyntaxToColor(int hl) {
  switch (hl) {
    case HL_COMMENT:
    case HL_MLCOMMENT: return CYAN;
    case HL_KEYWORD1: return HI_YELLOW;
    case HL_KEYWORD2: return GREEN;
    case HL_STRING: return HI_PURPLE;
    case HL_NUMBER: return RED;
    case HL_MATCH: return HI_BLUE;
    default: return WHITE;
  }
}

/* FILETYPE HIGHTLIGHTING */
char *C_HL_extensions[] = {".c", ".h", ".cpp", ".hpp", ".cc", NULL};
// KEYWORD2 words end in |
// For c: kw1 are general keywords, kw2 are types
char *C_HL_keywords[] = {
  "auto","break","case","continue","default","do","else","enum",
  "extern","for","goto","if","register","return","sizeof","static",
  "struct","switch","typedef","union","volatile","while","NULL",
  "#define", "#include",

  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", NULL
};

// Highlight database
struct editorSyntax HLDB[] = {
  {
    "c",
    C_HL_extensions,
    C_HL_keywords,
    "//", "/*", "*/",
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
  }
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))
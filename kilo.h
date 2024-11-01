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

/*** CUSTOMIZATION ***/

// Matches highlight cases w/ color code
enum editorHighlight {
  HL_NORMAL = WHITE,
  HL_COMMENT = CYAN,
  HL_MLCOMMENT = CYAN,
  HL_KEYWORD1 = HI_YELLOW,
  HL_KEYWORD2 = HI_GREEN,
  HL_STRING = HI_PURPLE,
  HL_NUMBER = RED,
  HL_MATCH = HI_BLUE
};

/* Highlight language database */
char *C_HL_extensions[] = {".c", ".h", ".cpp", ".hpp", ".cc", NULL};
// KEYWORD2 words end in |
// For c: kw1 are general keywords, kw2 are types
char *C_HL_keywords[] = {
  "auto","break","case","continue","default","do","else",
  "extern","for","goto","if","register","return","sizeof","static",
  "switch","typedef","union","volatile","while","NULL",
  "#define", "#include",

  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", "const|", "enum|", "struct|", NULL
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

/*** FUNCTION PROTOTYPES ***/

// Error handling
void die(const char *s);

/*** TERMINAL STATE ***/
void disableRawMode();
void enableRawMode();
int getCursorPosition(int *rows, int *cols);
int getWindowSize(int *rows, int *cols);

/*** SYNTAX HIGHLIGHTING ***/
int is_separator(int c);
void editorUpdateSyntax(erow *row);
void editorSelectSyntaxHighlight();

/*** ROW OPERATIONS ***/
int editorRowCxToRx(erow *row, int cx);
int editorRowRxToCx(erow *row, int rx);
void editorUpdateRow(erow *row);
void editorInsertRow(int at, char *s, size_t len);
void editorFreeRow(erow *row);
void editorRowInsertChar(erow *row, int at, int c);
void editorRowAppendString(erow *row, char *s, size_t len);
void editorRowDelChar(erow *row, int at);

/*** EDITOR OPERATIONS ***/
void editorInsertChar(int c);
void editorInsertNewline();
void editorDelChar();

/*** FILE IO ***/
char *editorRowsToString(int *buflen);
void editorOpen(char *filename);
void editorSave();

/*** FIND ***/
void editorFindCallback(char *query, int key);
void editorFind();

/*** APPEND BUFFER ***/
void abAppend(struct abuf *ab, const char *s, int len);
char *editorPrompt(char *prompt, void (*callback)(char *, int));
void editorMoveCursor(int key);
int editorReadBytes(HANDLE handle, char *pc, int max_bytes);
int editorReadKey();
void editorProcessKeypress();

/*** OUTPUT ***/
void editorScroll();
void clearScreen();
void editorDrawRows(struct abuf *ab);
void editorDrawStatusBar(struct abuf *ab);
void editorDrawMessageBar(struct abuf *ab);
void editorRefreshScreen();
void editorSetStatusMessage(const char *fmt, ...);

/*** INIT ***/
void initEditor();
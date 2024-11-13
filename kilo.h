// KILO FOR WINDOWS

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <time.h>
#include <windows.h>

/*** DEFINES ***/

#define KILO_VERSION "WINKILO:1.1.0"
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
  PAGE_DOWN,
  SHIFT_ARROW_LEFT,
  SHIFT_ARROW_RIGHT,
  SHIFT_ARROW_UP,
  SHIFT_ARROW_DOWN,
  CTRL_ARROW_LEFT,
  CTRL_ARROW_RIGHT,
  CTRL_ARROW_UP,
  CTRL_ARROW_DOWN,
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
  int rowoff;  // index of top-drawn row
  int coloff;
  int screenrows; // number of rows available to draw on in console
  int screencols;
  int numrows; // len of `row` array
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
  COORD og_terminal_size;

  // selections: for simplicity, we only allow one contiguous space of selected text for now
  struct textSelection *selections;
  int numselections;
};

struct textSelection {
  // indices matching E.cx/cy (rather than rx)
  // taily >= heady; if taily == heady: headx <= tailx
  // if head == tail, this is equivalent to their being no selection (just the cursor)
  int headx, heady, tailx, taily;
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

char *ASM6502_HL_extensions[] = {".asm", NULL};

// TODO: color numbers by type (bin, dec, 0x, literal/addr)
// TODO: assembler instructions (. commands)
char *ASM6502_HL_keywords[] = {
  "ADC", "AND", "ASL", "BIT", "CLC", "CLD", "CLI", "CLV", "CMP", "CPX", "CPY",
  "DEC", "DEX", "DEY", "EOR", "INC", "INX", "INY", "LDA", "LDX", "LDY", "LSR",
  "NOP", "ORA", "PHA", "PHP", "PLA", "PLP", "ROL", "ROR", "SBC",
  "SEC", "SED", "SEI", "STA", "STX", "STY", "TAX", "TAY", "TSX", "TXA", "TXS", "TYA",

   // Control flow operations as 2nd keyword highlights (jumps/branches,interupts)
  "BCC|", "BCS|", "BEQ|", "BMI|", "BNE|", "BPL|", "BRK|", "BVC|", "BVS|", "JMP|",
  "JSR|", "RTI|", "RTS|",
  NULL
};

// Highlight database
struct editorSyntax HLDB[] = {
  {
    "c",
    C_HL_extensions,
    C_HL_keywords,
    "//", "/*", "*/",
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
  },
  {
    "ASM 6502",
    ASM6502_HL_extensions,
    ASM6502_HL_keywords,
    ";", "", "",
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
void matchScreenBufferToWindow();
void restoreOriginalScreenBufferSize();

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
int editorMatchSpaces(erow *row_src, erow *row_dst);
void editorInsertChar(int c);
void editorInsertNewline(int match_spaces);
void editorDelChar();

/*** FILE IO ***/
char *editorRowsToString(int *buflen);
void editorOpen(char *filename);
void editorSave();

/*** FIND ***/
void editorFindCallback(char *query, int key);
void editorFind();
void editorJumpCallback(char *query, int key);
void editorJump();

/*** SELECTION ***/
struct textSelection canonicalSelection(struct textSelection *sel);
int isInSelection(int row, int col);
char *selectionToString(int *buflen);
void copySelectionToClipboard();

/*** APPEND BUFFER ***/
void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);

/*** INPUT ***/
char *editorPrompt(char *prompt, int numeric, void (*callback)(char *, int));
void editorMoveCursor(int key, int shift_pressed);
int editorReadEvents(HANDLE handle, char *pc, int n_records);
int editorReadKey();
void editorProcessKeypress();
void editorPasteFromClipboard();

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

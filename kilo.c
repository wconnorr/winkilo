/* 
  KILO EDITOR FOR WINDOWS

  Based on kilo editor by antirez (https://github.com/antirez/kilo)
  Editor features are based on kilo tutorial at: https://viewsourcecode.org/snaptoken/kilo/

  TODO:
    - autoindentaton
    - undo/redo
    - cut/copy/paste (requires mouse input for selection)
    - auto-resize for window
    - jump to line
*/

/*** INCLUDES ***/

#include "kilo.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
/* Includes in header file:
#include <time.h>
#include <windows.h>
*/

struct editorConfig E;

/*** ERROR HANDLING ***/

// Error handling; prints error info and message and kills program
void die(const char *s) {
  clearScreen();
  perror(s);
  exit(1);
}

/*** TERMINAL STATE ***/

// Returns terminal to original state
// Set to call at program exit
void disableRawMode() {
  if (!SetConsoleMode(E.in_handle, E.og_terminal_in_state))
    die("SetConsoleMode");
  if (!SetConsoleMode(E.out_handle, E.og_terminal_out_state))
    die("SetConsoleMode");
}

// Makes terminal process inputs w/o echoing or control codes
void enableRawMode() {
  // Get console mode
  if (!GetConsoleMode(E.in_handle, &E.og_terminal_in_state) ||
      !GetConsoleMode(E.out_handle, &E.og_terminal_out_state))
    die("GetConsoleMode");
  atexit(disableRawMode);

  // Set console input mode
  DWORD terminal_in_state = E.og_terminal_in_state;
  
  // Allow all inputs to be read by application instead of echoing or preprocessing
  // Note: some of these are on/off by default: we are being explicit with all options
  // DISABLE:
  terminal_in_state &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_INSERT_MODE );
  // ENABLE:
  // Enable VT commands
  terminal_in_state |= ENABLE_VIRTUAL_TERMINAL_INPUT |
  // Allow mouse selection (for copy command, TODO: mouse inputs are currently ignored)
                       ENABLE_MOUSE_INPUT | ENABLE_QUICK_EDIT_MODE |
  // Report window resizes (TODO: currently unused)
                       ENABLE_WINDOW_INPUT;

  if (!SetConsoleMode(E.in_handle, terminal_in_state))
    die("SetConsoleMode");
    
  // Set console output mode
  DWORD terminal_out_state = E.og_terminal_out_state;
  // Some of these are set by default, but we set them explicitly just in case
  terminal_out_state |= DISABLE_NEWLINE_AUTO_RETURN | ENABLE_PROCESSED_OUTPUT |
                        ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  terminal_out_state &= ~(ENABLE_WRAP_AT_EOL_OUTPUT | ENABLE_LVB_GRID_WORLDWIDE);
  
  if (!SetConsoleMode(E.out_handle, terminal_out_state))
    die("SetConsoleMode");      
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1; // query cursor position

  int i;
  // cursor position returned as 27']'<HEIGHT>';'<WIDTH>'R'
  for (i = 0; i < sizeof(buf); i++) {
    if(read(STDIN_FILENO, &buf[i], 1) != 1) break; // read in queried cursor position
    if (buf[i] == 'R') break;
  }
  buf[i] = '\0';

  if (buf[0] != ESC || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

  return 0;
}

// Returns terminal window dimensions; reading cursor position in bottom-right corner.
int getWindowSize(int *rows, int *cols) {
  // TODO: Try to get the information the "correct" way
  if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1; // send cursor to bottom right of screen
    return getCursorPosition(rows, cols);
}

/*** SYNTAX HIGHLIGHTING ***/

int is_separator(int c) {
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editorUpdateSyntax(erow *row) {
  // reset highlighting to match num rendered chars
  row->hl = realloc(row->hl, row->rsize);
  memset(row->hl, HL_NORMAL, row->rsize);

  if (E.syntax == NULL) return;

  char **keywords = E.syntax->keywords;

  char *scs = E.syntax->singleline_comment_start;
  char *mcs = E.syntax->multiline_comment_start;
  char *mce = E.syntax->multiline_comment_end;

  int scs_len = scs ? strlen(scs) : 0;
  int mcs_len = mcs ? strlen(mcs) : 0;
  int mce_len = mce ? strlen(mce) : 0;

  int prev_sep = 1; // previous character was a separator (whitespace, operator, etc)
  char in_string = 0; // 0 when not in string, else value of string character
  int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

  int i = 0;
  while (i < row->rsize) {
    char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i-1] : HL_NORMAL;

    // comments have highest priority (as long as we aren't in a string or multiline comment)
    if (scs_len && !in_string && !in_comment) {
      if (!strncmp(&row->render[i], scs, scs_len)) {
        memset(&row->hl[i], HL_COMMENT, row->rsize-i); // entire rest of row is a comment
        break;
      }
    }

    if (mcs_len && mce_len && !in_string) {
      if (in_comment) {
        row->hl[i] = HL_MLCOMMENT;
        if (!strncmp(&row->render[i], mce, mce_len)) {
          memset(&row->hl[i], HL_MLCOMMENT, mce_len);
          i += mce_len;
          in_comment = 0;
          prev_sep = 1;
          continue;
        } else {
          i++;
          continue;
        }
      } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
        memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
        i += mcs_len;
        in_comment = 1;
        continue;
      }
    }

    if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
      if (in_string) {
        row->hl[i] = HL_STRING;
        // handle escaped quotes
        if (c == '\\' && i+1 < row->rsize) {
          row->hl[i+1] = HL_STRING;
          i += 2;
          continue;
        }
        if (c == in_string) in_string = 0;
        i++;
        prev_sep = 1;
        continue;
      } else {
        if (c == '"' || c == '\'') {
          in_string = c;
          row->hl[i] = HL_STRING;
          i++;
          continue;
        }
      }
    }
    
    // numbers should have lower precedent than strings/comments
    if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
      if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
          (c == '.' && prev_hl == HL_NUMBER)) {
        row->hl[i] = HL_NUMBER;
        i++;
        prev_sep = 0;
        continue;
      }
    }

    // Keywords (1 and 2)
    if (prev_sep) {
      int j;
      for (j = 0; keywords[j]; j++) {
        int klen = strlen(keywords[j]);
        int kw2 = keywords[j][klen - 1] == '|';
        if (kw2) klen--; // KW2 end in | in the database

        if (!strncmp(&row->render[i], keywords[j], klen) &&
            is_separator(row->render[i+klen])) {
          memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
          i += klen;
          break;
        }
      }
      if (keywords[j] != NULL) { // if break reached:
        prev_sep = 0;
        continue;
      }
    }

    prev_sep = is_separator(c);
    i++;
  }

  int changed = (row->hl_open_comment != in_comment);
  row->hl_open_comment = in_comment;
  if (changed && row->idx + 1 < E.numrows)
    editorUpdateSyntax(&E.row[row->idx + 1]);
}

void editorSelectSyntaxHighlight() {
  E.syntax = NULL;
  if (E.filename == NULL) return;

  char *ext = strrchr(E.filename, '.'); // returns pointer to last '.' in filename

  for (unsigned int j=0; j < HLDB_ENTRIES; j++) {
    struct editorSyntax *s = &HLDB[j];
    for (unsigned int i = 0; s->filematch[i]; i++) {
      int is_ext = (s->filematch[i][0] == '.');

      if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
          (!is_ext && strstr(E.filename, s->filematch[i]))) {
        E.syntax = s;

        // Apply new syntax highlighting style
        for (int filerow = 0; filerow < E.numrows; filerow++)
          editorUpdateSyntax(&E.row[filerow]);

        return;
      }
    }
  }
}

/*** ROW OPERATIONS ***/

// Takes special chars ('\t') into account to translate from memory characters to rendered graphemes
int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  for (int j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
    rx++;
  } 
  return rx;
}

int editorRowRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
    cur_rx++;

    if (cur_rx > rx) break;
  }
  return cx;
}

void editorUpdateRow(erow *row) {
  int tabs = 0;
  for (int j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;

  free(row->render);
  row->render = malloc(row->size + tabs*(KILO_TAB_STOP-1) + 1); // assume each tab takes up max space

  int idx = 0;
  for (int j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      // Write each tab as spaces up to the next tab stop
      row->render[idx++] = ' ';
      while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
    } else
      row->render[idx++] = row->chars[j];
  }
  row->render[idx] = '\0';
  row->rsize = idx;

  editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows) return;

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows-at));
  for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++; // fix index stored at each row

  E.row[at].idx = at;

  E.row[at].size = len;
  E.row[at].chars = malloc(len+1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  E.row[at].hl = NULL;
  E.row[at].hl_open_comment = 0;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
  E.dirty = 1;
}

void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
  free(row->hl);
}

// Deletes row 'at' and moves up all the following rows
void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows) return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at+1], sizeof(erow) * (E.numrows - at - 1));
  for (int j = at; j < E.numrows-1; j++) E.row[j].idx--; // fix index stored at each row
  E.numrows--;
  E.dirty = 1;
}

// Inserts character into given row at given position.
void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  E.dirty = 1;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty = 1;
}

// Deletes character at given space
void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at+1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty = 1;
}

/*** EDITOR OPERATIONS ***/

// Inserts character at cursors. If on line past EOF, it creates a new row.
void editorInsertChar(int c) {
  if (E.cy == E.numrows) {
    editorInsertRow(E.numrows, "", 0); // append row at end
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

void editorInsertNewline() {
  if (E.cx == 0)
    editorInsertRow(E.cy, "", 0); // just insert new line
  else {
    // insert new line with everything from cx onward on it
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
}

// Deletes character less of cursor (backspace)
void editorDelChar() {
  if (E.cy == E.numrows) return;
  if (E.cx == 0 && E.cy == 0) return;

  erow *row = &E.row[E.cy];
  if (E.cx > 0)
    editorRowDelChar(row, --E.cx);
  else {
    // Delete line and move data to previous line
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDelRow(E.cy--);
  }
}

/*** FILE IO ***/

char *editorRowsToString(int *buflen) {
  int totlen = 0;
  for (int i = 0; i < E.numrows; i++)
    totlen += E.row[i].size + 1;
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  for (int i = 0; i < E.numrows; i++) {
    memcpy(p, E.row[i].chars, E.row[i].size);
    p += E.row[i].size;
    *p = '\n';
    p++;
  }

  return buf;
}

void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);

  editorSelectSyntaxHighlight(); // recompute syntax style whenever new file is opened

  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen-1] == '\n' || line[linelen-1]=='\r'))
      linelen--;
    editorInsertRow(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  E.dirty = 0;
}

void editorSave() {
  if (E.filename == NULL) {
    E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
    if (E.filename == NULL) {
      editorSetStatusMessage("Save aborted");
      return;
    }
    editorSelectSyntaxHighlight(); // recompute syntax style when new filename is saved
  }

  int len;
  char *buf = editorRowsToString(&len);

  int fd = open(E.filename, O_RDWR | O_CREAT, 0644); // Open file - read/write, create if doesn't exist, w/ file permissions 0644
  if (fd != -1) { // valid file open
    // Change file size to len (we do this before write instead of w/ open flag to save a bit from failed write)
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** FIND ***/

// Searches at each keypress
void editorFindCallback(char *query, int key) {
  static int last_match = -1;
  static int direction = 1;

  // Highlight reset vars
  static int saved_hl_line;
  static char *saved_hl = NULL;

  // Undo previous highlight at each change in the search (including cancelling) 
  if (saved_hl) {
    memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
    free(saved_hl);
    saved_hl = NULL;
  }

  if (key == '\r' || key == ESC) {
    last_match = -1;
    direction = 1;
    return;
  } else if (key == ARROW_RIGHT || key == ARROW_DOWN)
    direction = 1;
  else if (key == ARROW_LEFT || key == ARROW_UP)
    direction = -1;
  else {
    last_match = -1;
    direction = 1;
  }

  if (last_match == -1) direction = 1;
  int current = last_match;
  for (int i = 0; i < E.numrows; i++) {
    // search forward by default, search backward when left or up is pressed
    current += direction;
    // wrap around behavior in both directions
    if (current == -1) current = E.numrows-1;
    else if (current == E.numrows) current = 0;


    erow *row = &E.row[current];
    char *match = strstr(row->render, query); // finds index of substring
    if (match) {
      last_match = current;
      E.cy = current;
      E.cx = editorRowRxToCx(row, match - row->render);
      E.rowoff = E.numrows;

      // Store OG highlighting before we change highlighting
      saved_hl_line = current;
      saved_hl = malloc(row->rsize);
      memcpy(saved_hl, row->hl, row->rsize);
      memset(&row->hl[match - row->render], HL_MATCH, strlen(query)); // highlight match
      break;
    }
  }
}

// Creates prompt and begins search query
void editorFind() {
  // Save position to return to if search is cancelled
  int saved_cx = E.cx;
  int saved_cy = E.cy;
  int saved_coloff = E.coloff;
  int saved_rowoff = E.rowoff;

  char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);

  if (query) {
    free(query);
  } else {
    E.cx = saved_cx;
    E.cy = saved_cy;
    E.coloff = saved_coloff;
    E.rowoff = saved_rowoff;
  }
}

/*** APPEND BUFFER ***/

// Append string s of length len to buffer
void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len+len);
  
  if (new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

// Destructs append buffer
void abFree(struct abuf *ab) {
  free(ab->b);
}

/*** INPUT ***/

// Opens prompt and handles text input: if callback is not NULL, performs at each keypress
char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);

  size_t buflen = 0;
  buf[0] = '\0';

  while (1) {
    // Loop until ENTER is pressed (w/o empty inputs)
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();

    int c = editorReadKey();
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0) buf[--buflen] = '\0';
    } else if (c == ESC) {
      // Leave prompt w/o performing it
      editorSetStatusMessage("");
      if (callback) callback(buf, c);
      free(buf);
      return NULL;
    } else if (c == '\r') {
      if (buflen != 0) {
        editorSetStatusMessage("");
        if (callback) callback(buf, c);
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) { // non-control 'char' character
      if (buflen == bufsize-1) {
        // Increase buffer size as needed
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }

    if (callback) callback(buf, c);
  }
}

void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
    case ARROW_LEFT:
      if (E.cx != 0) 
        E.cx--;
      // if scroll left at 0, go to end of previous line
      else if (E.cy > 0)
        E.cx = E.row[--E.cy].size;
      break;
    case ARROW_RIGHT:
      // Only scroll right until end-of-line
      if (row && E.cx < row->size)
        E.cx++;
      // If past EOL, go to 0 at next line
      else if (row && E.cx == row->size) { 
        E.cy++;
        E.cx = 0;
      }
      break;
    case ARROW_UP:
      if (E.cy != 0)
        E.cy--;
      break;
    case ARROW_DOWN:
      if (E.cy < E.numrows)
        E.cy++;
      break;
  }

  // Snap cursor to end-of-line if it is past it
  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen)
    E.cx = rowlen;
}

// Reads a single keypress (up to max_bytes) with 0.1 second timeout
// Returns num bytes read, 0 on timeout
// If read or wait fails, dies
int editorReadBytes(HANDLE handle, char *pc, int max_bytes) {
  DWORD wait_ret = WaitForSingleObject(handle, 100);
  if (wait_ret == WAIT_TIMEOUT)
    return 0; // timeout
  else if (wait_ret == WAIT_OBJECT_0) {
    // char to read is in buffer
    long unsigned int nread;
    if(!ReadConsole(E.in_handle, pc, max_bytes, &nread, NULL) || nread < 1)
      die("read"); // read failed
    return (int)nread;
  } else {
    // wait failed
    die("WaitForSingleObject (reading bytes)");
  }
}

// Blocks until a single keypress is read in
// Returns an int because escape sequences will be mapped to a single value rather than multiple chars
int editorReadKey() {
  int nread;
  char buf[4];

  while((nread = editorReadBytes(E.in_handle, buf, 4)) == 0); // read until non-timeout event

  // char errorMessage[64];
  // sprintf(errorMessage, "read returned %d bytes", nread);
  // die(errorMessage);

  char c = buf[0];

  // read in escape sequence
  if (c == ESC) {

    // If ESC is pressed, only 1 byte is written
    if (nread == 1) return ESC;

    // If is escape sequence (ESC[ + at least 1 byte):
    if (nread > 2 && buf[1] == '[') {
      // If 1st byte is numeric, read next byte
      if (nread == 4 && buf[2] >= '0' && buf[2] <='9' && buf[3] == '~') {
        switch (buf[2]) {
          case '1': return HOME_KEY; // home and end have different codes for different OS
          case '3': return DEL_KEY;
          case '4': return END_KEY;
          case '5': return PAGE_UP;
          case '6': return PAGE_DOWN;
          case '7': return HOME_KEY;
          case '8': return END_KEY;
        }
      }
      // Single-byte escape sequence
      switch (buf[2]) {
        case 'A': return ARROW_UP;
        case 'B': return ARROW_DOWN;
        case 'C': return ARROW_RIGHT;
        case 'D': return ARROW_LEFT;
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    } else if (buf[1] == 'O') {
      switch (buf[2]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }
    // Default to returning ESC key
    return ESC;
  }
  return c;
}

// Gets keypress and performs corresponding action
void editorProcessKeypress() {
  static int quit_times = KILO_QUIT_TIMES;

  int c = editorReadKey();

  switch(c) {
    case '\r': // ENTER key
      editorInsertNewline();
      break;

    case CTRL_KEY('q'):
      if (E.dirty && quit_times > 0) {
        editorSetStatusMessage("WARNING!!! File has unsaved changes. "
                               "Press Ctrl-Q %d more times to quit.", quit_times);
        quit_times--;
        return;
      }
      clearScreen();
      exit(0);

    case CTRL_KEY('s'):
      editorSave();
      break;

    case HOME_KEY:
      E.cx = 0;
      break;

    case END_KEY:
      if (E.cy < E.numrows)
        E.cx = E.row[E.cy].size;
      break;

    case CTRL_KEY('f'):
      editorFind();
      break;

    case BACKSPACE:
    case CTRL_KEY('h'): // Old-timey backspace escape
    case DEL_KEY:
      // If delete: delete next char; else delete previous char
      if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
      editorDelChar();
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      if (c == PAGE_UP)
        E.cy = E.rowoff;
      else { // PAGE_DOWN
        E.cy = E.rowoff + E.screenrows-1;
        if (E.cy > E.numrows) E.cy = E.numrows;
      }

      int times = E.screenrows; // do it this way for scrolling
      while (times--)
        editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_RIGHT:
    case ARROW_LEFT:
      editorMoveCursor(c);
      break;

    case CTRL_KEY('l'): // Refresh screen - already done after any keypress
    case ESC:          // Any escape sequence we aren't processing (default return of editorReadKey())
      break;

    default:
      editorInsertChar(c);
      break;
  }

  quit_times = KILO_QUIT_TIMES;
}

/*** OUTPUT ***/

void editorScroll() {
  E.rx = 0;
  if (E.cy < E.numrows)
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);

  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  } else if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  } else if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols+1;
  }
}

// Return screen to blank
void clearScreen() {
  // NOTE: VT100 is largely supported by terminals, ncurses library supports more terminals
  // VT100 escape sequence: \x1b[ -> start escape sequence
  write(STDOUT_FILENO, "\x1b[2J", 4); // Clear entire screen
  write(STDOUT_FILENO, "\x1b[H",  3); // Reposition cursor at top
}

// Draw text on screen row-by-row
void editorDrawRows(struct abuf *ab) {
 
  // Draw column of ~'s to signify lines after EOF 
  for (int y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        // Write welcome message for blank file
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
        if (welcomelen > E.screencols) welcomelen = E.screencols;
        // Center message
        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } 
      else abAppend(ab, "~", 1);
    } else {
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0) len = 0;
      else if (len > E.screencols) len = E.screencols;
      char *c = &E.row[filerow].render[E.coloff]; 
      // Syntax highlighting
      unsigned char *hl = &E.row[filerow].hl[E.coloff];
      char current_color = -1;
      for (int j = 0; j < len; j++) {
        if (iscntrl(c[j])) {
          // Draw control characters as printables w/ inverted colors
          char sym = (c[j] <= 26) ? '@' + c[j] : '?'; // \0 is @, others are capital letters, beyond is all ?s
          abAppend(ab, "\x1b[7m", 4);
          abAppend(ab, &sym, 1);
          abAppend(ab, "\x1b[m", 3); // resets text formatting
          if (current_color != -1) {
            // set previous color back to normal
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
            abAppend(ab, buf, clen);
          }
        } else if (hl[j] == HL_NORMAL) {
          if (current_color != -1) {
            abAppend(ab, "\x1b[39m", 5); // reset text color to default
            current_color = -1;
          }
          abAppend(ab, &c[j], 1);
        } else {
          char color = hl[j];
          if (color != current_color) {
            current_color = color;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            abAppend(ab, buf, clen); // colored text
          }
          abAppend(ab, &c[j], 1);
        }
      }
      abAppend(ab, "\x1b[39m", 5); // reset text color to default just in case
    }
    abAppend(ab, "\x1b[K", 3); // erase everything right of the end of line
    abAppend(ab, "\r\n", 2); 
  }
}

void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4); // inverts colors
  char status[80], rstatus[80];
  // Status shows: up to 20 chars of filename, num lines
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", E.filename ? E.filename : "[No Name]", E.numrows,
                     E.dirty ? "(modified)" : "");
  // Right-aligned status window: display index of current line
  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d", E.syntax ? E.syntax->filetype : "no ft", E.cy+1, E.numrows);
  if (len > E.screencols) len = E.screencols;
  abAppend(ab, status, len);

  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      // Reached up to where rstatus string should begin: print it and end!
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      // Padding spaces between status and rstatus
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3); // restores standard formatting
  abAppend(ab, "\r\n", 2);  // new line for prompts
}

// Draws a message if the message is less than 5 seconds old (disappears at refresh on button press)
void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}

// Clears screen and writes current data buffer 
void editorRefreshScreen() {
  editorScroll();

  struct abuf ab = ABUF_INIT;

  // Write clearScreen commands to buffer
  abAppend(&ab, "\x1bp?25l", 6); // Hide cusor
  abAppend(&ab, "\x1b[H",  3); // Reposition cursor at top

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  // Reposition cursor to cx,cy
  char buf[32];
  int buflen = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy-E.rowoff)+1, (E.rx-E.coloff)+1);
  abAppend(&ab, buf, buflen);

  abAppend(&ab, "\x1b[?25h", 6); // Show cursor

  // Write buffer (w/ escape commands) to terminal
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

// Set formatted string w/ arbitrary args to editor status message
void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/*** INIT ***/

void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.syntax = NULL;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
  E.screenrows -= 2; // Make room for status bar and message prompts
}

int main(int argc, char *argv[]) {
  E.in_handle = GetStdHandle(STD_INPUT_HANDLE);
  E.out_handle = GetStdHandle(STD_OUTPUT_HANDLE);
  enableRawMode();
  initEditor();
  if (argc >= 2)
    editorOpen(argv[1]);

  editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

  while (1) {
    FlushConsoleInputBuffer(E.in_handle);
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}

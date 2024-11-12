# Guide to WinKilo

This guide explains the functionality of the WinKilo text editor. An installation guide, as well as information for customization, can be found in README.md.

## Using WinKilo

WinKilo must be compiled before running. See "Installation" in README.md for information on compiling the project to a .exe file.

kilo.exe is designed to be run on the Windows terminal or Powershell. Running the executable opens the application. We recommend putting the path to kilo.exe on your system path so you can run it by typing `kilo` into the terminal.

The command `kilo <FILENAME>` opens a file as a text file. Note that the file must be stored as ASCII text, as opposed to a binary or a file with a specific encoding like a `.pdf` or `.doc` file; otherwise, Kilo will not be able to treat the file correctly. If no filename is provided, a blank editor will be provided. A filepath can be provided on the first save to create a new file.

## Key Inputs

Kilo is designed as an intuitive text editor. Most keys work as expected for a standard text editor (or for VIM in input mode). Input keys insert characters at the cursor. The arrows move the cursor. When the cursor moves offscreen, the editor scrolls the viewable area in response. Holding shift while moving the arrow keys creates a selection area. Backspace deletes the prior character, while Delete removes the next character.

WinKilo accepts several function inputs caused by a single key press or pressing multiple keys simultaneously. They are as follows:
- Home or Ctrl+Left-Arrow - moves the cursor to the beginning of the line
- End or Ctrl+Right-Arrow - moves the cursor to the end of the line
- PageUp or Ctrl+Up-Arrow - scrolls up one page
- PageDown or Ctrl+Down-Arrow - scrolls down one page
- Ctrl+S - save (or save-as if no file was opened)
- Ctrl+Q - quit the application
- Ctrl+F - find - searches the application for an occurrence of the inputted text (case-sensitive)
- Ctrl+J - jump-to - jumps to a given line number
- Ctrl+A - creates a selection over the entire file
- Ctrl+H - another backspace (for compatibility with older systems)

## Mouse Inputs

WinKilo accepts mouse inputs. The cursor position is set to the targeted location on a left click. Holding the left mouse button and dragging creates a selection.

Resizing the terminal window using the mouse or other methods causes the viewable area in the Kilo editor to change in response. 

## Warnings

Resizing the text buffer appears to cause random crashes, so we have left the text buffer for the user to configure. This means that a scrollbar may appear on the terminal. Using the scrollbar will scroll the text buffer but NOT the Kilo application. This can cause an offset between the application and the terminal and may lead to unexpected behavior.
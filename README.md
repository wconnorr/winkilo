# winkilo
A version of antirez's kilo text editor for Windows. It's a lightweight command line text editor written in c that only relies on standard dependencies.

The original version of this project was written for Linux by following the [tutorial by Paige Ruten](https://viewsourcecode.org/snaptoken/kilo/), thus there are minor differences with [antirez's original](https://github.com/antirez/kilo).

The project was updated to work with the Windows API. It was developed using Powershell 5.1 and tested with Command Prompt on Windows 10.

### Installation

The project simply needs to be compiled, no additional dependencies should be necessary.
Compile using a standard C compiler, such as:
`gcc -o kilo.exe kilo.c kilo.h`

In addition, it may be necessary to enable virtual terminal (VT) commands in the terminal:
`Set-ItemProperty HKCU:\Console VirtualTerminalLevel -Type DWORD 1`

### User Guide

KiloGuide.md contains explanations of features for the average (nontechnical) user.

### Customization

Kilo was designed to be lightweight and easy to customize. I haven't added customization in the application itself, but minor modifications to the `/*** CUSTOMIZATION ***/` section of the `.h` file.

Editing the `editorHighlight` enum with the provided color code enumerations will allow for different colored syntax highlighting. This may be necessary on different consoles to better contrast the highlights.

New filetypes can be added to the highlight database `HLDB` by mimicking the setup for the provided c highlighting. Each HLDB entry consists of (in order): 
1. the file type display name
2. the file extension list (for detecting file type) (NULL terminated)
3. the list of highlight keywords: HL_KEYWORD2 highlights are used for strings appended with `|`, otherwise HL_KEYWORD1 highlights are used. To disable keyword highlighting, use `{NULL}`
4. single-line comment string. To disable, use `""`
5. multi-line comment start string. To disable multiline comments, use `""`
6. multi-line comment end string.
7. highlight flags joined with bitwise or `|` : so far only number highlighting (HL_HIGHLIGHT_NUMBERS) and string highlighting (HL_HIGHLIGHT_STRINGS) are used.


### License
License was added in accordance with [antirez's original project](https://github.com/antirez/kilo)
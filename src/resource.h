// resource.h — resource IDs for the plugin's own dialogs and their child
// controls. Kept deliberately small: the project tree panel builds its tree
// view at runtime rather than as a dialog template control, and the only
// other dialog we need is a tiny reusable text-prompt box.
#pragma once

#define IDD_PROJECTPANEL   1000
#define IDC_PROJECT_TREE   1001

// A small reusable "type a name" prompt, used for New Project / Rename
// Project. Built as a real dialog template (rather than an on-the-fly
// DLGTEMPLATE) because that's far less error-prone to get right.
#define IDD_INPUTBOX       1002
#define IDC_INPUT_PROMPT   1003
#define IDC_INPUT_EDIT     1004

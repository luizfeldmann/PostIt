/* ===================================================================================  //
//    This program is free software: you can redistribute it and/or modify              //
//    it under the terms of the GNU General Public License as published by              //
//    the Free Software Foundation, either version 3 of the License, or                 //
//    (at your option) any later version.                                               //
//                                                                                      //
//    This program is distributed in the hope that it will be useful,                   //
//    but WITHOUT ANY WARRANTY; without even the implied warranty of                    //
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                     //
//    GNU General Public License for more details.                                      //
//                                                                                      //
//    You should have received a copy of the GNU General Public License                 //
//    along with this program.  If not, see <https://www.gnu.org/licenses/>5.           //
//                                                                                      //
//    Copyright: Luiz Gustavo Pfitscher e Feldmann, 2020                                //
// ===================================================================================  */

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <stdint.h>
#include "resources.h"

// size of the post-it when it's created net
static const int defaultWidth = 300;
static const int defaultHeight = 300;

// available color for background and text
static const DWORD color_palette[] = {
    RGB(230, 185,   5), // 0 yellow
    RGB(111, 210,  98), // 1 green
    RGB(234, 134, 194), // 2 pink
    RGB(199, 142, 255), // 3 purple
    RGB( 90, 182, 231), // 4 blue
    RGB(170, 170, 170), // 5 gray
    RGB( 69,  69,  69), // 6 charcoal
    RGB(  0,   0,   0), // 7 black
    RGB(255, 255, 255), // 8 white
};

static const char POSTIT_CLASS_NAME[]  = "PostIt.Post";
static const char tray_class_name[] = "PostIt.Tray";

// APP SAVED DATA
// data that gets saved on disk to be persistent
struct notedata {
    HWND window;
    int32_t x, y, w, h;
    LOGFONT font;
    HFONT hFont;
    DWORD color_post;
    DWORD color_text;
    char *text;
};

struct myappdata
{
    uint32_t numNotes;
    DWORD default_color_post;
    DWORD default_color_text;
    LOGFONT default_font;
    struct notedata* notes;
};

struct myappdata appdata = {
    .numNotes = 0,
    .default_color_post = color_palette[0],
    .default_color_text = color_palette[8],
    .default_font = (LOGFONT) {
            .lfHeight = 0,
            .lfWidth = 0,
            .lfEscapement = 0,
            .lfOrientation = 0,
            .lfWeight = FW_DONTCARE,
            .lfItalic = FALSE,
            .lfUnderline = FALSE,
            .lfStrikeOut = FALSE,
            .lfCharSet = DEFAULT_CHARSET,
            .lfOutPrecision = OUT_DEFAULT_PRECIS,
            .lfClipPrecision = CLIP_DEFAULT_PRECIS,
            .lfQuality = ANTIALIASED_QUALITY,
            .lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE,
            .lfFaceName = "Calibri",
        },
    .notes = NULL
    };

int lastActiveNote = -1;

char filename[MAX_PATH] = "";

int UpdateFile(char* filename);
// ==============

// searches the list of posts for a specific window
struct notedata* FindNoteByHwnd(HWND hwnd, int* out_index)
{
    for (int noteIndex = 0; noteIndex < appdata.numNotes; noteIndex++)
    {
        struct notedata* note = &appdata.notes[noteIndex];

        if (note->window == hwnd)
        {
            if (out_index != NULL)
                *out_index = noteIndex;

            return note;
        }
    }

    return NULL;
};

void DeleteNote(int index)
{
    free(appdata.notes[index].text); // free the text buffer of that post

    for (int i = index; i < appdata.numNotes - 1; i++) // push back each list entry to close the gap
         appdata.notes[i] = appdata.notes[i+1];

    // TODO (maybe): realloc shrinked list of posts

    appdata.numNotes--;
    lastActiveNote = -1;
}

// called by each post-it window
LRESULT CALLBACK MainProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    int note_index;
    struct notedata* note;

    if ((note = FindNoteByHwnd(hwnd, &note_index)) == NULL)
        goto BAIL;

    switch (uMsg)
    {
        // when the text is changes
        case WM_COMMAND:
            if (HIWORD(wParam) == EN_CHANGE)
            {
                HWND edit = (HWND)lParam;

                int len = GetWindowTextLength(edit) + 1;
                char *new_text = (char*)calloc(sizeof(char), len);

                if (new_text == NULL)
                    break;

                free(note->text);
                note->text = new_text;

                GetWindowTextA(edit, new_text, len);
            }
        break;

        // when the child edits are repainted, allows for color change
        case WM_CTLCOLOREDIT:
        {
            HDC hdc = (HDC)wParam;

            SetTextColor(hdc, note->color_text);
            SetBkColor(hdc, note->color_post);
            SetDCBrushColor(hdc, note->color_post);

            return (LRESULT)GetStockObject(DC_BRUSH);
        }
        break;

        // post-it parent window is resized - must resize children to match
        case WM_SIZE:
        //case WM_SIZING:
        {
            RECT rcClient;

            BOOL CALLBACK EnumChild_Resize(HWND hwndChild, LPARAM lParam)
            {
                MoveWindow(hwndChild, 0, 0, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top, TRUE);
                return FALSE;
            }
            GetClientRect(hwnd, &rcClient);
            EnumChildWindows(hwnd, EnumChild_Resize, 0);

        }
        //break; //-- FALL THRHOUG TO WM_MOVE
        case WM_MOVE: // post-it parent window is moved - must save new position
        {
            RECT rcClient;
            GetWindowRect(hwnd, &rcClient);

            note->x = rcClient.left;
            note->y = rcClient.top;
            note->w = rcClient.right - rcClient.left;
            note->h = rcClient.bottom - rcClient.top;
        }
        break;

        case WM_CLOSE: // closing in the X button means deleting that post
            if ( MessageBox(hwnd, "Are you sure you wish to delete this note?", "Confirm delete", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_APPLMODAL) == IDYES)
            {
                DestroyWindow(hwnd);
                DeleteNote(note_index);
            }

            return 0;
        break;

        case WM_DESTROY: // we mus free the handle to the font we created in NewNote/LoadFromFile/SetFont
            if (note->hFont != NULL)
                DeleteObject(note->hFont);
        break;

        case WM_ACTIVATE:
            if (wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE)
                lastActiveNote = note_index;
            else
                UpdateFile(filename); // we update the saved file every time the user interacts with a note
        break;

        default: break;
    }

    BAIL:
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

#include <commdlg.h>
HFONT PostChooseFont(LOGFONT* logf, WINBOOL bDefault)
{
    if (logf == NULL)
        return NULL;

    if (bDefault)
    {
        if (appdata.default_font.lfHeight == 0)
        {
            HDC hDC = GetDC(HWND_DESKTOP);
            appdata.default_font.lfHeight = -MulDiv(16, GetDeviceCaps(hDC, LOGPIXELSY), 72);
            ReleaseDC(NULL, hDC);
        }

        *logf = appdata.default_font;
    }
    else
    {
        CHOOSEFONT chfont = (CHOOSEFONT) {
        .lStructSize = sizeof(CHOOSEFONT),
        .hwndOwner = NULL,
        .hDC = NULL,
        .lpLogFont = logf,
        .iPointSize = 0,
        .Flags = CF_FORCEFONTEXIST | CF_INITTOLOGFONTSTRUCT,
        .rgbColors = RGB(0,0,0),
        .lCustData = 0,
        .lpfnHook = NULL,
        .lpTemplateName = NULL,
        .hInstance = NULL,
        .lpszStyle = NULL,
        .nFontType = REGULAR_FONTTYPE,
        .nSizeMin = 0,
        . nSizeMax = 0,
        };

        if (!ChooseFont(&chfont))
            return NULL;

        if (lastActiveNote >= 0)
        {
            SetForegroundWindow(appdata.notes[lastActiveNote].window); // the dialog causes the window to loose focus - so we must re-establish it
            SetFocus(GetWindow(appdata.notes[lastActiveNote].window, GW_CHILD));
        }
    }

    return CreateFontIndirect(logf);
}

HWND CreatePostItWindow(HINSTANCE inst, HFONT hFont, char* initialText, int32_t x, int32_t y, int32_t w, int32_t h, WINBOOL bGrabFocus)
{
    HWND hwnd = CreateWindowEx( WS_EX_TOOLWINDOW, POSTIT_CLASS_NAME, "", WS_OVERLAPPEDWINDOW, x, y, w, h, NULL, NULL, inst, NULL );

    if (hwnd)
        ShowWindow(hwnd, SW_SHOW);
    else
        fprintf(stderr, "\nCreateWindowEx failed");

    HWND edit = CreateWindowEx(WS_EX_TRANSPARENT, "Edit", initialText, WS_CHILD | WS_VISIBLE | ES_MULTILINE, 0, 0, w, h, hwnd, NULL, NULL, NULL);

    if (bGrabFocus)
    {
        SetForegroundWindow(hwnd); // allow the user to type on it right away
        SetFocus(edit);
    }

    if (hFont != NULL)
        SendMessage(edit, WM_SETFONT, (WPARAM)hFont, TRUE);

    return hwnd;
}

struct notedata* NewNote()
{
    // create one new slot in buffer
    struct notedata* new_list = (struct notedata*)realloc(appdata.notes, sizeof(struct notedata) * (appdata.numNotes + 1));
    if (new_list == NULL)
    {
        fprintf(stderr, "\nRealloc for new item failed!");
        return NULL;
    }

    // if the reallocation was successful, update our table to the new pointer
    appdata.notes = new_list;
    appdata.numNotes++;

    // assign default values to the new post
    struct notedata* new_note = &appdata.notes[appdata.numNotes-1];

    new_note->text = NULL;
    new_note->x = CW_USEDEFAULT;
    new_note->y = CW_USEDEFAULT;
    new_note->w = defaultWidth;
    new_note->h = defaultHeight;
    new_note->color_post = appdata.default_color_post;
    new_note->color_text = appdata.default_color_text;
    new_note->hFont = PostChooseFont(&new_note->font, TRUE);
    new_note->window = CreatePostItWindow(NULL, new_note->hFont, new_note->text, new_note->x, new_note->y, new_note->w, new_note->h, TRUE);
    lastActiveNote = appdata.numNotes-1;

    return new_note;
}

void CloseAll()
{
    for (int noteIndex = 0; noteIndex < appdata.numNotes; noteIndex++)
        free(appdata.notes[noteIndex].text); // releases the text buffers

    free(appdata.notes); // release the post themselves
}


HBITMAP BitmapFromIcon(HICON hIcon, WINBOOL bDestroy)
{
    int cx = 16;//GetSystemMetrics(SM_CXICON); -- too big
    int cy = cx;//GetSystemMetrics(SM_CYICON);

    HDC screenDC = GetDC(NULL);
    HBITMAP bmpTmp = CreateCompatibleBitmap(screenDC, cx, cy);

    HDC memDC = CreateCompatibleDC(screenDC);

    HBITMAP pOldBmp = SelectObject(memDC, bmpTmp);
    DrawIconEx( memDC, 0, 0, hIcon, cx, cy, 0, NULL, DI_NORMAL);
    SelectObject( memDC, pOldBmp );

    HBITMAP hDibBmp = (HBITMAP)CopyImage(bmpTmp, IMAGE_BITMAP, 0, 0, LR_DEFAULTSIZE | LR_CREATEDIBSECTION);

    if (bDestroy)
        DestroyIcon(hIcon);

    DeleteObject(bmpTmp);

    DeleteDC(memDC);
    ReleaseDC(NULL, screenDC);

    return hDibBmp;
}

// called when user mouse-clicks the tray icon
void TrayPopup(HWND hwnd)
{
    HMENU hmenu;
    HMENU hmenuTrackPopup;
    int item;
    const static int menu_item_icon_list[] = {MENU_ITEM_NEW, MENU_ITEM_SHOW, MENU_ITEM_FONT, MENU_ITEM_TEXT_COLOR, MENU_ITEM_BACK_COLOR, MENU_ITEM_CLOSE}; // matches icon to menu item index

    // load the context menu from the resources file
    if ((hmenu = LoadMenu(NULL, "TrayMenu")) == NULL)
        return;

    hmenuTrackPopup = GetSubMenu(hmenu, 0);

    // list icons in the menu and get a bitmap for each one
    int itemCount = GetMenuItemCount(hmenuTrackPopup);
    HBITMAP *openBmp = (HBITMAP*)calloc(sizeof(HBITMAP), itemCount); // keep track of the image handles we created so we can close after the menu dismisses

    for (int i = 0; i< itemCount; i++)
    {
        MENUITEMINFO mif = {0};
        mif.cbSize = sizeof(mif);

        if (!GetMenuItemInfo(hmenuTrackPopup, i, TRUE, &mif ))
            continue;

        mif.fMask |= MIIM_BITMAP;
        mif.hbmpItem = BitmapFromIcon(LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(menu_item_icon_list[i])), TRUE);
        openBmp[i] = mif.hbmpItem;

        SetMenuItemInfo(hmenuTrackPopup, i, TRUE, &mif);
    }


    POINT lpClickPoint;
    GetCursorPos(&lpClickPoint);

    // shows the menu and gets the resulting item
    item = TrackPopupMenu(hmenuTrackPopup,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_LEFTBUTTON|TPM_BOTTOMALIGN,
               lpClickPoint.x, lpClickPoint.y,0,hwnd,NULL);

    switch (item)
   {
        case MENU_ITEM_NEW: // new
            NewNote();
        break;

        case MENU_ITEM_SHOW: // show all
            for (int noteIndex = 0; noteIndex < appdata.numNotes; noteIndex++)
                SetForegroundWindow(appdata.notes[noteIndex].window);
        break;

        case MENU_ITEM_FONT: // change font
            if (lastActiveNote >= 0)
            {
                HFONT newFont = PostChooseFont(&appdata.notes[lastActiveNote].font, FALSE);
                if (newFont != NULL)
                {
                    appdata.default_font = appdata.notes[lastActiveNote].font; // the font we just selected becomes default for all new posts

                    if (appdata.notes[lastActiveNote].hFont != NULL)
                        DeleteObject(appdata.notes[lastActiveNote].hFont);

                    appdata.notes[lastActiveNote].hFont = newFont;
                    SendMessage(GetWindow(appdata.notes[lastActiveNote].window, GW_CHILD), WM_SETFONT, (WPARAM)newFont, TRUE);
                }
            }
        break;

        case MENU_ITEM_CLOSE: // close
            PostQuitMessage(0);
        break;

        /*case MENU_ITEM_REPAINT: // repaint
            repaint:
            for (int noteIndex = 0; noteIndex < appdata.numNotes; noteIndex++)
                InvalidateRect(appdata.notes[noteIndex].window, NULL, TRUE);
        break;*/

        default:
            // changes the default color for new posts and the color of the currently selected one
            if (item >= MENU_ITEM_TEXT_COLOR_F && item <= MENU_ITEM_TEXT_COLOR_F + 8)
            {
                appdata.default_color_text = color_palette[item - 500];
                if (lastActiveNote >= 0)
                {
                    appdata.notes[lastActiveNote].color_text = appdata.default_color_text;
                    InvalidateRect(appdata.notes[lastActiveNote].window, NULL, TRUE);
                }
            }
            else if (item >= MENU_ITEM_BACK_COLOR_F && item <= MENU_ITEM_BACK_COLOR_F + 8)
            {
                appdata.default_color_post = color_palette[item - 600];
                if (lastActiveNote >= 0)
                {
                    appdata.notes[lastActiveNote].color_post = appdata.default_color_post;
                    InvalidateRect(appdata.notes[lastActiveNote].window, NULL, TRUE);
                }
            }
        break;
   }

    // cleanup
    DestroyMenu(hmenu);

    // delete the bitmaps
    for (int i = 0; i< itemCount; i++)
        DeleteObject(openBmp[i]);

    free(openBmp); // delete the list of bitmaps
}

// window events on the tray icon
LRESULT CALLBACK TrayProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_APP: // app-specific messages
            if (lParam != WM_LBUTTONUP && lParam != WM_RBUTTONUP)
                break;
            else
                TrayPopup(hwnd);
        break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        break;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int LoadFromFile(char* filename, HINSTANCE hInstance)
{
    // make sure the file exists
    if (GetFileAttributes(filename) == INVALID_FILE_ATTRIBUTES )
        return TRUE; // file does not exist - so default attributes will be used - this is not and error!

    // open the file for reading
    FILE* fp = fopen(filename, "rb+");
    if (fp == NULL)
    {
        fprintf(stderr, "\nError reading notes file");
        return FALSE;
    }

    // read number of notes saved
    if (fread(&appdata.numNotes, sizeof(appdata.numNotes), 1, fp) != 1)
        return FALSE;

    printf("\nSaved notes count: %d", appdata.numNotes);

    // save default font
    if (fread(&appdata.default_font, sizeof(appdata.default_font), 1, fp) != 1)
        return FALSE;

    // read color scheme
    if (fread(&appdata.default_color_post, sizeof(appdata.default_color_post), 1, fp) != 1)
        return FALSE;

    if (fread(&appdata.default_color_text, sizeof(appdata.default_color_text), 1, fp) != 1)
        return FALSE;

    // alloc space for all the notes and then read each one from the file
    appdata.notes = (struct notedata*)calloc(sizeof(struct notedata), appdata.numNotes);
    for (int noteIndex = 0; noteIndex < appdata.numNotes; noteIndex++)
    {
        struct notedata* note = &appdata.notes[noteIndex];

        uint32_t len = 0;

        // how long is the text?
        if (fread(&len, sizeof(len), 1, fp) != 1)
            break;

        // get placement data - position and size
        if (fread(&note->x, sizeof(note->x), 1, fp) != 1)
            break;

        if (fread(&note->y, sizeof(note->y), 1, fp) != 1)
            break;

        if (fread(&note->w, sizeof(note->w), 1, fp) != 1)
            break;

        if (fread(&note->h, sizeof(note->h), 1, fp) != 1)
            break;

        // read font info
        if (fread(&note->font, sizeof(note->font), 1, fp) != 1)
            break;

        // read color scheme
        if (fread(&note->color_post, sizeof(note->color_post), 1, fp) != 1)
            break;

        if (fread(&note->color_text, sizeof(note->color_text), 1, fp) != 1)
            break;

        // alloc space for the text and then read it
        note->text = (char*)calloc(sizeof(char), len + 1);

        if (fread(note->text, sizeof(char), len, fp) != len)
            break;

        note->hFont = CreateFontIndirect(&note->font);
        note->window = CreatePostItWindow(hInstance, note->hFont, note->text, note->x, note->y, note->w, note->h, FALSE);

        printf("Read note %d/%d: %s", noteIndex + 1, appdata.numNotes, note->text);
    }

    // release the file
    fclose(fp);

    return TRUE;
}

int UpdateFile(char* filename)
{
    // opens file for reading
    FILE* fp = fopen(filename, "wb+");

    if (fp == NULL)
    {
        fprintf(stderr, "Error opening notes file to update");
        return FALSE;
    }

    // write number of entries
    fwrite(&appdata.numNotes, sizeof(appdata.numNotes), 1, fp);

    // save default font
    fwrite(&appdata.default_font, sizeof(appdata.default_font), 1, fp);

    // save color scheme
    fwrite(&appdata.default_color_post, sizeof(appdata.default_color_post), 1, fp);
    fwrite(&appdata.default_color_text, sizeof(appdata.default_color_text), 1, fp);

    // write the entries
    for (int noteIndex = 0; noteIndex < appdata.numNotes; noteIndex++)
    {
        struct notedata* note = &appdata.notes[noteIndex];

        uint32_t len = 0;

        if (note->text != NULL)
            len = strlen(note->text);

        // write the length of the text in this entry
        fwrite(&len, sizeof(len), 1, fp);

        // write position and size
        fwrite(&note->x, sizeof(note->x), 1, fp);
        fwrite(&note->y, sizeof(note->y), 1, fp);
        fwrite(&note->w, sizeof(note->w), 1, fp);
        fwrite(&note->h, sizeof(note->h), 1, fp);

        // write font info
        fwrite(&note->font, sizeof(note->font), 1, fp);

        // write color scheme
        fwrite(&note->color_post, sizeof(note->color_post), 1, fp);
        fwrite(&note->color_text, sizeof(note->color_text), 1, fp);

        // write the actual text
        if (note->text != NULL)
            fwrite(note->text, sizeof(note->text[0]), len, fp);
    }

    fclose(fp);

    return TRUE;
}

#include "Shlwapi.h"
INT WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    PSTR lpCmdLine, INT nCmdShow)
{
    // register a message-only window for the tray
    WNDCLASSEX wx_tray = {};
    wx_tray.cbSize = sizeof(WNDCLASSEX);
    wx_tray.hInstance     = hInstance;
    wx_tray.lpfnWndProc = TrayProc;
    wx_tray.lpszClassName = tray_class_name;

    if ( !RegisterClassEx(&wx_tray) )
        return -1;

    // register a post-it window
    WNDCLASS wc_post = { };

    wc_post.lpfnWndProc   = MainProc;
    wc_post.hInstance     = hInstance;
    wc_post.lpszClassName = POSTIT_CLASS_NAME;

    if ( !RegisterClass(&wc_post) )
        return -1;

    // create the message-only window for the tray icon
    HWND msg_window;
    if ((msg_window = CreateWindowEx( 0, tray_class_name, "", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL )) <= 0)
        return -1;

    // add a tray icon
    NOTIFYICONDATA nid = {0};
    nid.hWnd             = msg_window;
    nid.uID              = 0;
    nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_APP;
    nid.hIcon            = LoadIcon(hInstance, "MAINICON");
    strcpy(nid.szTip, "PostIt");

    if (!Shell_NotifyIcon( NIM_ADD, &nid ))
        return -1;

    // load saved notes
    GetModuleFileNameA(NULL, filename, MAX_PATH - 6);
    strcat(filename, ".data");

    if (!LoadFromFile(filename, hInstance))
    {
        goto BAIL;
    }

    // main loop
    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // cleanup tray and classes
    BAIL:
    Shell_NotifyIcon( NIM_DELETE, &nid );
    UnregisterClass(POSTIT_CLASS_NAME, hInstance);
    UnregisterClass(tray_class_name, hInstance);

    // save the changes to the notes and releases memory
    UpdateFile(filename);
    CloseAll();

    return 0;
}

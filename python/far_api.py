import ctypes
from ctypes import wintypes
import sys


# Define GUID (UUID)
class GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", ctypes.c_ulong),
        ("Data2", ctypes.c_ushort),
        ("Data3", ctypes.c_ushort),
        ("Data4", ctypes.c_ubyte * 8),
    ]

    def __init__(self, d1=0, d2=0, d3=0, d4=None):
        self.Data1 = d1
        self.Data2 = d2
        self.Data3 = d3
        if d4:
            self.Data4 = (ctypes.c_ubyte * 8)(*d4)
        else:
            self.Data4 = (ctypes.c_ubyte * 8)(*([0] * 8))

    def __str__(self):
        d4_str = "".join(f"{b:02X}" for b in self.Data4)
        return f"{{{self.Data1:08X}-{self.Data2:04X}-{self.Data3:04X}-{d4_str[:4]}-{d4_str[4:]}}}"


# Far GUID (builtin "FarGuid" in plugin.hpp)
FarGuid = GUID(
    0x00000000, 0x0000, 0x0000, [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
)


# Common Types
HANDLE = wintypes.HANDLE
LPVOID = wintypes.LPVOID
DWORD = wintypes.DWORD
BOOL = wintypes.BOOL
FILETIME = wintypes.FILETIME
COLORREF = wintypes.COLORREF
size_t = ctypes.c_size_t
wchar_t = ctypes.c_wchar
_PTR = ctypes.c_uint64 if ctypes.sizeof(ctypes.c_void_p) == 8 else ctypes.c_uint32
intptr_t = _PTR
uintptr_t = _PTR


# FarColor structures
class rgba(ctypes.Structure):
    _fields_ = [
        ("r", ctypes.c_ubyte),
        ("g", ctypes.c_ubyte),
        ("b", ctypes.c_ubyte),
        ("a", ctypes.c_ubyte),
    ]


class color_index(ctypes.Structure):
    _fields_ = [
        ("i", ctypes.c_ubyte),
        ("reserved0", ctypes.c_ubyte),
        ("reserved1", ctypes.c_ubyte),
        ("a", ctypes.c_ubyte),
    ]


class FarColorUnionForeground(ctypes.Union):
    _fields_ = [
        ("ForegroundColor", COLORREF),
        ("ForegroundIndex", color_index),
        ("ForegroundRGBA", rgba),
    ]


class FarColorUnionBackground(ctypes.Union):
    _fields_ = [
        ("BackgroundColor", COLORREF),
        ("BackgroundIndex", color_index),
        ("BackgroundRGBA", rgba),
    ]


class FarColorUnionUnderline(ctypes.Union):
    _fields_ = [
        ("UnderlineColor", COLORREF),
        ("UnderlineIndex", color_index),
        ("UnderlineRGBA", rgba),
    ]


class FarColor(ctypes.Structure):
    _anonymous_ = ("Foreground", "Background", "Underline")
    _fields_ = [
        ("Flags", ctypes.c_ulonglong),  # FARCOLORFLAGS
        ("Foreground", FarColorUnionForeground),
        ("Background", FarColorUnionBackground),
        ("Underline", FarColorUnionUnderline),
        ("Reserved", DWORD),
    ]


# FarColor Flags
FCF_FG_INDEX = 0x0000000000000001
FCF_BG_INDEX = 0x0000000000000002
FCF_FG_UNDERLINE_INDEX = 0x0000000000000008
FCF_INDEXMASK = 0x000000000000000B
FCF_INHERIT_STYLE = 0x0000000000000004
FCF_FG_BOLD = 0x1000000000000000
FCF_FG_ITALIC = 0x2000000000000000
FCF_FG_UNDERLINE = 0x4000000000000000
FCF_NONE = 0


# PluginMenuItem
class PluginMenuItem(ctypes.Structure):
    _fields_ = [
        ("Guids", ctypes.POINTER(GUID)),
        ("Strings", ctypes.POINTER(ctypes.c_wchar_p)),
        ("Count", size_t),
    ]


# PluginInfo
class PluginInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Flags", ctypes.c_ulonglong),
        ("DiskMenu", PluginMenuItem),
        ("PluginMenu", PluginMenuItem),
        ("PluginConfig", PluginMenuItem),
        ("CommandPrefix", ctypes.c_wchar_p),
        ("Instance", LPVOID),
    ]


# Forward declarations
class PluginStartupInfo(ctypes.Structure):
    pass


class FarStandardFunctions(ctypes.Structure):
    pass


# Enum / Constants from plugin.hpp
FARMESSAGEFLAGS = ctypes.c_ulonglong
FMSG_WARNING = 0x0000000000000001
FMSG_ERRORTYPE = 0x0000000000000002
FMSG_KEEPBACKGROUND = 0x0000000000000004
FMSG_LEFTALIGN = 0x0000000000000008
FMSG_ALLINONE = 0x0000000000000010
FMSG_MB_OK = 0x0000000000010000
FMSG_MB_OKCANCEL = 0x0000000000020000
FMSG_MB_ABORTRETRYIGNORE = 0x0000000000030000
FMSG_MB_YESNO = 0x0000000000040000
FMSG_MB_YESNOCANCEL = 0x0000000000050000
FMSG_MB_RETRYCANCEL = 0x0000000000060000


# FarKey
class FarKey(ctypes.Structure):
    _fields_ = [
        ("VirtualKeyCode", wintypes.WORD),
        ("ControlKeyState", wintypes.DWORD),
    ]


# FarMenuItem
class FarMenuItem(ctypes.Structure):
    _fields_ = [
        ("Flags", ctypes.c_ulonglong),
        ("Text", ctypes.c_wchar_p),
        ("AccelKey", FarKey),
        ("UserData", intptr_t),
        ("Reserved", intptr_t * 2),
    ]


# Function Pointer Types
FARAPIMESSAGE = ctypes.WINFUNCTYPE(
    intptr_t,
    ctypes.POINTER(GUID),  # PluginId
    ctypes.POINTER(GUID),  # Id
    FARMESSAGEFLAGS,  # Flags
    ctypes.c_wchar_p,  # HelpTopic
    ctypes.POINTER(ctypes.c_wchar_p),  # Items
    size_t,  # ItemsNumber
    intptr_t,  # ButtonsNumber
)

FARAPIMENU = ctypes.WINFUNCTYPE(
    intptr_t,
    ctypes.POINTER(GUID),  # PluginId
    ctypes.POINTER(GUID),  # Id
    intptr_t,  # X
    intptr_t,  # Y
    intptr_t,  # MaxHeight
    ctypes.c_ulonglong,  # Flags
    ctypes.c_wchar_p,  # Title
    ctypes.c_wchar_p,  # Bottom
    ctypes.c_wchar_p,  # HelpTopic
    ctypes.POINTER(intptr_t),  # BreakKeys
    ctypes.POINTER(intptr_t),  # BreakCode
    ctypes.POINTER(FarMenuItem),  # Item
    size_t,  # ItemsNumber
)

FARAPIINPUTBOX = ctypes.WINFUNCTYPE(
    intptr_t,
    ctypes.POINTER(GUID),  # PluginId
    ctypes.POINTER(GUID),  # Id
    ctypes.c_wchar_p,  # Title
    ctypes.c_wchar_p,  # SubTitle
    ctypes.c_wchar_p,  # HistoryName
    ctypes.c_wchar_p,  # SrcText
    ctypes.c_wchar_p,  # DestText
    size_t,  # DestSize
    ctypes.c_wchar_p,  # HelpTopic
    ctypes.c_ulonglong,  # Flags
)

FARAPIPANELCONTROL = ctypes.WINFUNCTYPE(
    intptr_t,
    HANDLE,  # hPanel
    ctypes.c_int,  # Command (FILE_CONTROL_COMMANDS)
    intptr_t,  # Param1
    LPVOID,  # Param2
)


# Dialog API Constants

# FARDIALOGITEMTYPES - Dialog item types
DI_TEXT = 0
DI_VTEXT = 1
DI_SINGLEBOX = 2
DI_DOUBLEBOX = 3
DI_EDIT = 4
DI_PSWEDIT = 5
DI_FIXEDIT = 6
DI_BUTTON = 7
DI_CHECKBOX = 8
DI_RADIOBUTTON = 9
DI_COMBOBOX = 10
DI_LISTBOX = 11
DI_USERCONTROL = 255

# FARDIALOGITEMFLAGS - Dialog item flags
FARDIALOGITEMFLAGS = ctypes.c_ulonglong
DIF_BOXCOLOR = 0x0000000000000200
DIF_GROUP = 0x0000000000000400
DIF_LEFTTEXT = 0x0000000000000800
DIF_MOVESELECT = 0x0000000000001000
DIF_SHOWAMPERSAND = 0x0000000000002000
DIF_CENTERGROUP = 0x0000000000004000
DIF_NOBRACKETS = 0x0000000000008000
DIF_MANUALADDHISTORY = 0x0000000000008000
DIF_SEPARATOR = 0x0000000000010000
DIF_SEPARATOR2 = 0x0000000000020000
DIF_EDITOR = 0x0000000000020000
DIF_LISTNOAMPERSAND = 0x0000000000020000
DIF_LISTNOBOX = 0x0000000000040000
DIF_HISTORY = 0x0000000000040000
DIF_BTNNOCLOSE = 0x0000000000040000
DIF_CENTERTEXT = 0x0000000000040000
DIF_SEPARATORUSER = 0x0000000000080000
DIF_SETSHIELD = 0x0000000000080000
DIF_EDITEXPAND = 0x0000000000080000
DIF_DROPDOWNLIST = 0x0000000000100000
DIF_USELASTHISTORY = 0x0000000000200000
DIF_MASKEDIT = 0x0000000000400000
DIF_LISTTRACKMOUSE = 0x0000000000400000
DIF_LISTTRACKMOUSEINFOCUS = 0x0000000000800000
DIF_SELECTONENTRY = 0x0000000000800000
DIF_3STATE = 0x0000000000800000
DIF_EDITPATH = 0x0000000001000000
DIF_LISTWRAPMODE = 0x0000000001000000
DIF_NOAUTOCOMPLETE = 0x0000000002000000
DIF_LISTAUTOHIGHLIGHT = 0x0000000002000000
DIF_LISTNOCLOSE = 0x0000000004000000
DIF_EDITPATHEXEC = 0x0000000004000000
DIF_AUTOMATION = 0x0000000008000000
DIF_HIDDEN = 0x0000000010000000
DIF_READONLY = 0x0000000020000000
DIF_NOFOCUS = 0x0000000040000000
DIF_DISABLE = 0x0000000080000000
DIF_DEFAULTBUTTON = 0x0000000100000000
DIF_FOCUS = 0x0000000200000000
DIF_RIGHTTEXT = 0x0000000400000000
DIF_WORDWRAP = 0x0000000800000000
DIF_LISTNOMERGEBORDER = 0x0000001000000000
DIF_HOMEITEM = 0x0000002000000000
DIF_NONE = 0

# FARMESSAGE - Dialog messages (DM_*)
FARMESSAGE = ctypes.c_int
DM_FIRST = 0
DM_CLOSE = 1
DM_ENABLE = 2
DM_ENABLEREDRAW = 3
DM_GETDLGDATA = 4
DM_GETDLGITEM = 5
DM_GETDLGRECT = 6
DM_GETTEXT = 7
DM_KEY = 9
DM_MOVEDIALOG = 10
DM_SETDLGDATA = 11
DM_SETDLGITEM = 12
DM_SETFOCUS = 13
DM_REDRAW = 14
DM_SETTEXT = 15
DM_SETMAXTEXTLENGTH = 16
DM_SHOWDIALOG = 17
DM_GETFOCUS = 18
DM_GETCURSORPOS = 19
DM_SETCURSORPOS = 20
DM_SETTEXTPTR = 22
DM_SHOWITEM = 23
DM_ADDHISTORY = 24
DM_GETCHECK = 25
DM_SETCHECK = 26
DM_SET3STATE = 27
DM_LISTSORT = 28
DM_LISTGETITEM = 29
DM_LISTGETCURPOS = 30
DM_LISTSETCURPOS = 31
DM_LISTDELETE = 32
DM_LISTADD = 33
DM_LISTADDSTR = 34
DM_LISTUPDATE = 35
DM_LISTINSERT = 36
DM_LISTFINDSTRING = 37
DM_LISTINFO = 38
DM_LISTGETDATA = 39
DM_LISTSETDATA = 40
DM_LISTSETTITLES = 41
DM_LISTGETTITLES = 42
DM_RESIZEDIALOG = 43
DM_SETITEMPOSITION = 44
DM_GETDROPDOWNOPENED = 45
DM_SETDROPDOWNOPENED = 46
DM_SETHISTORY = 47
DM_GETITEMPOSITION = 48
DM_SETINPUTNOTIFY = 49
DM_EDITUNCHANGEDFLAG = 50
DM_GETITEMDATA = 51
DM_SETITEMDATA = 52
DM_LISTSET = 53
DM_LISTSETMOUSEREACTION = 54
DM_GETCURSORSIZE = 55
DM_SETCURSORSIZE = 56
DM_LISTSTYLE = 57
DM_GETDIALOGINFO = 58
DM_GETDIALOGTITLE = 59
DM_SETDIALOGPOS = 60

# Dialog Notifications (DN_*)
DN_FIRST = 4096
DN_BTNCLICK = 4097
DN_CTLCOLORDIALOG = 4098
DN_CTLCOLORDLGITEM = 4099
DN_CTLCOLORDLGLIST = 4100
DN_DRAWDIALOG = 4101
DN_DRAWDLGITEM = 4102
DN_EDITCHANGE = 4103
DN_GOTFOCUS = 4105
DN_HELP = 4106
DN_HOTKEY = 4107
DN_INITDIALOG = 4108
DN_KILLFOCUS = 4109
DN_LISTCHANGE = 4110
DN_DRAGGED = 4111
DN_RESIZECONSOLE = 4112
DN_DRAWDIALOGDONE = 4113
DN_LISTHOTKEY = 4114
DN_INPUT = 4115
DN_CONTROLINPUT = 4116
DN_CLOSE = 4117
DN_GETVALUE = 4118
DN_DROPDOWNOPENED = 4119
DN_DRAWDLGITEMDONE = 4120
DM_KILLSAVESCREEN = DN_FIRST - 1
DM_ALLKEYMODE = DN_FIRST - 2

# Dialog Events (DE_*)
DE_DLGPROCINIT = 0
DE_DEFDLGPROCINIT = 1
DE_DLGPROCEND = 2

# Editor Events (EE_*)
EE_READ = 0
EE_SAVE = 1
EE_REDRAW = 2
EE_CLOSE = 3
EE_GOTFOCUS = 6
EE_KILLFOCUS = 7
EE_CHANGE = 8

# Viewer Events (VE_*)
VE_READ = 0
VE_CLOSE = 1
VE_GOTFOCUS = 6
VE_KILLFOCUS = 7


# Forward declaration for DialogItem
class FarDialogItem(ctypes.Structure):
    pass


# FarListItem - items in a FarList (for combobox/listbox)
class FarListItem(ctypes.Structure):
    _fields_ = [
        ("Flags", ctypes.c_ulonglong),  # LISTITEMFLAGS
        ("Text", ctypes.c_wchar_p),
        ("UserData", intptr_t),
        ("Reserved", intptr_t),
    ]


# FarList - list of items for combobox/listbox
class FarList(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("ItemsNumber", size_t),
        ("Items", ctypes.POINTER(FarListItem)),
    ]


# FAR_CHAR_INFO - character with attributes for virtual buffer
class FAR_CHAR_INFO(ctypes.Structure):
    _fields_ = [
        ("Char", ctypes.c_wchar),
        ("Reserved0", ctypes.c_wchar),
        ("Reserved1", ctypes.c_int),
        ("Attributes", FarColor),
    ]


# Union for FarDialogItem.Param field
class FarDialogItemParam(ctypes.Union):
    _fields_ = [
        ("Selected", intptr_t),
        ("ListItems", ctypes.POINTER(FarList)),
        ("VBuf", ctypes.POINTER(FAR_CHAR_INFO)),
        ("Reserved0", intptr_t),
    ]


# Complete FarDialogItem definition
FarDialogItem._fields_ = [
    ("Type", ctypes.c_int),  # FARDIALOGITEMTYPES enum
    ("X1", intptr_t),
    ("Y1", intptr_t),
    ("X2", intptr_t),
    ("Y2", intptr_t),
    ("Param", FarDialogItemParam),
    ("History", ctypes.c_wchar_p),
    ("Mask", ctypes.c_wchar_p),
    ("Flags", ctypes.c_ulonglong),  # FARDIALOGITEMFLAGS
    ("Data", ctypes.c_wchar_p),
    ("MaxLength", size_t),
    ("UserData", intptr_t),
    ("Reserved", intptr_t * 2),
]


# FarDialogItemData - for getting/setting dialog item data
class FarDialogItemData(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("PtrLength", size_t),
        ("PtrData", ctypes.c_wchar_p),
    ]


# FarDialogEvent - dialog event info
class FarDialogEvent(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("hDlg", HANDLE),
        ("Msg", intptr_t),
        ("Param1", intptr_t),
        ("Param2", ctypes.c_void_p),
        ("Result", intptr_t),
    ]


# FarGetDialogItem - for FCTL_GETDIALOGITEM
class FarGetDialogItem(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Size", size_t),
        ("Item", ctypes.POINTER(FarDialogItem)),
    ]


# FarDialogItemColors - for DM_GETITEMCOLOR/DM_SETITEMCOLOR
class FarDialogItemColors(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Flags", ctypes.c_ulonglong),
        ("ColorsCount", size_t),
        ("Colors", ctypes.POINTER(FarColor)),
    ]


# DialogInfo - for DM_GETDIALOGINFO
class DialogInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Id", GUID),
        ("Owner", GUID),
    ]


# LISTITEMFLAGS - flags for FarListItem
LIF_SELECTED = 0x0000000000010000
LIF_CHECKED = 0x0000000000020000
LIF_SEPARATOR = 0x0000000000040000
LIF_DISABLE = 0x0000000000080000
LIF_GRAYED = 0x0000000000100000
LIF_HIDDEN = 0x0000000000200000
LIF_DELETEUSERDATA = 0x0000000080000000


# FarListInfo - for DM_LISTINFO
class FarListInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Flags", ctypes.c_ulonglong),  # LISTITEMFLAGS
        ("ItemsNumber", size_t),
        ("SelectPos", intptr_t),
        ("TopPos", intptr_t),
        ("MaxHeight", intptr_t),
        ("MaxLength", intptr_t),
    ]


# FarListPos - for DM_LISTGETCURPOS / DM_LISTSETCURPOS
class FarListPos(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("SelectPos", intptr_t),
        ("TopPos", intptr_t),
    ]


# FarListGetItem - for DM_LISTGETITEM
class FarListGetItem(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("ItemIndex", intptr_t),
        ("Item", FarListItem),
    ]


# FarListTitles - for DM_LISTGETTITLES / DM_LISTSETTITLES
class FarListTitles(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("TitleSize", size_t),
        ("Title", ctypes.c_wchar_p),
        ("BottomSize", size_t),
        ("Bottom", ctypes.c_wchar_p),
    ]


FARWINDOWPROC = ctypes.WINFUNCTYPE(
    intptr_t,
    HANDLE,  # hDlg
    intptr_t,  # Msg
    intptr_t,  # Param1
    LPVOID,  # Param2
)

FARAPIDIALOGINIT = ctypes.WINFUNCTYPE(
    HANDLE,
    ctypes.POINTER(GUID),  # PluginId
    ctypes.POINTER(GUID),  # Id
    intptr_t,  # X1
    intptr_t,  # Y1
    intptr_t,  # X2
    intptr_t,  # Y2
    ctypes.c_wchar_p,  # HelpTopic
    ctypes.POINTER(FarDialogItem),  # Item
    size_t,  # ItemsNumber
    intptr_t,  # Reserved
    ctypes.c_ulonglong,  # Flags
    FARWINDOWPROC,  # DlgProc
    LPVOID,  # Param
)

FARAPIDIALOGRUN = ctypes.WINFUNCTYPE(
    intptr_t,
    HANDLE,  # hDlg
)

FARAPIDIALOGFREE = ctypes.WINFUNCTYPE(
    None,
    HANDLE,  # hDlg
)

FARAPISENDDLGMESSAGE = ctypes.WINFUNCTYPE(
    intptr_t,
    HANDLE,  # hDlg
    intptr_t,  # Msg
    intptr_t,  # Param1
    LPVOID,  # Param2
)

FARAPIDEFDLGPROC = ctypes.WINFUNCTYPE(
    intptr_t,
    HANDLE,  # hDlg
    intptr_t,  # Msg
    intptr_t,  # Param1
    LPVOID,  # Param2
)

# Editor Flags
EF_NONMODAL = 0x0000000000000001
EF_CREATENEW = 0x0000000000000002
EF_ENABLE_F6 = 0x0000000000000004
EF_DISABLE_HISTORY = 0x0000000000000008
EF_DELETEONCLOSE = 0x0000000000000010
EF_IMMEDIATERETURN = 0x0000000000000100
EF_OPENMODE_MASK = 0x00000000F0000000
EF_OPENMODE_USEEXISTING = 0x0000000020000000
EF_OPENMODE_BREAKIFOPEN = 0x0000000010000000
EF_OPENMODE_RELOADIFOPEN = 0x0000000030000000

# Viewer Flags
VF_NONMODAL = 0x0000000000000001
VF_DELETEONCLOSE = 0x0000000000000002
VF_ENABLE_F6 = 0x0000000000000004
VF_DISABLEHISTORY = 0x0000000000000008
VF_IMMEDIATERETURN = 0x0000000000000100
VF_DELETEONLYFILEONCLOSE = 0x0000000000000200

FARAPIEDITOR = ctypes.WINFUNCTYPE(
    intptr_t,
    ctypes.c_wchar_p,  # FileName
    ctypes.c_wchar_p,  # Title
    intptr_t,  # X1
    intptr_t,  # Y1
    intptr_t,  # X2
    intptr_t,  # Y2
    ctypes.c_ulonglong,  # Flags
    intptr_t,  # StartLine
    intptr_t,  # StartChar
    uintptr_t,  # CodePage (Must be uintptr_t)
)

FARAPIVIEWER = ctypes.WINFUNCTYPE(
    intptr_t,
    ctypes.c_wchar_p,  # FileName
    ctypes.c_wchar_p,  # Title
    intptr_t,  # X1
    intptr_t,  # Y1
    intptr_t,  # X2
    intptr_t,  # Y2
    ctypes.c_ulonglong,  # Flags
    uintptr_t,  # CodePage (Must be uintptr_t)
)

FARAPITEXT = ctypes.WINFUNCTYPE(
    None,
    intptr_t,  # X
    intptr_t,  # Y
    ctypes.POINTER(FarColor),  # Color
    ctypes.c_wchar_p,  # Str
)

# FARHELPFLAGS constants
FARHELPFLAGS = ctypes.c_ulonglong
FHELP_NOSHOWERROR = 0x0000000080000000
FHELP_SELFHELP = 0x0000000000000000
FHELP_FARHELP = 0x0000000000000001
FHELP_CUSTOMFILE = 0x0000000000000002
FHELP_CUSTOMPATH = 0x0000000000000004
FHELP_GUID = 0x0000000000000008
FHELP_USECONTENTS = 0x0000000040000000
FHELP_NONE = 0

FARAPISHOWHELP = ctypes.WINFUNCTYPE(
    BOOL,
    ctypes.c_wchar_p,  # ModuleName
    ctypes.c_wchar_p,  # HelpTopic
    FARHELPFLAGS,  # Flags
)

FARAPIVIEWERCONTROL = ctypes.WINFUNCTYPE(
    intptr_t,
    intptr_t,  # ViewerID
    ctypes.c_int,  # VIEWER_CONTROL_COMMANDS
    intptr_t,  # Param1
    LPVOID,  # Param2
)

FARAPIEDITORCONTROL = ctypes.WINFUNCTYPE(
    intptr_t,
    intptr_t,  # EditorID
    ctypes.c_int,  # EDITOR_CONTROL_COMMANDS
    intptr_t,  # Param1
    LPVOID,  # Param2
)

# Screen Management API functions
FARAPISAVESCREEN = ctypes.WINFUNCTYPE(
    HANDLE,
    intptr_t,  # X1
    intptr_t,  # Y1
    intptr_t,  # X2
    intptr_t,  # Y2
)

FARAPIRESTORESCREEN = ctypes.WINFUNCTYPE(
    None,
    HANDLE,  # hScreen
)

FARAPIFREESCREEN = ctypes.WINFUNCTYPE(
    None,
    HANDLE,  # hScreen
)

# Settings Control API
# FAR_SETTINGS_CONTROL_COMMANDS
SCTL_CREATE = 0
SCTL_FREE = 1
SCTL_SET = 2
SCTL_GET = 3
SCTL_ENUM = 4
SCTL_DELETE = 5
SCTL_CREATESUBKEY = 6
SCTL_OPENSUBKEY = 7

# FARSETTINGSTYPES
FST_UNKNOWN = 0
FST_SUBKEY = 1
FST_QWORD = 2
FST_STRING = 3
FST_DATA = 4

# FARSETTINGS_SUBFOLDERS
FSSF_ROOT = 0
FSSF_HISTORY_CMD = 1
FSSF_HISTORY_FOLDER = 2
FSSF_HISTORY_VIEW = 3
FSSF_HISTORY_EDIT = 4
FSSF_HISTORY_EXTERNAL = 5
FSSF_FOLDERSHORTCUT_0 = 6
FSSF_FOLDERSHORTCUT_1 = 7
FSSF_FOLDERSHORTCUT_2 = 8
FSSF_FOLDERSHORTCUT_3 = 9
FSSF_FOLDERSHORTCUT_4 = 10
FSSF_FOLDERSHORTCUT_5 = 11
FSSF_FOLDERSHORTCUT_6 = 12
FSSF_FOLDERSHORTCUT_7 = 13
FSSF_FOLDERSHORTCUT_8 = 14
FSSF_FOLDERSHORTCUT_9 = 15
FSSF_CONFIRMATIONS = 16
FSSF_SYSTEM = 17
FSSF_PANEL = 18
FSSF_EDITOR = 19
FSSF_SCREEN = 20
FSSF_DIALOG = 21
FSSF_INTERFACE = 22
FSSF_PANELLAYOUT = 23

# FAR_PLUGIN_SETTINGS_LOCATION
PSL_ROAMING = 0
PSL_LOCAL = 1


class FarSettingsCreate(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Guid", GUID),
        ("Handle", HANDLE),
    ]


class FarSettingsDataUnion(ctypes.Union):
    _fields_ = [
        ("Size", size_t),
        ("Data", LPVOID),
    ]


class FarSettingsItemValue(ctypes.Union):
    _fields_ = [
        ("Number", ctypes.c_ulonglong),
        ("String", ctypes.c_wchar_p),
        ("Data", FarSettingsDataUnion),
    ]


class FarSettingsItem(ctypes.Structure):
    _anonymous_ = ("Value",)
    _fields_ = [
        ("StructSize", size_t),
        ("Root", size_t),
        ("Name", ctypes.c_wchar_p),
        ("Type", ctypes.c_int),  # FARSETTINGSTYPES
        ("Value", FarSettingsItemValue),
    ]


class FarSettingsName(ctypes.Structure):
    _fields_ = [
        ("Name", ctypes.c_wchar_p),
        ("Type", ctypes.c_int),  # FARSETTINGSTYPES
    ]


class FarSettingsHistory(ctypes.Structure):
    _fields_ = [
        ("Name", ctypes.c_wchar_p),
        ("Param", ctypes.c_wchar_p),
        ("PluginId", GUID),
        ("File", ctypes.c_wchar_p),
        ("Time", FILETIME),
        ("Lock", BOOL),
    ]


class FarSettingsEnumUnion(ctypes.Union):
    _fields_ = [
        ("Items", ctypes.POINTER(FarSettingsName)),
        ("Histories", ctypes.POINTER(FarSettingsHistory)),
    ]


class FarSettingsEnum(ctypes.Structure):
    _anonymous_ = ("Union",)
    _fields_ = [
        ("StructSize", size_t),
        ("Root", size_t),
        ("Count", size_t),
        ("Union", FarSettingsEnumUnion),
    ]

    @property
    def Value(self):
        # Far's C API names this union member `Value` in C mode.
        return self.Union


class FarSettingsValue(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Root", size_t),
        ("Value", ctypes.c_wchar_p),
    ]


FARAPISETTINGSCONTROL = ctypes.WINFUNCTYPE(
    intptr_t,
    HANDLE,  # hHandle
    ctypes.c_int,  # FAR_SETTINGS_CONTROL_COMMANDS
    intptr_t,  # Param1
    LPVOID,  # Param2
)

# Advanced Control API
# ADVANCED_CONTROL_COMMANDS
ACTL_GETFARMANAGERVERSION = 0
ACTL_WAITKEY = 2
ACTL_GETCOLOR = 3
ACTL_GETARRAYCOLOR = 4
ACTL_GETWINDOWINFO = 6
ACTL_GETWINDOWCOUNT = 7
ACTL_SETCURRENTWINDOW = 8
ACTL_COMMIT = 9
ACTL_GETFARHWND = 10
ACTL_SETARRAYCOLOR = 16
ACTL_REDRAWALL = 19
ACTL_SYNCHRO = 20
ACTL_SETPROGRESSSTATE = 21
ACTL_SETPROGRESSVALUE = 22
ACTL_QUIT = 23
ACTL_GETFARRECT = 24
ACTL_GETCURSORPOS = 25
ACTL_SETCURSORPOS = 26
ACTL_PROGRESSNOTIFY = 27
ACTL_GETWINDOWTYPE = 28

# VERSION_STAGE
VS_RELEASE = 0
VS_ALPHA = 1
VS_BETA = 2
VS_RC = 3
VS_SPECIAL = 4
VS_PRIVATE = 5


class VersionInfo(ctypes.Structure):
    _fields_ = [
        ("Major", DWORD),
        ("Minor", DWORD),
        ("Revision", DWORD),
        ("Build", DWORD),
        ("Stage", ctypes.c_int),  # VERSION_STAGE
    ]


# WINDOWINFO_TYPE
WTYPE_UNKNOWN = -1
WTYPE_DESKTOP = 0
WTYPE_PANELS = 1
WTYPE_VIEWER = 2
WTYPE_EDITOR = 3
WTYPE_DIALOG = 4
WTYPE_VMENU = 5
WTYPE_HELP = 6
WTYPE_COMBOBOX = 7
WTYPE_GRABBER = 8
WTYPE_HMENU = 9

# WINDOWINFO_FLAGS
WINDOWINFO_FLAGS = ctypes.c_ulonglong
WIF_MODIFIED = 0x0000000000000001
WIF_CURRENT = 0x0000000000000002
WIF_MODAL = 0x0000000000000004
WIF_NONE = 0


class WindowInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Id", intptr_t),
        ("TypeName", ctypes.c_wchar_p),
        ("Name", ctypes.c_wchar_p),
        ("TypeNameSize", intptr_t),
        ("NameSize", intptr_t),
        ("Pos", intptr_t),
        ("Type", ctypes.c_int),  # WINDOWINFO_TYPE
        ("Flags", WINDOWINFO_FLAGS),
    ]


class WindowType(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Type", ctypes.c_int),  # WINDOWINFO_TYPE
    ]


# TASKBARPROGRESSTATE
TBPS_NOPROGRESS = 0x0
TBPS_INDETERMINATE = 0x1
TBPS_NORMAL = 0x2
TBPS_ERROR = 0x4
TBPS_PAUSED = 0x8


class ProgressValue(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Completed", ctypes.c_ulonglong),
        ("Total", ctypes.c_ulonglong),
    ]


FARAPIADVCONTROL = ctypes.WINFUNCTYPE(
    intptr_t,
    ctypes.POINTER(GUID),  # PluginId
    ctypes.c_int,  # ADVANCED_CONTROL_COMMANDS
    intptr_t,  # Param1
    LPVOID,  # Param2
)

# Macro Control API
# FAR_MACRO_CONTROL_COMMANDS
MCTL_LOADALL = 0
MCTL_SAVEALL = 1
MCTL_SENDSTRING = 2
MCTL_GETSTATE = 5
MCTL_GETAREA = 6
MCTL_ADDMACRO = 7
MCTL_DELMACRO = 8
MCTL_GETLASTERROR = 9
MCTL_EXECSTRING = 10

# FARKEYMACROFLAGS
FARKEYMACROFLAGS = ctypes.c_ulonglong
KMFLAGS_SILENTCHECK = 0x0000000000000001
KMFLAGS_NOSENDKEYSTOPLUGINS = 0x0000000000000002
KMFLAGS_ENABLEOUTPUT = 0x0000000000000004
KMFLAGS_LANGMASK = 0x0000000000000070
KMFLAGS_LUA = 0x0000000000000000
KMFLAGS_MOONSCRIPT = 0x0000000000000010
KMFLAGS_NONE = 0

# FARMACROSENDSTRINGCOMMAND
MSSC_POST = 0
MSSC_CHECK = 2

# FARMACROVARTYPE
FMVT_UNKNOWN = 0
FMVT_INTEGER = 1
FMVT_STRING = 2
FMVT_DOUBLE = 3
FMVT_BOOLEAN = 4
FMVT_BINARY = 5
FMVT_POINTER = 6
FMVT_NIL = 7
FMVT_ARRAY = 8
FMVT_PANEL = 9
FMVT_ERROR = 10
FMVT_MBSTRING = 11
FMVT_NEWTABLE = 12
FMVT_SETTABLE = 13

# FARMACROSTATE
MACROSTATE_NOMACRO = 0
MACROSTATE_EXECUTING = 1
MACROSTATE_EXECUTING_COMMON = 2
MACROSTATE_RECORDING = 3
MACROSTATE_RECORDING_COMMON = 4

# FARMACROPARSEERRORCODE
MPEC_SUCCESS = 0
MPEC_ERROR = 1

# FARMACROAREA
MACROAREA_OTHER = 0
MACROAREA_SHELL = 1
MACROAREA_VIEWER = 2
MACROAREA_EDITOR = 3
MACROAREA_DIALOG = 4
MACROAREA_SEARCH = 5
MACROAREA_DISKS = 6
MACROAREA_MAINMENU = 7
MACROAREA_MENU = 8
MACROAREA_HELP = 9
MACROAREA_INFOPANEL = 10
MACROAREA_QVIEWPANEL = 11
MACROAREA_TREEPANEL = 12
MACROAREA_FINDFOLDER = 13
MACROAREA_USERMENU = 14
MACROAREA_SHELLAUTOCOMPLETION = 15
MACROAREA_DIALOGAUTOCOMPLETION = 16
MACROAREA_GRABBER = 17
MACROAREA_DESKTOP = 18
MACROAREA_COMMON = 255

# Need INPUT_RECORD from Windows API
from ctypes import wintypes


def _get_wintype(name: str, fallback):
    return getattr(wintypes, name, fallback)


COORD = _get_wintype("COORD", _get_wintype("_COORD", None))
if COORD is None:

    class COORD(ctypes.Structure):
        _fields_ = [("X", ctypes.c_short), ("Y", ctypes.c_short)]


BOOL = _get_wintype("BOOL", ctypes.c_int)
WORD = _get_wintype("WORD", ctypes.c_ushort)
DWORD = _get_wintype("DWORD", ctypes.c_ulong)


class KEY_EVENT_RECORD(ctypes.Structure):
    _fields_ = [
        ("bKeyDown", BOOL),
        ("wRepeatCount", WORD),
        ("wVirtualKeyCode", WORD),
        ("wVirtualScanCode", WORD),
        ("uChar", ctypes.c_wchar),
        ("dwControlKeyState", DWORD),
    ]


class MOUSE_EVENT_RECORD(ctypes.Structure):
    _fields_ = [
        ("dwMousePosition", COORD),
        ("dwButtonState", DWORD),
        ("dwControlKeyState", DWORD),
        ("dwEventFlags", DWORD),
    ]


class WINDOW_BUFFER_SIZE_RECORD(ctypes.Structure):
    _fields_ = [("dwSize", COORD)]


class MENU_EVENT_RECORD(ctypes.Structure):
    _fields_ = [("dwCommandId", DWORD)]


class FOCUS_EVENT_RECORD(ctypes.Structure):
    _fields_ = [("bSetFocus", BOOL)]


class _INPUT_RECORD_UNION(ctypes.Union):
    _fields_ = [
        ("KeyEvent", KEY_EVENT_RECORD),
        ("MouseEvent", MOUSE_EVENT_RECORD),
        ("WindowBufferSizeEvent", WINDOW_BUFFER_SIZE_RECORD),
        ("MenuEvent", MENU_EVENT_RECORD),
        ("FocusEvent", FOCUS_EVENT_RECORD),
    ]


class INPUT_RECORD(ctypes.Structure):
    _fields_ = [("EventType", WORD), ("Event", _INPUT_RECORD_UNION)]


class FarMacroValueBinary(ctypes.Structure):
    _fields_ = [
        ("Data", LPVOID),
        ("Size", size_t),
    ]


# Forward declarations to allow mutual references
class FarMacroValue(ctypes.Structure):
    pass


class FarMacroValueArray(ctypes.Structure):
    _fields_ = [
        ("Values", ctypes.POINTER(FarMacroValue)),
        ("Count", size_t),
    ]


class FarMacroValueUnion(ctypes.Union):
    _fields_ = [
        ("Integer", ctypes.c_longlong),
        ("Boolean", ctypes.c_longlong),
        ("Double", ctypes.c_double),
        ("String", ctypes.c_wchar_p),
        ("MBString", ctypes.c_char_p),
        ("Pointer", LPVOID),
        ("Binary", FarMacroValueBinary),
        ("Array", FarMacroValueArray),
    ]


FarMacroValue._anonymous_ = ("Value",)
FarMacroValue._fields_ = [
    ("Type", ctypes.c_int),  # FARMACROVARTYPE
    ("Value", FarMacroValueUnion),
]


class MacroSendMacroText(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Flags", FARKEYMACROFLAGS),
        ("AKey", INPUT_RECORD),
        ("SequenceText", ctypes.c_wchar_p),
    ]


class MacroExecuteString(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Flags", FARKEYMACROFLAGS),
        ("SequenceText", ctypes.c_wchar_p),
        ("InCount", size_t),
        ("InValues", ctypes.POINTER(FarMacroValue)),
        ("OutCount", size_t),
        ("OutValues", ctypes.POINTER(FarMacroValue)),
    ]


class MacroParseResult(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("ErrCode", DWORD),
        ("ErrPos", wintypes._COORD if hasattr(wintypes, "_COORD") else ctypes.c_int),
        ("ErrSrc", ctypes.c_wchar_p),
    ]


FARAPIMACROCONTROL = ctypes.WINFUNCTYPE(
    intptr_t,
    ctypes.POINTER(GUID),  # PluginId
    ctypes.c_int,  # FAR_MACRO_CONTROL_COMMANDS
    intptr_t,  # Param1
    LPVOID,  # Param2
)

# RegExp Control API
# FAR_REGEXP_CONTROL_COMMANDS
RECTL_CREATE = 0
RECTL_FREE = 1
RECTL_COMPILE = 2
RECTL_OPTIMIZE = 3
RECTL_MATCHEX = 4
RECTL_SEARCHEX = 5
RECTL_BRACKETSCOUNT = 6
RECTL_NAMEDGROUPINDEX = 7
RECTL_GETNAMEDGROUPS = 8
RECTL_GETSTATUS = 9


class RegExpMatch(ctypes.Structure):
    _fields_ = [
        ("start", intptr_t),
        ("end", intptr_t),
    ]


class RegExpSearch(ctypes.Structure):
    _fields_ = [
        ("Text", ctypes.c_wchar_p),
        ("Position", intptr_t),
        ("Length", intptr_t),
        ("Match", ctypes.POINTER(RegExpMatch)),
        ("Count", intptr_t),
        ("Reserved", LPVOID),
    ]


class RegExpNamedGroup(ctypes.Structure):
    _fields_ = [
        ("Index", size_t),
        ("Name", ctypes.c_wchar_p),
    ]


class RegExpStatus(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Error", ctypes.c_wchar_p),
        ("Position", ctypes.c_int),
        ("Status", ctypes.c_int),
    ]


FARAPIREGEXPCONTROL = ctypes.WINFUNCTYPE(
    intptr_t,
    HANDLE,  # hHandle
    ctypes.c_int,  # FAR_REGEXP_CONTROL_COMMANDS
    intptr_t,  # Param1
    LPVOID,  # Param2
)

# Plugins Control API
# FAR_PLUGINS_CONTROL_COMMANDS
PCTL_LOADPLUGIN = 0
PCTL_UNLOADPLUGIN = 1
PCTL_FORCEDLOADPLUGIN = 2
PCTL_FINDPLUGIN = 3
PCTL_GETPLUGININFORMATION = 4
PCTL_GETPLUGINS = 5

# FAR_PLUGIN_LOAD_TYPE
PLT_PATH = 0

# FAR_PLUGIN_FIND_TYPE
PFM_GUID = 0
PFM_MODULENAME = 1


class FarGetPluginInformation(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("ModuleName", ctypes.c_wchar_p),
        ("Flags", ctypes.c_ulonglong),  # FAR_PLUGIN_FLAGS
        ("PInfo", LPVOID),  # PluginInfo pointer
        ("GInfo", LPVOID),  # GlobalInfo pointer
    ]


FARAPIPLUGINSCONTROL = ctypes.WINFUNCTYPE(
    intptr_t,
    HANDLE,  # hHandle
    ctypes.c_int,  # FAR_PLUGINS_CONTROL_COMMANDS
    intptr_t,  # Param1
    LPVOID,  # Param2
)

# File Filter Control API
# FAR_FILE_FILTER_CONTROL_COMMANDS
FFCTL_CREATEFILEFILTER = 0
FFCTL_FREEFILEFILTER = 1
FFCTL_OPENFILTERSMENU = 2
FFCTL_STARTINGTOFILTER = 3
FFCTL_ISFILEINFILTER = 4

# FAR_FILE_FILTER_TYPE
FFT_PANEL = 0
FFT_FINDFILE = 1
FFT_COPY = 2
FFT_SELECT = 3
FFT_CUSTOM = 4

FARAPIFILEFILTERCONTROL = ctypes.WINFUNCTYPE(
    intptr_t,
    HANDLE,  # hHandle
    ctypes.c_int,  # FAR_FILE_FILTER_CONTROL_COMMANDS
    intptr_t,  # Param1
    LPVOID,  # Param2
)

# Color Dialog API
COLORDIALOGFLAGS = ctypes.c_ulonglong
CDF_NONE = 0

FARAPICOLORDIALOG = ctypes.WINFUNCTYPE(
    BOOL,
    ctypes.POINTER(GUID),  # PluginId
    COLORDIALOGFLAGS,  # Flags
    ctypes.POINTER(FarColor),  # Color
)

# FarStandardFunctions (FSF) API
# Function pointer types for FSF

# Note: Many FSF functions use variadic arguments which are difficult in ctypes.
# We'll define the typedefs but Python equivalents are often better.

FARSTDATOI = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_wchar_p)
FARSTDATOI64 = ctypes.WINFUNCTYPE(ctypes.c_longlong, ctypes.c_wchar_p)
FARSTDITOA = ctypes.WINFUNCTYPE(
    ctypes.c_wchar_p, ctypes.c_int, ctypes.c_wchar_p, ctypes.c_int
)
FARSTDITOA64 = ctypes.WINFUNCTYPE(
    ctypes.c_wchar_p, ctypes.c_longlong, ctypes.c_wchar_p, ctypes.c_int
)

# sprintf, sscanf, snprintf are variadic - skip or use simplified versions
FARSTDQSORT = ctypes.WINFUNCTYPE(None, LPVOID, size_t, size_t, LPVOID, LPVOID)
FARSTDBSEARCH = ctypes.WINFUNCTYPE(
    LPVOID, LPVOID, LPVOID, size_t, size_t, LPVOID, LPVOID
)

FARSTDLOCALISLOWER = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_wchar)
FARSTDLOCALISUPPER = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_wchar)
FARSTDLOCALISALPHA = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_wchar)
FARSTDLOCALISALPHANUM = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_wchar)
FARSTDLOCALUPPER = ctypes.WINFUNCTYPE(ctypes.c_wchar, ctypes.c_wchar)
FARSTDLOCALLOWER = ctypes.WINFUNCTYPE(ctypes.c_wchar, ctypes.c_wchar)
FARSTDLOCALUPPERBUF = ctypes.WINFUNCTYPE(None, ctypes.c_wchar_p, intptr_t)
FARSTDLOCALLOWERBUF = ctypes.WINFUNCTYPE(None, ctypes.c_wchar_p, intptr_t)
FARSTDLOCALSTRUPR = ctypes.WINFUNCTYPE(None, ctypes.c_wchar_p)
FARSTDLOCALSTRLWR = ctypes.WINFUNCTYPE(None, ctypes.c_wchar_p)
FARSTDLOCALSTRICMP = ctypes.WINFUNCTYPE(
    ctypes.c_int, ctypes.c_wchar_p, ctypes.c_wchar_p
)
FARSTDLOCALSTRNICMP = ctypes.WINFUNCTYPE(
    ctypes.c_int, ctypes.c_wchar_p, ctypes.c_wchar_p, intptr_t
)

FARSTDUNQUOTE = ctypes.WINFUNCTYPE(None, ctypes.POINTER(ctypes.c_wchar))
FARSTDLTRIM = ctypes.WINFUNCTYPE(ctypes.c_wchar_p, ctypes.c_wchar_p)
FARSTDRTRIM = ctypes.WINFUNCTYPE(ctypes.c_wchar_p, ctypes.c_wchar_p)
FARSTDTRIM = ctypes.WINFUNCTYPE(ctypes.c_wchar_p, ctypes.c_wchar_p)
FARSTDTRUNCSTR = ctypes.WINFUNCTYPE(ctypes.c_wchar_p, ctypes.c_wchar_p, intptr_t)
FARSTDTRUNCPATHSTR = ctypes.WINFUNCTYPE(ctypes.c_wchar_p, ctypes.c_wchar_p, intptr_t)
FARSTDQUOTESPACEONLY = ctypes.WINFUNCTYPE(ctypes.c_wchar_p, ctypes.c_wchar_p)
FARSTDPOINTTONAME = ctypes.WINFUNCTYPE(ctypes.c_wchar_p, ctypes.c_wchar_p)
FARSTDGETPATHROOT = ctypes.WINFUNCTYPE(
    size_t, ctypes.c_wchar_p, ctypes.c_wchar_p, size_t
)
FARSTDADDENDSLASH = ctypes.WINFUNCTYPE(BOOL, ctypes.POINTER(ctypes.c_wchar))
FARSTDCOPYTOCLIPBOARD = ctypes.WINFUNCTYPE(BOOL, ctypes.c_int, ctypes.c_wchar_p)
FARSTDPASTEFROMCLIPBOARD = ctypes.WINFUNCTYPE(
    size_t, ctypes.c_int, ctypes.c_wchar_p, size_t
)

FARSTDGETFILEOWNER = ctypes.WINFUNCTYPE(
    size_t, ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_wchar_p, size_t
)
FARSTDGETNUMBEROFLINKS = ctypes.WINFUNCTYPE(size_t, ctypes.c_wchar_p)

# FarClock returns uptime in microseconds since Far Manager session started
FARSTDFARCLOCK = ctypes.WINFUNCTYPE(ctypes.c_ulonglong)

# MkTemp creates a unique temp filename
FARSTDMKTEMP = ctypes.WINFUNCTYPE(size_t, ctypes.c_wchar_p, size_t, ctypes.c_wchar_p)

# FormatFileSize flags
FFFS_COMMAS = 0x0100000000000000
FFFS_FLOATSIZE = 0x0200000000000000
FFFS_SHOWBYTESINDEX = 0x0400000000000000
FFFS_ECONOMIC = 0x0800000000000000
FFFS_THOUSAND = 0x1000000000000000
FFFS_MINSIZEINDEX = 0x2000000000000000
FFFS_MINSIZEINDEX_MASK = 0x0000000000000007
FFFS_NONE = 0

# FormatFileSize formats a file size into human-readable form
FARFORMATFILESIZE = ctypes.WINFUNCTYPE(
    size_t,
    ctypes.c_ulonglong,  # Size
    intptr_t,  # Width
    ctypes.c_ulonglong,  # Flags (FARFORMATFILESIZEFLAGS)
    ctypes.c_wchar_p,  # Dest
    size_t,  # DestSize
)

# CompareStrings compares two strings using Far's current sort settings
FARSTDCOMPARESTRINGS = ctypes.WINFUNCTYPE(
    ctypes.c_int,  # Result (-1, 0, 1)
    ctypes.c_wchar_p,  # Str1
    size_t,  # Size1
    ctypes.c_wchar_p,  # Str2
    size_t,  # Size2
)

# ConvertPath modes (CONVERTPATHMODES)
CPM_FULL = 0  # Convert to full path
CPM_REAL = 1  # Resolve symlinks/junctions
CPM_NATIVE = 2  # Convert to native NT path

# ConvertPath converts paths between different formats
FARSTDCONVERTPATH = ctypes.WINFUNCTYPE(
    size_t,  # Result (required buffer size or 0 on error)
    ctypes.c_int,  # Mode (CONVERTPATHMODES)
    ctypes.c_wchar_p,  # Src
    ctypes.c_wchar_p,  # Dest
    size_t,  # DestSize
)

# GetCurrentDirectory gets the current working directory
FARSTDGETCURRENTDIRECTORY = ctypes.WINFUNCTYPE(
    size_t,  # Result (required buffer size including NUL)
    size_t,  # Size
    ctypes.c_wchar_p,  # Buffer
)

# ProcessName flags (PROCESSNAME_FLAGS)
PN_CMPNAME = 0x00000000  # Compare name with mask
PN_CMPNAMELIST = 0x00010000  # Compare with semicolon-separated mask list
PN_GENERATENAME = 0x00020000  # Generate unique name
PN_CHECKMASK = 0x00030000  # Check if string is valid mask
PN_SKIPPATH = 0x01000000  # Skip path when comparing
PN_SHOWERRORMESSAGE = 0x02000000  # Show error message on failure

# ProcessName performs filename pattern matching operations
FARSTDPROCESSNAME = ctypes.WINFUNCTYPE(
    size_t,  # Result
    ctypes.c_wchar_p,  # Param1 (mask or name depending on mode)
    ctypes.c_wchar_p,  # Param2 (name or output buffer)
    size_t,  # Size
    ctypes.c_ulonglong,  # Flags (PROCESSNAME_FLAGS)
)

# DetectCodePage detects file encoding
FARSTDDETECTCODEPAGE = ctypes.WINFUNCTYPE(
    uintptr_t,  # Result (TRUE if detected, FALSE otherwise)
    ctypes.c_wchar_p,  # FileName
    ctypes.c_ulonglong,  # Flags (reserved, use 0)
    ctypes.POINTER(uintptr_t),  # CodePage (output)
)

# XLat flags (XLATFLAGS)
XLAT_SWITCHKEYBLAYOUT = 0x00000001  # Switch keyboard layout
XLAT_SWITCHKEYBBEEP = 0x00000002  # Beep on switch
XLAT_USEKEYBLAYOUTNAME = 0x00000004  # Use keyboard layout name
XLAT_CONVERTALLCMDLINE = 0x00010000  # Convert all command line

# XLat performs keyboard transliteration
FARSTDXLAT = ctypes.WINFUNCTYPE(
    ctypes.c_wchar_p,  # Result (pointer to Line)
    ctypes.c_wchar_p,  # Line (buffer to transliterate, modified in place)
    intptr_t,  # StartPos
    intptr_t,  # EndPos
    ctypes.c_ulonglong,  # Flags (XLATFLAGS)
)

# FarInputRecordToName converts INPUT_RECORD to key name string
FARSTDINPUTRECORDTONAME = ctypes.WINFUNCTYPE(
    size_t,  # Result (required buffer size or 0 on error)
    ctypes.POINTER(INPUT_RECORD),  # Key
    ctypes.c_wchar_p,  # KeyText output buffer
    size_t,  # Size of buffer
)

# FarNameToInputRecord converts key name string to INPUT_RECORD
FARSTDNAMETOINPUTRECORD = ctypes.WINFUNCTYPE(
    BOOL,  # Result (TRUE if successful)
    ctypes.c_wchar_p,  # Name (key name string)
    ctypes.POINTER(INPUT_RECORD),  # Key output
)

# GetReparsePointInfo returns reparse point target path
FARSTDGETREPARSEPOINTINFO = ctypes.WINFUNCTYPE(
    BOOL,  # Result (TRUE if successful)
    ctypes.c_wchar_p,  # Src (path to reparse point)
    ctypes.c_wchar_p,  # Dest (output buffer for target path)
    size_t,  # DestSize (buffer size)
)

# Color palette indices (COL_* from farcolor.hpp)
# These are the indices used with ACTL_GETCOLOR
COL_MENUTEXT = 0
COL_MENUSELECTEDTEXT = 1
COL_MENUTITLE = 2
COL_MENUHIGHLIGHT = 3
COL_MENUSELECTEDHIGHLIGHT = 4
COL_MENUBOX = 5
COL_MENUTITLEYTEXT = 6
COL_HMENUTEXT = 7
COL_HMENUSELECTEDTEXT = 8
COL_HMENUHIGHLIGHT = 9
COL_HMENUSELECTEDHIGHLIGHT = 10
COL_PANELTEXT = 11
COL_PANELSELECTEDTEXT = 12
COL_PANELHIGHLIGHTTEXT = 13
COL_PANELINFOTEXT = 14
COL_PANELCURSOR = 15
COL_PANELSELECTEDCURSOR = 16
COL_PANELTITLE = 17
COL_PANELSELECTEDTITLE = 18
COL_PANELCOLUMNTITLE = 19
COL_PANELTOTALINFO = 20
COL_PANELSELECTEDINFO = 21
COL_DIALOGTEXT = 22
COL_DIALOGHIGHLIGHTTEXT = 23
COL_DIALOGBOX = 24
COL_DIALOGBOXTITLE = 25
COL_DIALOGHIGHLIGHTBOXTITLE = 26
COL_DIALOGEDIT = 27
COL_DIALOGBUTTON = 28
COL_DIALOGSELECTEDBUTTON = 29
COL_DIALOGHIGHLIGHTBUTTON = 30
COL_DIALOGHIGHLIGHTSELECTEDBUTTON = 31
COL_DIALOGLISTTEXT = 32
COL_DIALOGLISTSELECTEDTEXT = 33
COL_DIALOGLISTHIGHLIGHT = 34
COL_DIALOGLISTSELECTEDHIGHLIGHT = 35
COL_WARNDIALOGTEXT = 36
COL_WARNDIALOGHIGHLIGHTTEXT = 37
COL_WARNDIALOGBOX = 38
COL_WARNDIALOGBOXTITLE = 39
COL_WARNDIALOGHIGHLIGHTBOXTITLE = 40
COL_WARNDIALOGEDIT = 41
COL_WARNDIALOGBUTTON = 42
COL_WARNDIALOGSELECTEDBUTTON = 43
COL_WARNDIALOGHIGHLIGHTBUTTON = 44
COL_WARNDIALOGHIGHLIGHTSELECTEDBUTTON = 45
COL_KEYBARNUM = 46
COL_KEYBARTEXT = 47
COL_KEYBARBACKGROUND = 48
COL_COMMANDLINE = 49
COL_CLOCK = 50
COL_VIEWERTEXT = 51
COL_VIEWERSELECTEDTEXT = 52
COL_VIEWERSTATUS = 53
COL_EDITORTEXT = 54
COL_EDITORSELECTEDTEXT = 55
COL_EDITORSTATUS = 56
COL_HELPTEXT = 57
COL_HELPHIGHLIGHTTEXT = 58
COL_HELPTOPIC = 59
COL_HELPSELECTEDTOPIC = 60
COL_HELPBOX = 61
COL_HELPBOXTITLE = 62
COL_PANELDRAGTEXT = 63
COL_DIALOGEDITUNCHANGED = 64
COL_PANELSCROLLBAR = 65
COL_HELPSCROLLBAR = 66
COL_PANELBOX = 67
COL_PANELSCREENSNUMBER = 68
COL_DIALOGEDITSELECTED = 69
COL_COMMANDLINESELECTED = 70
COL_VIEWERARROWS = 71
COL_DIALOGLISTSCROLLBAR = 72
COL_MENUSCROLLBAR = 73
COL_VIEWERSCROLLBAR = 74
COL_COMMANDLINEPREFIX = 75
COL_DIALOGDISABLED = 76
COL_DIALOGEDITDISABLED = 77
COL_DIALOGLISTDISABLED = 78
COL_WARNDIALOGDISABLED = 79
COL_WARNDIALOGEDITDISABLED = 80
COL_WARNDIALOGLISTDISABLED = 81
COL_MENUDISABLEDTEXT = 82
COL_EDITORCLOCK = 83
COL_VIEWERCLOCK = 84
COL_DIALOGLISTTITLE = 85
COL_DIALOGLISTBOX = 86
COL_WARNDIALOGEDITSELECTED = 87
COL_WARNDIALOGEDITNCHANGED = 88
COL_DIALOGCOMBOTEXT = 89
COL_DIALOGCOMBOSELECTEDTEXT = 90
COL_DIALOGCOMBOHIGHLIGHT = 91
COL_DIALOGCOMBOSELECTEDHIGHLIGHT = 92
COL_DIALOGCOMBOBOX = 93
COL_DIALOGCOMBOTITLE = 94
COL_DIALOGCOMBODISABLED = 95
COL_DIALOGCOMBOSCROLLBAR = 96
COL_WARNDIALOGLISTTEXT = 97
COL_WARNDIALOGLISTSELECTEDTEXT = 98
COL_WARNDIALOGLISTHIGHLIGHT = 99
COL_WARNDIALOGLISTSELECTEDHIGHLIGHT = 100
COL_WARNDIALOGLISTBOX = 101
COL_WARNDIALOGLISTTITLE = 102
COL_WARNDIALOGLISTSCROLLBAR = 103
COL_WARNDIALOGCOMBOTEXT = 104
COL_WARNDIALOGCOMBOSELECTEDTEXT = 105
COL_WARNDIALOGCOMBOHIGHLIGHT = 106
COL_WARNDIALOGCOMBOSELECTEDHIGHLIGHT = 107
COL_WARNDIALOGCOMBOBOX = 108
COL_WARNDIALOGCOMBOTITLE = 109
COL_WARNDIALOGCOMBODISABLED = 110
COL_WARNDIALOGCOMBOSCROLLBAR = 111
COL_DIALOGLISTARROWS = 112
COL_DIALOGLISTARROWSDISABLED = 113
COL_DIALOGLISTARROWSSELECTED = 114
COL_DIALOGCOMBOARROWS = 115
COL_DIALOGCOMBOARROWSDISABLED = 116
COL_DIALOGCOMBOARROWSSELECTED = 117
COL_WARNDIALOGLISTARROWS = 118
COL_WARNDIALOGLISTARROWSDISABLED = 119
COL_WARNDIALOGLISTARROWSSELECTED = 120
COL_WARNDIALOGCOMBOARROWS = 121
COL_WARNDIALOGCOMBOARROWSDISABLED = 122
COL_WARNDIALOGCOMBOARROWSSELECTED = 123
COL_MENUARROWS = 124
COL_MENUARROWSDISABLED = 125
COL_MENUARROWSSELECTED = 126
COL_COMMANDLINEUSERSCREEN = 127
COL_EDITORSCROLLBAR = 128
COL_MENUGRAYTEXT = 129
COL_MENUSELECTEDGRAYTEXT = 130
COL_DIALOGCOMBOGRAY = 131
COL_DIALOGCOMBOSELECTEDGRAYTEXT = 132
COL_DIALOGLISTGRAY = 133
COL_DIALOGLISTSELECTEDGRAYTEXT = 134
COL_WARNDIALOGCOMBOGRAY = 135
COL_WARNDIALOGCOMBOSELECTEDGRAYTEXT = 136
COL_WARNDIALOGLISTGRAY = 137
COL_WARNDIALOGLISTSELECTEDGRAYTEXT = 138
COL_DIALOGDEFAULTBUTTON = 139
COL_DIALOGSELECTEDDEFAULTBUTTON = 140
COL_DIALOGHIGHLIGHTDEFAULTBUTTON = 141
COL_DIALOGHIGHLIGHTSELECTEDDEFAULTBUTTON = 142
COL_WARNDIALOGDEFAULTBUTTON = 143
COL_WARNDIALOGSELECTEDDEFAULTBUTTON = 144
COL_WARNDIALOGHIGHLIGHTDEFAULTBUTTON = 145
COL_WARNDIALOGHIGHLIGHTSELECTEDDEFAULTBUTTON = 146
COL_LASTPALETTECOLOR = 147  # Total number of colors


FarStandardFunctions._fields_ = [
    ("StructSize", size_t),
    ("atoi", FARSTDATOI),
    ("atoi64", FARSTDATOI64),
    ("itoa", FARSTDITOA),
    ("itoa64", FARSTDITOA64),
    ("sprintf", LPVOID),  # Variadic, skip
    ("sscanf", LPVOID),  # Variadic, skip
    ("qsort", FARSTDQSORT),
    ("bsearch", FARSTDBSEARCH),
    ("snprintf", LPVOID),  # Variadic, skip
    ("LIsLower", FARSTDLOCALISLOWER),
    ("LIsUpper", FARSTDLOCALISUPPER),
    ("LIsAlpha", FARSTDLOCALISALPHA),
    ("LIsAlphanum", FARSTDLOCALISALPHANUM),
    ("LUpper", FARSTDLOCALUPPER),
    ("LLower", FARSTDLOCALLOWER),
    ("LUpperBuf", FARSTDLOCALUPPERBUF),
    ("LLowerBuf", FARSTDLOCALLOWERBUF),
    ("LStrupr", FARSTDLOCALSTRUPR),
    ("LStrlwr", FARSTDLOCALSTRLWR),
    ("LStricmp", FARSTDLOCALSTRICMP),
    ("LStrnicmp", FARSTDLOCALSTRNICMP),
    ("Unquote", FARSTDUNQUOTE),
    ("LTrim", FARSTDLTRIM),
    ("RTrim", FARSTDRTRIM),
    ("Trim", FARSTDTRIM),
    ("TruncStr", FARSTDTRUNCSTR),
    ("TruncPathStr", FARSTDTRUNCPATHSTR),
    ("QuoteSpaceOnly", FARSTDQUOTESPACEONLY),
    ("PointToName", FARSTDPOINTTONAME),
    ("GetPathRoot", FARSTDGETPATHROOT),
    ("AddEndSlash", FARSTDADDENDSLASH),
    ("CopyToClipboard", FARSTDCOPYTOCLIPBOARD),
    ("PasteFromClipboard", FARSTDPASTEFROMCLIPBOARD),
    ("FarInputRecordToName", FARSTDINPUTRECORDTONAME),
    ("FarNameToInputRecord", FARSTDNAMETOINPUTRECORD),
    ("XLat", FARSTDXLAT),
    ("GetFileOwner", FARSTDGETFILEOWNER),
    ("GetNumberOfLinks", FARSTDGETNUMBEROFLINKS),
    ("FarRecursiveSearch", LPVOID),  # Complex callback, skip
    ("MkTemp", FARSTDMKTEMP),
    ("ProcessName", FARSTDPROCESSNAME),
    ("MkLink", LPVOID),  # Complex, skip
    ("ConvertPath", FARSTDCONVERTPATH),
    ("GetReparsePointInfo", FARSTDGETREPARSEPOINTINFO),
    ("GetCurrentDirectory", FARSTDGETCURRENTDIRECTORY),
    ("FormatFileSize", FARFORMATFILESIZE),
    ("FarClock", FARSTDFARCLOCK),
    ("CompareStrings", FARSTDCOMPARESTRINGS),
    ("DetectCodePage", FARSTDDETECTCODEPAGE),
]


# Panel handles
PANEL_ACTIVE = ctypes.c_void_p(-1)
PANEL_PASSIVE = ctypes.c_void_p(-2)

# File Control Commands (FILE_CONTROL_COMMANDS from include/plugin.hpp)
FCTL_CLOSEPANEL = 0
FCTL_GETPANELINFO = 1
FCTL_UPDATEPANEL = 2
FCTL_REDRAWPANEL = 3
FCTL_GETCMDLINE = 4
FCTL_SETCMDLINE = 5
FCTL_SETSELECTION = 6
FCTL_SETVIEWMODE = 7
FCTL_INSERTCMDLINE = 8
FCTL_SETUSERSCREEN = 9
FCTL_SETPANELDIRECTORY = 10
FCTL_SETCMDLINEPOS = 11
FCTL_GETCMDLINEPOS = 12
FCTL_SETSORTMODE = 13
FCTL_SETSORTORDER = 14
FCTL_SETCMDLINESELECTION = 15
FCTL_GETCMDLINESELECTION = 16
FCTL_CHECKPANELSEXIST = 17
FCTL_GETUSERSCREEN = 19
FCTL_ISACTIVEPANEL = 20
FCTL_GETPANELITEM = 21
FCTL_GETSELECTEDPANELITEM = 22
FCTL_GETCURRENTPANELITEM = 23
FCTL_GETPANELDIRECTORY = 24
FCTL_GETCOLUMNTYPES = 25
FCTL_GETCOLUMNWIDTHS = 26
FCTL_BEGINSELECTION = 27
FCTL_ENDSELECTION = 28
FCTL_CLEARSELECTION = 29
FCTL_SETDIRECTORIESFIRST = 30
FCTL_GETPANELFORMAT = 31
FCTL_GETPANELHOSTFILE = 32
FCTL_GETPANELPREFIX = 34
FCTL_SETACTIVEPANEL = 35

# Panel Info Flags
PFLAGS_SHOWHIDDEN = 0x00000001
PFLAGS_HIGHLIGHT = 0x00000002
PFLAGS_REVERSESORTORDER = 0x00000004
PFLAGS_USESORTGROUPS = 0x00000008
PFLAGS_SELECTEDFIRST = 0x00000010
PFLAGS_REALNAMES = 0x00000020
PFLAGS_NUMERICSORT = 0x00000040
PFLAGS_PANELLEFT = 0x00000080
PFLAGS_DIRECTORIESFIRST = 0x00000100
PFLAGS_USECRC32 = 0x00000200
PFLAGS_CASESENSITIVESORT = 0x00000400
PFLAGS_PANELPLUGIN = 0x00000800
PFLAGS_VISIBLE = 0x00001000
PFLAGS_FOCUS = 0x00002000
PFLAGS_ALTERNATIVENAMES = 0x00004000
PFLAGS_SHORTNAMES = 0x00008000


# Windows SMALL_RECT structure - uses SHORT (c_short)
# Used by DM_GETDLGRECT, DM_GETITEMPOSITION, etc.
class SMALL_RECT(ctypes.Structure):
    """Windows SMALL_RECT structure - uses SHORT (c_short)"""

    _fields_ = [
        ("Left", ctypes.c_short),
        ("Top", ctypes.c_short),
        ("Right", ctypes.c_short),
        ("Bottom", ctypes.c_short),
    ]


# PanelInfo Structure
class Rect(ctypes.Structure):
    """Windows RECT structure - uses LONG (c_long), not short"""

    _fields_ = [
        ("Left", ctypes.c_long),
        ("Top", ctypes.c_long),
        ("Right", ctypes.c_long),
        ("Bottom", ctypes.c_long),
    ]


class PanelInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("PluginHandle", HANDLE),
        ("OwnerGuid", GUID),
        ("Flags", ctypes.c_ulonglong),
        ("ItemsNumber", size_t),
        ("SelectedItemsNumber", size_t),
        ("PanelRect", Rect),
        ("CurrentItem", size_t),
        ("TopPanelItem", size_t),
        ("ViewMode", intptr_t),
        ("PanelType", ctypes.c_int),
        ("SortMode", ctypes.c_int),
    ]


# PanelInfo Types
PTYPE_FILEPANEL = 0
PTYPE_TREEPANEL = 1
PTYPE_QVIEWPANEL = 2
PTYPE_INFOPANEL = 3

# Define HANDLE properly
if ctypes.sizeof(ctypes.c_void_p) == 8:
    HANDLE = ctypes.c_void_p  # x64
else:
    HANDLE = ctypes.c_ulong  # x86


# Panel Directory
class FarPanelDirectory(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Name", ctypes.c_wchar_p),
        ("Param", ctypes.c_wchar_p),
        ("PluginId", GUID),
        ("File", ctypes.c_wchar_p),
    ]


# Command line selection
class CmdLineSelect(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("SelStart", intptr_t),
        ("SelEnd", intptr_t),
    ]


# Panel Item Flags
PPIF_SELECTED = 0x0000000040000000
PPIF_PROCESSDESCR = 0x0000000080000000
PPIF_NONE = 0


# Forward declarations for PluginPanelItem
class FarPanelItemFreeInfo(ctypes.Structure):
    pass


FARPANELITEMFREECALLBACK = ctypes.WINFUNCTYPE(
    None,
    ctypes.c_void_p,  # UserData
    ctypes.POINTER(FarPanelItemFreeInfo),  # Info
)


class UserDataItem(ctypes.Structure):
    _fields_ = [
        ("Data", ctypes.c_void_p),
        ("FreeData", FARPANELITEMFREECALLBACK),
    ]


class PluginPanelItem(ctypes.Structure):
    _fields_ = [
        ("CreationTime", FILETIME),
        ("LastAccessTime", FILETIME),
        ("LastWriteTime", FILETIME),
        ("ChangeTime", FILETIME),
        ("FileSize", ctypes.c_ulonglong),
        ("AllocationSize", ctypes.c_ulonglong),
        ("FileName", ctypes.c_wchar_p),
        ("AlternateFileName", ctypes.c_wchar_p),
        ("Description", ctypes.c_wchar_p),
        ("Owner", ctypes.c_wchar_p),
        ("CustomColumnData", ctypes.POINTER(ctypes.c_wchar_p)),
        ("CustomColumnNumber", size_t),
        ("Flags", ctypes.c_ulonglong),  # PLUGINPANELITEMFLAGS
        ("UserData", UserDataItem),
        ("FileAttributes", uintptr_t),
        ("NumberOfLinks", uintptr_t),
        ("CRC32", uintptr_t),
        ("Reserved", intptr_t * 2),
    ]


FarPanelItemFreeInfo._fields_ = [
    ("StructSize", size_t),
    ("hPlugin", HANDLE),
]


class FarGetPluginPanelItem(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Size", size_t),
        ("Item", ctypes.POINTER(PluginPanelItem)),
    ]


# Viewer Control Commands
VCTL_GETINFO = 0
VCTL_QUIT = 1
VCTL_REDRAW = 2
VCTL_SETKEYBAR = 3
VCTL_SETPOSITION = 4
VCTL_SELECT = 5
VCTL_SETMODE = 6
VCTL_GETFILENAME = 7


# Viewer Options Flags
VOPT_SAVEFILEPOSITION = 0x0000000000000001
VOPT_AUTODETECTCODEPAGE = 0x0000000000000002
VOPT_SHOWTITLEBAR = 0x0000000000000004
VOPT_SHOWKEYBAR = 0x0000000000000008
VOPT_SHOWSCROLLBAR = 0x0000000000000010
VOPT_QUICKVIEW = 0x0000000000000020


# Viewer SetMode Types
VSMT_VIEWMODE = 0
VSMT_WRAP = 1
VSMT_WORDWRAP = 2


# Viewer SetMode Flags
VSMFL_REDRAW = 0x0000000000000001


# Viewer SetPosition Flags
VSP_NOREDRAW = 0x0000000000000001
VSP_PERCENT = 0x0000000000000002
VSP_RELATIVE = 0x0000000000000004
VSP_NORETNEWPOS = 0x0000000000000008


# Viewer Mode Flags
VMF_WRAP = 0x0000000000000001
VMF_WORDWRAP = 0x0000000000000002


# Viewer Mode Types
VMT_TEXT = 0
VMT_HEX = 1
VMT_DUMP = 2


class ViewerMode(ctypes.Structure):
    _fields_ = [
        ("CodePage", uintptr_t),
        ("Flags", ctypes.c_ulonglong),
        ("ViewMode", ctypes.c_int),
    ]


class ViewerInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("ViewerID", intptr_t),
        ("TabSize", intptr_t),
        ("CurMode", ViewerMode),
        ("FileSize", ctypes.c_longlong),
        ("FilePos", ctypes.c_longlong),
        ("LeftPos", ctypes.c_longlong),
        ("Options", ctypes.c_ulonglong),
        ("WindowSizeX", intptr_t),
        ("WindowSizeY", intptr_t),
    ]


class ViewerSetPosition(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Flags", ctypes.c_ulonglong),
        ("StartPos", ctypes.c_longlong),
        ("LeftPos", ctypes.c_longlong),
    ]


class ViewerSelect(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("BlockStartPos", ctypes.c_longlong),
        ("BlockLen", ctypes.c_longlong),
    ]


class ViewerSetModeParam(ctypes.Union):
    _fields_ = [
        ("iParam", intptr_t),
        ("wszParam", ctypes.c_wchar_p),
    ]


class ViewerSetMode(ctypes.Structure):
    _anonymous_ = ("Param",)
    _fields_ = [
        ("StructSize", size_t),
        ("Type", ctypes.c_int),
        ("Param", ViewerSetModeParam),
        ("Flags", ctypes.c_ulonglong),
    ]


# Editor Control Commands
ECTL_GETSTRING = 0
ECTL_SETSTRING = 1
ECTL_INSERTSTRING = 2
ECTL_DELETESTRING = 3
ECTL_DELETECHAR = 4
ECTL_INSERTTEXT = 5
ECTL_GETINFO = 6
ECTL_SETPOSITION = 7
ECTL_SELECT = 8
ECTL_REDRAW = 9
ECTL_TABTOREAL = 10
ECTL_REALTOTAB = 11
ECTL_EXPANDTABS = 12
ECTL_SETTITLE = 13
ECTL_READINPUT = 14
ECTL_PROCESSINPUT = 15
ECTL_ADDCOLOR = 16
ECTL_GETCOLOR = 17
ECTL_SAVEFILE = 18
ECTL_QUIT = 19
ECTL_SETKEYBAR = 20
ECTL_SETPARAM = 22
ECTL_GETBOOKMARKS = 23
ECTL_DELETEBLOCK = 25
ECTL_ADDSESSIONBOOKMARK = 26
ECTL_PREVSESSIONBOOKMARK = 27
ECTL_NEXTSESSIONBOOKMARK = 28
ECTL_CLEARSESSIONBOOKMARKS = 29
ECTL_DELETESESSIONBOOKMARK = 30
ECTL_GETSESSIONBOOKMARKS = 31
ECTL_UNDOREDO = 32
ECTL_GETFILENAME = 33
ECTL_DELCOLOR = 34
ECTL_SUBSCRIBECHANGEEVENT = 36
ECTL_UNSUBSCRIBECHANGEEVENT = 37


# Editor Control Structures
class EditorGetString(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("StringNumber", intptr_t),
        ("StringLength", intptr_t),
        ("StringText", ctypes.c_wchar_p),
        ("StringEOL", ctypes.c_wchar_p),
        ("SelStart", intptr_t),
        ("SelEnd", intptr_t),
    ]


class EditorSetString(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("StringNumber", intptr_t),
        ("StringLength", intptr_t),
        ("StringText", ctypes.c_wchar_p),
        ("StringEOL", ctypes.c_wchar_p),
    ]


EUR_BEGIN = 0
EUR_END = 1
EUR_UNDO = 2
EUR_REDO = 3


class EditorUndoRedo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Command", ctypes.c_int),
    ]


class EditorSetPosition(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("CurLine", intptr_t),
        ("CurPos", intptr_t),
        ("CurTabPos", intptr_t),
        ("TopScreenLine", intptr_t),
        ("LeftPos", intptr_t),
        ("Overtype", intptr_t),
    ]


class EditorSelect(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("BlockType", intptr_t),
        ("BlockStartLine", intptr_t),
        ("BlockStartPos", intptr_t),
        ("BlockWidth", intptr_t),
        ("BlockHeight", intptr_t),
    ]


class EditorSaveFile(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("FileName", ctypes.c_wchar_p),
        ("FileEOL", ctypes.c_wchar_p),
        ("CodePage", uintptr_t),
    ]


# Editor Info Structure
class EditorInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("EditorID", intptr_t),
        ("WindowSizeX", intptr_t),
        ("WindowSizeY", intptr_t),
        ("TotalLines", intptr_t),
        ("CurLine", intptr_t),
        ("CurPos", intptr_t),
        ("CurTabPos", intptr_t),
        ("TopScreenLine", intptr_t),
        ("LeftPos", intptr_t),
        ("Overtype", intptr_t),
        ("BlockType", intptr_t),
        ("BlockStartLine", intptr_t),
        ("Options", ctypes.c_ulonglong),
        ("TabSize", intptr_t),
        ("BookmarkCount", size_t),
        ("SessionBookmarkCount", size_t),
        ("CurState", ctypes.c_ulonglong),
        ("CodePage", uintptr_t),
    ]


# Editor Bookmarks Structure
class EditorBookmarks(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Size", size_t),
        ("Count", size_t),
        ("Line", ctypes.POINTER(ctypes.c_longlong)),
        ("Cursor", ctypes.POINTER(ctypes.c_longlong)),
        ("ScreenLine", ctypes.POINTER(ctypes.c_longlong)),
        ("LeftPos", ctypes.POINTER(ctypes.c_longlong)),
    ]


# Editor Convert Position Structure
class EditorConvertPos(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("StringNumber", intptr_t),
        ("SrcPos", intptr_t),
        ("DestPos", intptr_t),
    ]


# Editor Color Flags
ECF_TABMARKFIRST = 0x0000000000000001
ECF_TABMARKCURRENT = 0x0000000000000002
ECF_AUTODELETE = 0x0000000000000004
ECF_NONE = 0

# Editor Color Priority
EDITOR_COLOR_NORMAL_PRIORITY = 0x80000000


# Editor Color Structure (for ECTL_GETCOLOR / ECTL_ADDCOLOR)
class EditorColor(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("StringNumber", intptr_t),
        ("ColorItem", intptr_t),
        ("StartPos", intptr_t),
        ("EndPos", intptr_t),
        ("Priority", uintptr_t),
        ("Flags", ctypes.c_ulonglong),  # EDITORCOLORFLAGS
        ("Color", FarColor),
        ("Owner", GUID),
    ]


# Editor Delete Color Structure (for ECTL_DELCOLOR)
class EditorDeleteColor(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Owner", GUID),
        ("StringNumber", intptr_t),
        ("StartPos", intptr_t),
    ]


# ... definitions for other function pointers would go here ...
# For now, we define them as generic generic pointers or None to save space/time until needed
FARAPIGETMSG = ctypes.WINFUNCTYPE(ctypes.c_wchar_p, ctypes.POINTER(GUID), intptr_t)


# OpenInfo
class OpenInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("OpenFrom", ctypes.c_int),  # OPENFROM enum
        ("Guid", ctypes.POINTER(GUID)),
        ("Data", intptr_t),
        ("Instance", ctypes.c_void_p),
    ]


# ClosePanelInfo – passed to ClosePanelW (plugin.hpp)
class ClosePanelInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("hPanel", ctypes.c_void_p),
        ("Instance", ctypes.c_void_p),
    ]


# ConfigureInfo – passed to ConfigureW (plugin.hpp)
class ConfigureInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Guid", ctypes.POINTER(GUID)),
        ("Instance", ctypes.c_void_p),
    ]


# ExitInfo – passed to ExitFARW (plugin.hpp)
class ExitInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Instance", ctypes.c_void_p),
    ]


# ProcessDialogEventInfo — passed to ProcessDialogEventW
class ProcessDialogEventInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Event", intptr_t),
        ("Param", ctypes.POINTER(FarDialogEvent)),
        ("Instance", ctypes.c_void_p),
    ]


class ProcessPanelEventInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", ctypes.c_size_t),
        ("Event", intptr_t),
        ("Param", ctypes.c_void_p),
        ("hPanel", HANDLE),
        ("Instance", ctypes.c_void_p),
    ]

class ProcessPanelInputInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", ctypes.c_size_t),
        ("hPanel", HANDLE),
        ("Rec", INPUT_RECORD),
        ("Instance", ctypes.c_void_p),
    ]

class ProcessHostFileInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", ctypes.c_size_t),
        ("hPanel", HANDLE),
        ("PanelItem", ctypes.POINTER(PluginPanelItem)),
        ("ItemsNumber", ctypes.c_size_t),
        ("OpMode", ctypes.c_ulonglong),
        ("Instance", ctypes.c_void_p),
    ]

class SetFindListInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", ctypes.c_size_t),
        ("hPanel", HANDLE),
        ("PanelItem", ctypes.POINTER(PluginPanelItem)),
        ("ItemsNumber", ctypes.c_size_t),
        ("Instance", ctypes.c_void_p),
    ]

class ProcessEditorEventInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Event", intptr_t),
        ("Param", ctypes.c_void_p),
        ("EditorID", intptr_t),
        ("Instance", ctypes.c_void_p),
    ]


class ProcessEditorInputInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Rec", INPUT_RECORD),
        ("Instance", ctypes.c_void_p),
    ]


class ProcessViewerEventInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Event", intptr_t),
        ("Param", ctypes.c_void_p),
        ("ViewerID", intptr_t),
        ("Instance", ctypes.c_void_p),
    ]


class ProcessConsoleInputInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("Flags", ctypes.c_ulonglong),
        ("Rec", INPUT_RECORD),
        ("Instance", ctypes.c_void_p),
    ]


# Supporting structs for OpenPanelInfo
class InfoPanelLine(ctypes.Structure):
    _fields_ = [
        ("Text", ctypes.c_wchar_p),
        ("Data", ctypes.c_wchar_p),
        ("Flags", ctypes.c_ulonglong),  # INFOPANELLINE_FLAGS
    ]

# PanelMode flags
PANELMODE_FLAGS = ctypes.c_ulonglong
PMFLAGS_FULLSCREEN      = 0x0000000000000001
PMFLAGS_DETAILEDSTATUS  = 0x0000000000000002
PMFLAGS_ALIGNEXTENSIONS = 0x0000000000000004
PMFLAGS_CASECONVERSION  = 0x0000000000000008
PMFLAGS_NONE            = 0

class PanelMode(ctypes.Structure):
    _fields_ = [
        ("ColumnTypes", ctypes.c_wchar_p),
        ("ColumnWidths", ctypes.c_wchar_p),
        ("ColumnTitles", ctypes.POINTER(ctypes.c_wchar_p)),
        ("StatusColumnTypes", ctypes.c_wchar_p),
        ("StatusColumnWidths", ctypes.c_wchar_p),
        ("Flags", PANELMODE_FLAGS),
    ]

class KeyBarTitles(ctypes.Structure):
    _fields_ = [
        ("Titles", ctypes.POINTER(ctypes.c_wchar_p)),
        ("TitlesNumber", ctypes.c_int),
        ("KeyBarRect", SMALL_RECT),
        ("CtrlKeyBarRect", SMALL_RECT),
    ]


class CompareInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("hPanel", HANDLE),
        ("Item1", ctypes.POINTER(PluginPanelItem)),
        ("Item2", ctypes.POINTER(PluginPanelItem)),
        ("Mode", size_t), # enum OPENPANELINFO_SORTMODES
        ("Instance", ctypes.c_void_p),
    ]

class OpenPanelInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("hPanel", HANDLE),
        ("Flags", ctypes.c_ulonglong),  # OPENPANELINFO_FLAGS
        ("HostFile", ctypes.c_wchar_p),
        ("CurDir", ctypes.c_wchar_p),
        ("Format", ctypes.c_wchar_p),
        ("PanelTitle", ctypes.c_wchar_p),
        ("InfoLines", ctypes.POINTER(InfoPanelLine)),
        ("InfoLinesNumber", size_t),
        ("DescrFiles", ctypes.POINTER(ctypes.c_wchar_p)),
        ("DescrFilesNumber", size_t),
        ("PanelModesArray", ctypes.POINTER(PanelMode)),
        ("PanelModesNumber", size_t),
        ("StartPanelMode", ctypes.c_longlong),
        ("StartSortMode", ctypes.c_int),  # OPENPANELINFO_SORTMODES
        ("StartSortOrder", ctypes.c_longlong),
        ("KeyBar", ctypes.POINTER(KeyBarTitles)),
        ("ShortcutData", ctypes.c_wchar_p),
        ("FreeSize", ctypes.c_ulonglong),
        ("UserData", UserDataItem),
        ("Instance", ctypes.c_void_p),
    ]


# Constants for OPENFROM
OPEN_LEFTDISKMENU = 0
OPEN_PLUGINSMENU = 1
# ...

# CodePages
CP_DEFAULT = 0xFFFFFFFFFFFFFFFF
CP_UNICODE = 1200
CP_UTF8 = 65001

# Flags for Menu
FMENU_SHOWAMPERSAND = 0x0000000000000001
OPEN_FINDLIST = 2
OPEN_SHORTCUT = 3
OPEN_COMMANDLINE = 4
OPEN_EDITOR = 5
OPEN_VIEWER = 6
OPEN_FILEPANEL = 7
OPEN_DIALOG = 8
OPEN_ANALYSE = 9
OPEN_RIGHTDISKMENU = 10
OPEN_FROMMACRO = 11
OPEN_LUAMACRO = 100

# OPENPANELINFO_FLAGS
OPENPANELINFO_FLAGS = ctypes.c_ulonglong
OPIF_DISABLEFILTER = 0x0000000000000001
OPIF_DISABLESORTGROUPS = 0x0000000000000002
OPIF_DISABLEHIGHLIGHTING = 0x0000000000000004
OPIF_ADDDOTS = 0x0000000000000008
OPIF_RAWSELECTION = 0x0000000000000010
OPIF_REALNAMES = 0x0000000000000020
OPIF_SHOWNAMESONLY = 0x0000000000000040
OPIF_SHOWRIGHTALIGNNAMES = 0x0000000000000080
OPIF_SHOWPRESERVECASE = 0x0000000000000100
OPIF_COMPAREFATTIME = 0x0000000000000400
OPIF_EXTERNALGET = 0x0000000000000800
OPIF_EXTERNALPUT = 0x0000000000001000
OPIF_EXTERNALDELETE = 0x0000000000002000
OPIF_EXTERNALMKDIR = 0x0000000000004000
OPIF_USEATTRHIGHLIGHTING = 0x0000000000008000
OPIF_USECRC32 = 0x0000000000010000
OPIF_USEFREESIZE = 0x0000000000020000
OPIF_SHORTCUT = 0x0000000000040000
OPIF_RECURSIVEPANEL = 0x0000000000080000
OPIF_NONE = 0

# PLUGIN_FLAGS
PLUGIN_FLAGS = ctypes.c_ulonglong
PF_PRELOAD = 0x0000000000000001
PF_DISABLEPANELS = 0x0000000000000002
PF_EDITOR = 0x0000000000000004
PF_VIEWER = 0x0000000000000008
PF_FULLCMDLINE = 0x0000000000000010
PF_DIALOG = 0x0000000000000020
PF_NONE = 0

# FAR_PLUGIN_FLAGS
FAR_PLUGIN_FLAGS = ctypes.c_ulonglong
FPF_LOADED = 0x0000000000000001
FPF_ANSI = 0x1000000000000000
FPF_NONE = 0

# OPENPANELINFO_SORTMODES
SM_DEFAULT = 0
SM_UNSORTED = 1
SM_NAME = 2
SM_FULLNAME = 2
SM_EXT = 3
SM_MTIME = 4
SM_CTIME = 5
SM_ATIME = 6
SM_SIZE = 7
SM_DESCR = 8
SM_OWNER = 9
SM_COMPRESSEDSIZE = 10
SM_NUMLINKS = 11
SM_NUMSTREAMS = 12
SM_STREAMSSIZE = 13
SM_NAMEONLY = 14
SM_CHTIME = 15

# Flags for Menu
FMENU_SHOWAMPERSAND = 0x0000000000000001
FMENU_WRAPMODE = 0x0000000000000002
FMENU_AUTOHIGHLIGHT = 0x0000000000000004
FMENU_REVERSEAUTOHIGHLIGHT = 0x0000000000000008

# Flags for InputBox
FIB_ENABLEEMPTY = 0x0000000000000001
FIB_PASSWORD = 0x0000000000000002
FIB_EXPANDENV = 0x0000000000000004
FIB_NOUSELASTHISTORY = 0x0000000000000008
FIB_BUTTONS = 0x0000000000000010
FIB_NOAMPERSAND = 0x0000000000000020
FIB_EDITPATH = 0x0000000000000040


# OpenShortcutInfo
class OpenShortcutInfo(ctypes.Structure):
    _fields_ = [
        ("HostFile", ctypes.c_wchar_p),
        ("ShortcutData", ctypes.c_wchar_p),
        ("Flags", ctypes.c_ulonglong),  # FARMACROCALLFLAGS
    ]


# OpenCommandLineInfo
class AnalyseInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", ctypes.c_size_t),
        ("FileName", ctypes.c_wchar_p),
        ("Buffer", ctypes.c_void_p),
        ("BufferSize", ctypes.c_size_t),
        ("OpMode", ctypes.c_ulonglong),
        ("Instance", ctypes.c_void_p),
    ]

class OpenAnalyseInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", ctypes.c_size_t),
        ("Info", ctypes.POINTER(AnalyseInfo)),
        ("Handle", HANDLE),
    ]

class CloseAnalyseInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", ctypes.c_size_t),
        ("Handle", HANDLE),
        ("Instance", ctypes.c_void_p),
    ]

class OpenCommandLineInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("CommandLine", ctypes.c_wchar_p),
    ]


# Directory List API functions
FARAPIGETDIRLIST = ctypes.WINFUNCTYPE(
    intptr_t,
    ctypes.c_wchar_p,  # Dir
    ctypes.POINTER(ctypes.POINTER(PluginPanelItem)),  # pPanelItem
    ctypes.POINTER(size_t),  # pItemsNumber
)

FARAPIGETPLUGINDIRLIST = ctypes.WINFUNCTYPE(
    intptr_t,
    ctypes.POINTER(GUID),  # PluginId
    HANDLE,  # hPanel
    ctypes.c_wchar_p,  # Dir
    ctypes.POINTER(ctypes.POINTER(PluginPanelItem)),  # pPanelItem
    ctypes.POINTER(size_t),  # pItemsNumber
)

FARAPIFREEDIRLIST = ctypes.WINFUNCTYPE(
    None,
    ctypes.POINTER(PluginPanelItem),  # PanelItem
    size_t,  # nItemsNumber
)

FARAPIFREEPLUGINDIRLIST = ctypes.WINFUNCTYPE(
    None,
    HANDLE,  # hPanel
    ctypes.POINTER(PluginPanelItem),  # PanelItem
    size_t,  # nItemsNumber
)

# PluginStartupInfo
PluginStartupInfo._fields_ = [
    ("StructSize", size_t),
    ("ModuleName", ctypes.c_wchar_p),
    ("Menu", FARAPIMENU),
    ("Message", FARAPIMESSAGE),
    ("GetMsg", FARAPIGETMSG),
    ("PanelControl", FARAPIPANELCONTROL),
    ("SaveScreen", FARAPISAVESCREEN),
    ("RestoreScreen", FARAPIRESTORESCREEN),
    ("GetDirList", FARAPIGETDIRLIST),
    ("GetPluginDirList", FARAPIGETPLUGINDIRLIST),
    ("FreeDirList", FARAPIFREEDIRLIST),
    ("FreePluginDirList", FARAPIFREEPLUGINDIRLIST),
    ("Viewer", FARAPIVIEWER),
    ("Editor", FARAPIEDITOR),
    ("Text", FARAPITEXT),
    ("EditorControl", FARAPIEDITORCONTROL),
    ("FSF", ctypes.POINTER(FarStandardFunctions)),
    ("ShowHelp", FARAPISHOWHELP),
    ("AdvControl", FARAPIADVCONTROL),
    ("InputBox", FARAPIINPUTBOX),
    ("ColorDialog", FARAPICOLORDIALOG),
    ("DialogInit", FARAPIDIALOGINIT),
    ("DialogRun", FARAPIDIALOGRUN),
    ("DialogFree", FARAPIDIALOGFREE),
    ("SendDlgMessage", FARAPISENDDLGMESSAGE),
    ("DefDlgProc", FARAPIDEFDLGPROC),
    ("ViewerControl", FARAPIVIEWERCONTROL),
    ("PluginsControl", FARAPIPLUGINSCONTROL),
    ("FileFilterControl", FARAPIFILEFILTERCONTROL),
    ("RegExpControl", FARAPIREGEXPCONTROL),
    ("MacroControl", FARAPIMACROCONTROL),
    ("SettingsControl", FARAPISETTINGSCONTROL),
    ("Private", LPVOID),
    ("Instance", LPVOID),
    ("FreeScreen", FARAPIFREESCREEN),
]

# VFS plugin export structs (Far calls these on the plugin, not PSI function pointers)
class GetFindDataInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("hPanel", HANDLE),
        ("PanelItem", ctypes.POINTER(PluginPanelItem)),  # OUTPUT
        ("ItemsNumber", size_t),  # OUTPUT
        ("OpMode", ctypes.c_ulonglong),  # OPERATION_MODES
        ("Instance", ctypes.c_void_p),
    ]

class FreeFindDataInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("hPanel", HANDLE),
        ("PanelItem", ctypes.POINTER(PluginPanelItem)),
        ("ItemsNumber", size_t),
        ("Instance", ctypes.c_void_p),
    ]

class SetDirectoryInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("hPanel", HANDLE),
        ("Dir", ctypes.c_wchar_p),
        ("Reserved", ctypes.c_longlong),
        ("OpMode", ctypes.c_ulonglong),
        ("UserData", UserDataItem),
        ("Instance", ctypes.c_void_p),
    ]

class DeleteFilesInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("hPanel", HANDLE),
        ("PanelItem", ctypes.POINTER(PluginPanelItem)),
        ("ItemsNumber", size_t),
        ("OpMode", ctypes.c_ulonglong),
        ("Instance", ctypes.c_void_p),
    ]

class MakeDirectoryInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("hPanel", HANDLE),
        ("Name", ctypes.c_wchar_p),
        ("OpMode", ctypes.c_ulonglong),
        ("Instance", ctypes.c_void_p),
    ]

class GetFilesInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("hPanel", HANDLE),
        ("PanelItem", ctypes.POINTER(PluginPanelItem)),
        ("ItemsNumber", size_t),
        ("Move", ctypes.c_int),  # BOOL
        ("DestPath", ctypes.c_wchar_p),
        ("OpMode", ctypes.c_ulonglong),
        ("Instance", ctypes.c_void_p),
    ]

class PutFilesInfo(ctypes.Structure):
    _fields_ = [
        ("StructSize", size_t),
        ("hPanel", HANDLE),
        ("PanelItem", ctypes.POINTER(PluginPanelItem)),
        ("ItemsNumber", size_t),
        ("Move", ctypes.c_int),  # BOOL
        ("SrcPath", ctypes.c_wchar_p),
        ("OpMode", ctypes.c_ulonglong),
        ("Instance", ctypes.c_void_p),
    ]

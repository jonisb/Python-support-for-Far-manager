#include <cstddef>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../include/plugin.hpp"

#define DUMP_STRUCT_START(name) out << "    \"" #name "\": {\n" << "        \"sizeof\": " << sizeof(name) << ",\n" << "        \"fields\": {\n"
#define DUMP_STRUCT_START_NAMED(struct_name, json_name) out << "    \"" #json_name "\": {\n" << "        \"sizeof\": " << sizeof(struct_name) << ",\n" << "        \"fields\": {\n"
#define DUMP_FIELD(struct_name, field_name) out << "            \"" #field_name "\": " << offsetof(struct_name, field_name) << ",\n"
#define DUMP_FIELD_LAST(struct_name, field_name) out << "            \"" #field_name "\": " << offsetof(struct_name, field_name) << "\n"
#define DUMP_STRUCT_END() out << "        }\n    },\n"
#define DUMP_STRUCT_END_LAST() out << "        }\n    }\n"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <output.json>" << std::endl;
        return 1;
    }
    
    std::ofstream out(argv[1]);
    if (!out) {
        std::cerr << "Failed to open output file: " << argv[1] << std::endl;
        return 1;
    }

    out << "{\n";

    DUMP_STRUCT_START(PluginStartupInfo);
    DUMP_FIELD(PluginStartupInfo, StructSize);
    DUMP_FIELD(PluginStartupInfo, ModuleName);
    DUMP_FIELD(PluginStartupInfo, Menu);
    DUMP_FIELD(PluginStartupInfo, Message);
    DUMP_FIELD(PluginStartupInfo, GetMsg);
    DUMP_FIELD(PluginStartupInfo, PanelControl);
    DUMP_FIELD(PluginStartupInfo, SaveScreen);
    DUMP_FIELD(PluginStartupInfo, RestoreScreen);
    DUMP_FIELD(PluginStartupInfo, GetDirList);
    DUMP_FIELD(PluginStartupInfo, GetPluginDirList);
    DUMP_FIELD(PluginStartupInfo, FreeDirList);
    DUMP_FIELD(PluginStartupInfo, FreePluginDirList);
    DUMP_FIELD(PluginStartupInfo, Viewer);
    DUMP_FIELD(PluginStartupInfo, Editor);
    DUMP_FIELD(PluginStartupInfo, Text);
    DUMP_FIELD(PluginStartupInfo, EditorControl);
    DUMP_FIELD(PluginStartupInfo, FSF);
    DUMP_FIELD(PluginStartupInfo, ShowHelp);
    DUMP_FIELD(PluginStartupInfo, AdvControl);
    DUMP_FIELD(PluginStartupInfo, InputBox);
    DUMP_FIELD(PluginStartupInfo, ColorDialog);
    DUMP_FIELD(PluginStartupInfo, DialogInit);
    DUMP_FIELD(PluginStartupInfo, DialogRun);
    DUMP_FIELD(PluginStartupInfo, DialogFree);
    DUMP_FIELD(PluginStartupInfo, SendDlgMessage);
    DUMP_FIELD(PluginStartupInfo, DefDlgProc);
    DUMP_FIELD(PluginStartupInfo, ViewerControl);
    DUMP_FIELD(PluginStartupInfo, PluginsControl);
    DUMP_FIELD(PluginStartupInfo, FileFilterControl);
    DUMP_FIELD(PluginStartupInfo, RegExpControl);
    DUMP_FIELD(PluginStartupInfo, MacroControl);
    DUMP_FIELD(PluginStartupInfo, SettingsControl);
    DUMP_FIELD(PluginStartupInfo, Private);
    DUMP_FIELD(PluginStartupInfo, Instance);
    DUMP_FIELD_LAST(PluginStartupInfo, FreeScreen);
    DUMP_STRUCT_END();

    DUMP_STRUCT_START(PanelInfo);
    DUMP_FIELD(PanelInfo, StructSize);
    DUMP_FIELD(PanelInfo, PluginHandle);
    DUMP_FIELD(PanelInfo, OwnerGuid);
    DUMP_FIELD(PanelInfo, Flags);
    DUMP_FIELD(PanelInfo, ItemsNumber);
    DUMP_FIELD(PanelInfo, SelectedItemsNumber);
    DUMP_FIELD(PanelInfo, PanelRect);
    DUMP_FIELD(PanelInfo, CurrentItem);
    DUMP_FIELD(PanelInfo, TopPanelItem);
    DUMP_FIELD(PanelInfo, ViewMode);
    DUMP_FIELD(PanelInfo, PanelType);
    DUMP_FIELD_LAST(PanelInfo, SortMode);
    DUMP_STRUCT_END();

    DUMP_STRUCT_START(OpenInfo);
    DUMP_FIELD(OpenInfo, StructSize);
    DUMP_FIELD(OpenInfo, OpenFrom);
    DUMP_FIELD(OpenInfo, Guid);
    DUMP_FIELD(OpenInfo, Data);
    DUMP_FIELD_LAST(OpenInfo, Instance);
    DUMP_STRUCT_END();

    DUMP_STRUCT_START(PluginInfo);
    DUMP_FIELD(PluginInfo, StructSize);
    DUMP_FIELD(PluginInfo, Flags);
    DUMP_FIELD(PluginInfo, DiskMenu);
    DUMP_FIELD(PluginInfo, PluginMenu);
    DUMP_FIELD(PluginInfo, PluginConfig);
    DUMP_FIELD(PluginInfo, CommandPrefix);
    DUMP_FIELD_LAST(PluginInfo, Instance);
    DUMP_STRUCT_END();

    DUMP_STRUCT_START_NAMED(RECT, Rect);
    DUMP_FIELD(RECT, left);
    DUMP_FIELD(RECT, top);
    DUMP_FIELD(RECT, right);
    DUMP_FIELD_LAST(RECT, bottom);
    DUMP_STRUCT_END_LAST();

    out << "}\n";
    return 0;
}

// The MonoCore.E3ImGUI internal calls: the ImGui windows a script draws (MQ2Mono's MQ2MonoImGui.cpp).
#include "hsmono.hpp"

// From RegisterInternalCalls (hsmono.cpp). Each line stays commented out until its mono_ImGUI_* function is in this file
// (above this point, or declared there).
void RegisterImGuiInternalCalls() {
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ColorPicker4", mono_ImGUI_ColorPicker4);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ColorPicker_GetRGBA", mono_ImGUI_ColorPicker_GetRGBA);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ColorPicker4_Float", mono_ImGUI_ColorPicker4_Float);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ColorPicker_GetRGBA_Float", mono_ImGUI_ColorPicker_GetRGBA_Float);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetStyleColorVec4", mono_ImGUI_GetStyleColorVec4);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ColorPicker_Clear", mono_ImGUI_ColorPicker_Clear);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Begin", mono_ImGUI_Begin);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Button", mono_ImGUI_Button);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ButtonEx", mono_ImGUI_ButtonEx);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InvisibleButton", mono_ImGUI_InvisibleButton);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SmallButton", mono_ImGUI_SmallButton);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_End", mono_ImGUI_End);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Begin_OpenFlagSet", mono_ImGUI_Begin_OpenFlagSet);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Begin_OpenFlagGet", mono_ImGUI_Begin_OpenFlagGet);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Text", mono_ImGUI_Text);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Separator", mono_ImGUI_Separator);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SameLine", mono_ImGUI_SameLine);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SameLineEx", mono_ImGUI_SameLineEx);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Checkbox", mono_ImGUI_Checkbox);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Checkbox_Clear", mono_ImGUI_Checkbox_Clear);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Checkbox_Get", mono_ImGUI_Checkbox_Get);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginTabBar", mono_ImGUI_BeginTabBar);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndTabBar", mono_ImGUI_EndTabBar);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginTabItem", mono_ImGUI_BeginTabItem);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndTabItem", mono_ImGUI_EndTabItem);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushID", mono_ImGUI_PushID);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PopID", mono_ImGUI_PopID);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushItemWidth", mono_ImGUI_PushItemWidth);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PopItemWidth", mono_ImGUI_PopItemWidth);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginChild", mono_ImGUI_BeginChild);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndChild", mono_ImGUI_EndChild);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Selectable", mono_ImGUI_Selectable);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Selectable_WithFlags", mono_ImGUI_Selectable_WithFlags);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetContentRegionAvailX", mono_ImGUI_GetContentRegionAvailX);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetContentRegionAvailY", mono_ImGUI_GetContentRegionAvailY);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputText", mono_ImGUI_InputText);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputText_Clear", mono_ImGUI_InputText_Clear);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_CalcTextSizeX", mono_ImGUI_CalcTextSizeX);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_CalcTextSize", mono_ImGUI_CalcTextSize);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowContentRegionMinX", mono_ImGUI_GetWindowContentRegionMin_X);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowContentRegionMinY", mono_ImGUI_GetWindowContentRegionMin_Y);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowContentRegionMaxX", mono_ImGUI_GetWindowContentRegionMax_X);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowContentRegionMaxY", mono_ImGUI_GetWindowContentRegionMax_Y);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextWindowPos", mono_ImGUI_SetNextWindowPos);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetItemRectSize", mono_ImGUI_GetItemRectSize);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowPosX", mono_ImGUI_GetWindowPosX);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowPosY", mono_ImGUI_GetWindowPosY);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowSizeX", mono_ImGUI_GetWindowSizeX);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowSizeY", mono_ImGUI_GetWindowSizeY);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextWindowFocus", mono_ImGUI_SetNextWindowFocus);
    //mono_ImGUI_InputTextClear
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputTextMultiline", mono_ImGUI_InputTextMultiline);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputText_Get", mono_ImGUI_InputText_Get);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextItemWidth", mono_ImGUI_SetNextItemWidth);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowHeight", mono_ImGUI_GetWindowHeight);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowWidth", mono_ImGUI_GetWindowWidth);

    // Combo wrappers
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginCombo", mono_ImGUI_BeginCombo);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndCombo", mono_ImGUI_EndCombo);
    // Right align helper
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_RightAlignButton", mono_ImGUI_RightAlignButton);

    // Context menus / popups
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginPopupContextItem", mono_ImGUI_BeginPopupContextItem);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginPopupContextWindow", mono_ImGUI_BeginPopupContextWindow);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndPopup", mono_ImGUI_EndPopup);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_MenuItem", mono_ImGUI_MenuItem);

    //Progress bar
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ProgressBar", mono_ImGUI_ProgressBar);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ProgressBarGradient", mono_ImGUI_ProgressBarGradient);

    // Tables
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginTable", mono_ImGUI_BeginTable);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginTableS", mono_ImGUI_BeginTableSimple);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndTable", mono_ImGUI_EndTable);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableSetupColumn", mono_ImGUI_TableSetupColumn);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableSetupColumn_Default", mono_ImGUI_TableSetupColumn_Default);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableSetBgColor", mono_ImGUI_TableSetBgColor);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableSetColumnIndex", mono_ImGUI_TableSetColumnIndex);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableHeadersRow", mono_ImGUI_TableHeadersRow);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableHeader", mono_ImGUI_TableHeader);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableGetColumnName", mono_ImGUI_TableGetColumnName);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableNextRow", mono_ImGUI_TableNextRow);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableNextColumn", mono_ImGUI_TableNextColumn);

    // Colors / styled text
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TextColored", mono_ImGUI_TextColored);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushStyleColor", mono_ImGUI_PushStyleColor);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PopStyleColor", mono_ImGUI_PopStyleColor);

    // Style variables
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushStyleVarFloat", mono_ImGUI_PushStyleVarFloat);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushStyleVarVec2", mono_ImGUI_PushStyleVarVec2);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PopStyleVar", mono_ImGUI_PopStyleVar);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetStyleVarFloat", mono_ImGUI_GetStyleVarFloat);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetStyleVarVec2", mono_ImGUI_GetStyleVarVec2);

    // Text wrapping and window sizing
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TextWrapped", mono_ImGUI_TextWrapped);
    // Expose unformatted text render to avoid printf-style format crashes
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TextUnformatted", mono_ImGUI_TextUnformatted);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushTextWrapPos", mono_ImGUI_PushTextWrapPos);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PopTextWrapPos", mono_ImGUI_PopTextWrapPos);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextWindowSizeConstraints", mono_ImGUI_SetNextWindowSizeConstraints);

    // New window control functions
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextWindowBgAlpha", mono_ImGUI_SetNextWindowBgAlpha);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextWindowSize", mono_ImGUI_SetNextWindowSize);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextWindowSizeWithCond", mono_ImGUI_SetNextWindowSizeWithCond);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_IsWindowHovered", mono_ImGUI_IsWindowHovered);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowWidth", mono_ImGUI_GetWindowWidth);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowHeight", mono_ImGUI_GetWindowHeight);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_IsMouseClicked", mono_ImGUI_IsMouseClicked);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_IsMouseDoubleClicked", mono_ImGUI_IsMouseDoubleClicked);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_IsItemClicked", mono_ImGUI_IsItemClicked);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetClipboardText", mono_ImGUI_SetClipboardText);

    // Input int
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputInt", mono_ImGUI_InputInt);
    //mono_ImGUI_InputTextClear
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputInt_Get", mono_ImGUI_InputInt_Get);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputInt_Clear", mono_ImGUI_InputInt_Clear);
    //bool mono_ImGUI_InputIntClear(MonoString* id)

    //mono_ImGUI_InputInt

    // Sliders
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SliderInt", mono_ImGUI_SliderInt);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SliderDouble", mono_ImGUI_SliderDouble);

    // Tree nodes
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TreeNode", mono_ImGUI_TreeNode);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TreeNodeEx", mono_ImGUI_TreeNodeEx);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TreePop", mono_ImGUI_TreePop);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_CollapsingHeader", mono_ImGUI_CollapsingHeader);

    // Tooltips and hover detection
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_IsItemHovered", mono_ImGUI_IsItemHovered);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginTooltip", mono_ImGUI_BeginTooltip);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndTooltip", mono_ImGUI_EndTooltip);

    // Image display
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Image", mono_ImGUI_Image);

    // Fonts
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_AddFontFromFileTTF", mono_ImGUI_AddFontFromFileTTF);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushFont", mono_ImGUI_PushFont);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PopFont", mono_ImGUI_PopFont);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushEQFont", mono_ImGUI_PushEQFont);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Style_GetFontSizeBase", mono_ImGUI_Style_GetFontSizeBase);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushMaterialIconsFont", mono_ImGUI_PushMaterialIconsFont);

    // Spell icon drawing
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_DrawSpellIconByIconIndex", mono_ImGUI_DrawSpellIconByIconIndex);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_DrawSpellIconBySpellID", mono_ImGUI_DrawSpellIconBySpellID);

    // Drawing functions for custom backgrounds
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableNextRowEx", mono_ImGUI_TableNextRowEx);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetCursorPosX", mono_ImGUI_GetCursorPosX);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetCursorPosX", mono_ImGUI_SetCursorPosX);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetCursorPosY", mono_ImGUI_GetCursorPosY);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetCursorPosY", mono_ImGUI_SetCursorPosY);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetCursorScreenPosX", mono_ImGUI_GetCursorScreenPosX);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetCursorScreenPosY", mono_ImGUI_GetCursorScreenPosY);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetTextLineHeightWithSpacing", mono_ImGUI_GetTextLineHeightWithSpacing);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetTextLineHeight", mono_ImGUI_GetTextLineHeight);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetFrameHeight", mono_ImGUI_GetFrameHeight);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddRectFilled", mono_ImGUI_GetWindowDrawList_AddRectFilled);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddRect", mono_ImGUI_GetWindowDrawList_AddRect);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddRectFilledMultiColor", mono_ImGUI_GetWindowDrawList_AddRectFilledMultiColor);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddQuad", mono_ImGUI_GetWindowDrawList_AddQuad);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddQuadFilled", mono_ImGUI_GetWindowDrawList_AddQuadFilled);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddTriangle", mono_ImGUI_GetWindowDrawList_AddTriangle);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddTriangleFilled", mono_ImGUI_GetWindowDrawList_AddTriangleFilled);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddCircle", mono_ImGUI_GetWindowDrawList_AddCircle);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddCircleFilled", mono_ImGUI_GetWindowDrawList_AddCircleFilled);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddNgon", mono_ImGUI_GetWindowDrawList_AddNgon);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddNgonFilled", mono_ImGUI_GetWindowDrawList_AddNgonFilled);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddEllipse", mono_ImGUI_GetWindowDrawList_AddEllipse);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddEllipseFilled", mono_ImGUI_GetWindowDrawList_AddEllipseFilled);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddBezierCubic", mono_ImGUI_GetWindowDrawList_AddBezierCubic);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddBezierQuadratic", mono_ImGUI_GetWindowDrawList_AddBezierQuadratic);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Internal_ItemSize", mono_ImGUI_Internal_ItemSize);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Internal_ItemAdd", mono_ImGUI_Internal_ItemAdd);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Internal_CalcItemSize", mono_ImGUI_Internal_CalcItemSize);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddLine", mono_ImGUI_GetWindowDrawList_AddLine);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddText", mono_ImGUI_GetWindowDrawList_AddText);

    // Item rect + color helpers
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetItemRectMinX", mono_ImGUI_GetItemRectMinX);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetItemRectMinY", mono_ImGUI_GetItemRectMinY);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetItemRectMaxX", mono_ImGUI_GetItemRectMaxX);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetItemRectMaxY", mono_ImGUI_GetItemRectMaxY);
    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetItemRectMin", mono_ImGUI_GetItemRectMin);

    // HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetColorU32", mono_ImGUI_GetColorU32);

    // Texture creation from raw data
    // HSMONO_ICALL("MonoCore.E3ImGUI::mq_CreateTextureFromData", mono_CreateTextureFromData);
    // HSMONO_ICALL("MonoCore.E3ImGUI::mq_DestroyTexture", mono_DestroyTexture);
}

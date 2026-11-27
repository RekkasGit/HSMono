// The MonoCore.E3ImGUI internal calls: the ImGui windows a script draws from its OnUpdateImGui (MQ2Mono's
// MQ2MonoImGui.cpp). They draw into the client's own Dear ImGui context (host->imgui_context), with this plugin's copy
// of the same Dear ImGui version (plugins/hsmono/CMakeLists.txt).
//
// Written for Dear ImGui 1.93, the client's version. A script passes Dear ImGui's enums as plain numbers, and they go
// to Dear ImGui as they are: the C# side's enums (E3ImGui.cs) give 1.93's numbers, and imgui_GetVersionNum and
// imgui_HasDocking tell a script which Dear ImGui it is drawing with. Where Hailstone has no counterpart, a call does
// the safest thing and says so in its comment: MQ's EverQuest fonts, spell icons and textures (no plugin API for them
// yet).
#include "hsmono.hpp"

#include <cstring>
#include <deque>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"   // ItemSize, ItemAdd, CalcItemSize, the current window, error recovery
#include "imgui_stdlib.h"     // InputText into a std::string

static_assert(IMGUI_VERSION_NUM >= 19300, "hsmono_imgui.cpp is written for Dear ImGui 1.93");

namespace {

// ---- helpers

std::string Str(MonoString* s) {
    if (!s) return {};
    char* c = mono.mono_string_to_utf8(s);
    std::string out(c ? c : "");
    if (c) mono.mono_free(c);
    return out;
}

MonoDomain* CurrentDomain() {
    MonoDomain* d = mono.mono_domain_get();
    return d ? d : g_rootDomain;
}

MonoString* NewString(const char* s) { return mono.mono_string_new(CurrentDomain(), s ? s : ""); }

// A float[] or int[] for the C# side, in the calling script's domain.
MonoArray* FloatArray(std::initializer_list<float> values) {
    MonoArray* a = mono.mono_array_new(CurrentDomain(), mono.mono_get_single_class(), values.size());
    uintptr_t i = 0;
    for (float v : values) *reinterpret_cast<float*>(mono.mono_array_addr_with_size(a, sizeof(float), i++)) = v;
    return a;
}
MonoArray* IntArray(std::initializer_list<int32_t> values) {
    MonoArray* a = mono.mono_array_new(CurrentDomain(), mono.mono_get_int32_class(), values.size());
    uintptr_t i = 0;
    for (int32_t v : values) *reinterpret_cast<int32_t*>(mono.mono_array_addr_with_size(a, sizeof(int32_t), i++)) = v;
    return a;
}

// The state a script's widgets keep between frames (its maps in monoAppDomainInfo); null from the root domain.
monoAppDomainInfo* Script() { return CurrentScript(); }

// A style variable's slot in ImGuiStyle when it holds `count` floats (1 or 2); null for an unknown or other one.
float* StyleVarSlot(int var, unsigned count) {
    if (var < 0 || var >= ImGuiStyleVar_COUNT) return nullptr;   // GetStyleVarInfo asserts on one out of range
    const ImGuiStyleVarInfo* info = ImGui::GetStyleVarInfo(var);
    if (!info || info->DataType != ImGuiDataType_Float || info->Count != count) return nullptr;
    return static_cast<float*>(info->GetVarPtr(&ImGui::GetStyle()));
}

// Since 1.92.9 Dear ImGui writes a typed number back only when it is confirmed; MQ2Mono's scripts read it back every
// frame, as Dear ImGui did before. Held around each number widget.
struct LiveEdit {
    LiveEdit() { ImGui::PushItemFlag(ImGuiItemFlags_LiveEditOnInputScalar, true); }
    ~LiveEdit() { ImGui::PopItemFlag(); }
    LiveEdit(const LiveEdit&) = delete;
    LiveEdit& operator=(const LiveEdit&) = delete;
};

// ---- windows

void mono_ImGUI_Begin_OpenFlagSet(MonoString* name, bool open) {
    if (monoAppDomainInfo* s = Script()) s->m_IMGUI_OpenWindows[Str(name)] = open;
}

MonoBoolean mono_ImGUI_Begin_OpenFlagGet(MonoString* name) {
    monoAppDomainInfo* s = Script();
    if (!s) return 0;
    auto [it, added] = s->m_IMGUI_OpenWindows.try_emplace(Str(name), true);   // a window starts open
    return it->second ? 1 : 0;
}

bool mono_ImGUI_Begin(MonoString* name, int flags) {
    monoAppDomainInfo* s = Script();
    if (!s) return false;
    const std::string str = Str(name);
    s->m_CurrentWindow = str;
    auto [it, added] = s->m_IMGUI_OpenWindows.try_emplace(str, true);
    return ImGui::Begin(str.c_str(), &it->second, flags);
}

void mono_ImGUI_End() { ImGui::End(); }

bool mono_ImGUI_BeginChild(MonoString* id, float width, float height, int child_flags, int window_flags) {
    return ImGui::BeginChild(Str(id).c_str(), ImVec2(width, height), child_flags, window_flags);
}
void mono_ImGUI_EndChild() { ImGui::EndChild(); }

void mono_ImGUI_SetNextWindowPos(float x, float y, int flags, float xpiv, float ypiv) {
    ImGui::SetNextWindowPos(ImVec2(x, y), flags, ImVec2(xpiv, ypiv));
}
void mono_ImGUI_SetNextWindowFocus() { ImGui::SetNextWindowFocus(); }
void mono_ImGUI_SetNextWindowBgAlpha(float alpha) { ImGui::SetNextWindowBgAlpha(alpha); }
void mono_ImGUI_SetNextWindowSize(float width, float height) { ImGui::SetNextWindowSize(ImVec2(width, height)); }
void mono_ImGUI_SetNextWindowSizeWithCond(float width, float height, int cond) {
    ImGui::SetNextWindowSize(ImVec2(width, height), cond);
}
void mono_ImGUI_SetNextWindowSizeConstraints(float min_w, float min_h, float max_w, float max_h) {
    ImGui::SetNextWindowSizeConstraints(ImVec2(min_w, min_h), ImVec2(max_w, max_h));
}

float mono_ImGUI_GetWindowPosX() { return ImGui::GetWindowPos().x; }
float mono_ImGUI_GetWindowPosY() { return ImGui::GetWindowPos().y; }
float mono_ImGUI_GetWindowSizeX() { return ImGui::GetWindowSize().x; }
float mono_ImGUI_GetWindowSizeY() { return ImGui::GetWindowSize().y; }
float mono_ImGUI_GetWindowWidth() { return ImGui::GetWindowWidth(); }
float mono_ImGUI_GetWindowHeight() { return ImGui::GetWindowHeight(); }
bool mono_ImGUI_IsWindowHovered() { return ImGui::IsWindowHovered(); }

// GetWindowContentRegionMin/Max are obsolete Dear ImGui calls, compiled out where IMGUI_DISABLE_OBSOLETE_FUNCTIONS is
// set (the client sets it); these compute what they returned.
ImVec2 ContentRegionMin() {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    return ImVec2(w->ContentRegionRect.Min.x - w->Pos.x, w->ContentRegionRect.Min.y - w->Pos.y);
}
ImVec2 ContentRegionMax() {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    return ImVec2(w->ContentRegionRect.Max.x - w->Pos.x, w->ContentRegionRect.Max.y - w->Pos.y);
}
float mono_ImGUI_GetWindowContentRegionMin_X() { return ContentRegionMin().x; }
float mono_ImGUI_GetWindowContentRegionMin_Y() { return ContentRegionMin().y; }
float mono_ImGUI_GetWindowContentRegionMax_X() { return ContentRegionMax().x; }
float mono_ImGUI_GetWindowContentRegionMax_Y() { return ContentRegionMax().y; }

// ---- cursor and layout

float mono_ImGUI_GetCursorPosX() { return ImGui::GetCursorPosX(); }
void mono_ImGUI_SetCursorPosX(float x) { ImGui::SetCursorPosX(x); }
float mono_ImGUI_GetCursorPosY() { return ImGui::GetCursorPosY(); }
void mono_ImGUI_SetCursorPosY(float y) { ImGui::SetCursorPosY(y); }
float mono_ImGUI_GetCursorScreenPosX() { return ImGui::GetCursorScreenPos().x; }
float mono_ImGUI_GetCursorScreenPosY() { return ImGui::GetCursorScreenPos().y; }

void mono_ImGUI_Separator() { ImGui::Separator(); }
void mono_ImGUI_SameLine() { ImGui::SameLine(); }
void mono_ImGUI_SameLineEx(float offset_from_start_x, float spacing) { ImGui::SameLine(offset_from_start_x, spacing); }
float mono_ImGUI_GetContentRegionAvailX() { return ImGui::GetContentRegionAvail().x; }
float mono_ImGUI_GetContentRegionAvailY() { return ImGui::GetContentRegionAvail().y; }
void mono_ImGUI_SetNextItemWidth(float width) { ImGui::SetNextItemWidth(width); }
// These four, SetClipboardText, Checkbox_Clear and Internal_CalcItemSize are bool in E3's C# and returned nothing in
// MQ2Mono, so C# read whatever the register held. They return true here (or what they report).
bool mono_ImGUI_PushItemWidth(float width) {
    ImGui::PushItemWidth(width);
    return true;
}
bool mono_ImGUI_PopItemWidth() {   // no argument in C# (MQ2Mono's C++ took a float it never got)
    ImGui::PopItemWidth();
    return true;
}
bool mono_ImGUI_PushID(int id) {
    ImGui::PushID(id);
    return true;
}
bool mono_ImGUI_PopID() {
    ImGui::PopID();
    return true;
}

float mono_ImGUI_GetTextLineHeight() { return ImGui::GetTextLineHeight(); }
float mono_ImGUI_GetTextLineHeightWithSpacing() { return ImGui::GetTextLineHeightWithSpacing(); }
float mono_ImGUI_GetFrameHeight() { return ImGui::GetFrameHeight(); }

void mono_ImGUI_Internal_ItemSize(float x, float y, float text_baseline_y) { ImGui::ItemSize(ImVec2(x, y), text_baseline_y); }
bool mono_ImGUI_Internal_ItemAdd(float x, float y, float x2, float y2) { return ImGui::ItemAdd(ImRect(x, y, x2, y2), 0); }
bool mono_ImGUI_Internal_CalcItemSize(float x, float y, float default_w, float* r_x, float* r_y) {
    if (!r_x || !r_y) return false;
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImVec2 size = ImGui::CalcItemSize(ImVec2(x, y), default_w, g.FontSize + g.Style.FramePadding.y * 2.0f);
    *r_x = size.x;
    *r_y = size.y;
    return true;
}

// ---- items

float mono_ImGUI_GetItemRectMinX() { return ImGui::GetItemRectMin().x; }
float mono_ImGUI_GetItemRectMinY() { return ImGui::GetItemRectMin().y; }
float mono_ImGUI_GetItemRectMaxX() { return ImGui::GetItemRectMax().x; }
float mono_ImGUI_GetItemRectMaxY() { return ImGui::GetItemRectMax().y; }
MonoArray* mono_ImGUI_GetItemRectMin() {
    const ImVec2 v = ImGui::GetItemRectMin();
    return FloatArray({v.x, v.y});
}
MonoArray* mono_ImGUI_GetItemRectSize() {
    const ImVec2 v = ImGui::GetItemRectSize();
    return FloatArray({v.x, v.y});
}
bool mono_ImGUI_IsItemHovered() { return ImGui::IsItemHovered(); }
bool mono_ImGUI_IsItemClicked(int button) { return ImGui::IsItemClicked(button); }
bool mono_ImGUI_IsMouseClicked(int button) { return ImGui::IsMouseClicked(button); }
bool mono_ImGUI_IsMouseDoubleClicked(int button) { return ImGui::IsMouseDoubleClicked(button); }

// ---- text

void mono_ImGUI_Text(MonoString* text) { ImGui::TextUnformatted(Str(text).c_str()); }
void mono_ImGUI_TextUnformatted(MonoString* text) { ImGui::TextUnformatted(Str(text).c_str()); }
void mono_ImGUI_TextWrapped(MonoString* text) {
    if (text) ImGui::TextWrapped("%s", Str(text).c_str());
}
void mono_ImGUI_TextColored(float r, float g, float b, float a, MonoString* text) {
    if (text) ImGui::TextColored(ImVec4(r, g, b, a), "%s", Str(text).c_str());
}
void mono_ImGUI_PushTextWrapPos(float wrap_local_pos_x) { ImGui::PushTextWrapPos(wrap_local_pos_x); }
void mono_ImGUI_PopTextWrapPos() { ImGui::PopTextWrapPos(); }
float mono_ImGUI_CalcTextSizeX(MonoString* input) { return input ? ImGui::CalcTextSize(Str(input).c_str()).x : 0.0f; }
MonoArray* mono_ImGUI_CalcTextSize(MonoString* input) {
    if (!input) return FloatArray({0.0f, 0.0f});
    const ImVec2 v = ImGui::CalcTextSize(Str(input).c_str());
    return FloatArray({v.x, v.y});
}
bool mono_ImGUI_SetClipboardText(MonoString* text) {
    ImGui::SetClipboardText(Str(text).c_str());
    return true;
}

// ---- buttons, selectables, checkboxes

bool mono_ImGUI_Button(MonoString* name) { return ImGui::Button(Str(name).c_str()); }
bool mono_ImGUI_ButtonEx(MonoString* name, float width, float height) {
    return ImGui::Button(Str(name).c_str(), ImVec2(width, height));
}
bool mono_ImGUI_SmallButton(MonoString* name) { return ImGui::SmallButton(Str(name).c_str()); }
bool mono_ImGUI_InvisibleButton(MonoString* name, float width, float height, int flags) {
    return ImGui::InvisibleButton(Str(name).c_str(), ImVec2(width, height), flags);
}
bool mono_ImGUI_RightAlignButton(MonoString* name) {
    if (!name) return false;
    const std::string label = Str(name);
    const float buttonWidth = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > buttonWidth) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - buttonWidth));
    return ImGui::Button(label.c_str());
}

bool mono_ImGUI_Selectable(MonoString* label, bool selected) { return ImGui::Selectable(Str(label).c_str(), selected); }
bool mono_ImGUI_Selectable_WithFlags(MonoString* label, bool selected, int flags) {
    return ImGui::Selectable(Str(label).c_str(), selected, flags);
}

bool mono_ImGUI_Checkbox(MonoString* name, bool currentValue) {
    monoAppDomainInfo* s = Script();
    if (!s) return false;
    const std::string str = Str(name);
    bool& value = s->m_IMGUI_CheckboxValues[str];
    value = currentValue;
    return ImGui::Checkbox(str.c_str(), &value);
}
bool mono_ImGUI_Checkbox_Clear(MonoString* id) {   // true when there was one
    monoAppDomainInfo* s = Script();
    return s && s->m_IMGUI_CheckboxValues.erase(Str(id)) > 0;
}
bool mono_ImGUI_Checkbox_Get(MonoString* id) {
    monoAppDomainInfo* s = Script();
    if (!s) return false;
    auto it = s->m_IMGUI_CheckboxValues.find(Str(id));
    return it != s->m_IMGUI_CheckboxValues.end() && it->second;
}

// ---- progress bars

void mono_ImGUI_ProgressBar(float fraction, float height, float width, MonoString* overlay) {
    if (overlay) ImGui::ProgressBar(fraction, ImVec2(width, height), Str(overlay).c_str());
    else ImGui::ProgressBar(fraction, ImVec2(width, height));
}

void mono_ImGUI_ProgressBarGradient(float progress, float height, float width, unsigned int color_start,
                                    unsigned int color_end) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImGuiStyle& style = g.Style;
    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 size = ImGui::CalcItemSize(ImVec2(width, height), ImGui::GetContentRegionAvail().x,
                                            g.FontSize + style.FramePadding.y * 2.0f);
    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ImGui::ItemSize(size);
    if (!ImGui::ItemAdd(bb, 0)) return;
    window->DrawList->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
    if (progress > 0.0f) {
        const float progress_x = bb.Min.x + size.x * progress;
        window->DrawList->AddRectFilledMultiColor(bb.Min, ImVec2(progress_x, bb.Max.y), color_start, color_end, color_end,
                                                  color_start);
    }
}

// ---- tabs

bool mono_ImGUI_BeginTabBar(MonoString* name) { return ImGui::BeginTabBar(Str(name).c_str()); }
void mono_ImGUI_EndTabBar() { ImGui::EndTabBar(); }
bool mono_ImGUI_BeginTabItem(MonoString* label) { return ImGui::BeginTabItem(Str(label).c_str()); }
void mono_ImGUI_EndTabItem() { ImGui::EndTabItem(); }

// ---- tables

bool mono_ImGUI_BeginTableSimple(MonoString* id, int columns, int flags) {
    return id && ImGui::BeginTable(Str(id).c_str(), columns, flags);
}
bool mono_ImGUI_BeginTable(MonoString* id, int columns, int flags, float outer_width, float outer_height) {
    return id && ImGui::BeginTable(Str(id).c_str(), columns, flags, ImVec2(outer_width, outer_height));
}
void mono_ImGUI_EndTable() { ImGui::EndTable(); }
void mono_ImGUI_TableSetupColumn(MonoString* label, int flags, float init_width) {
    if (label) ImGui::TableSetupColumn(Str(label).c_str(), flags, init_width);
}
void mono_ImGUI_TableSetupColumn_Default(MonoString* label) {
    if (label) ImGui::TableSetupColumn(Str(label).c_str());
}
void mono_ImGUI_TableHeadersRow() { ImGui::TableHeadersRow(); }
void mono_ImGUI_TableHeader(MonoString* label) {
    if (label) ImGui::TableHeader(Str(label).c_str());
}
MonoString* mono_ImGUI_TableGetColumnName(int column) { return NewString(ImGui::TableGetColumnName(column)); }
void mono_ImGUI_TableNextRow() { ImGui::TableNextRow(); }
void mono_ImGUI_TableNextRowEx(int row_flags, float min_row_height) { ImGui::TableNextRow(row_flags, min_row_height); }
bool mono_ImGUI_TableNextColumn() { return ImGui::TableNextColumn(); }
bool mono_ImGUI_TableSetColumnIndex(int index) { return ImGui::TableSetColumnIndex(index); }   // bool in C# too
void mono_ImGUI_TableSetBgColor(int target, unsigned int color, int column) {
    ImGui::TableSetBgColor(target, static_cast<ImU32>(color), column);
}

// ---- colour pickers

// MQ2Mono read the map's end() after inserting a new label (undefined behaviour on the first frame); the inserted
// entry is used here.
bool ColorPicker(MonoString* label, std::array<float, 4> initial, int flags) {
    monoAppDomainInfo* s = Script();
    if (!s) return false;
    const std::string str = Str(label);
    auto [it, added] = s->m_IMGUI_InputColorValues.try_emplace(str, initial);
    return ImGui::ColorPicker4(str.c_str(), it->second.data(), flags);
}
bool mono_ImGUI_ColorPicker4(MonoString* label, int r, int g, int b, int a, int flags) {
    return ColorPicker(label,
                       {static_cast<float>(r) / 255, static_cast<float>(g) / 255, static_cast<float>(b) / 255,
                        static_cast<float>(a) / 255},
                       flags);
}
bool mono_ImGUI_ColorPicker4_Float(MonoString* label, float r, float g, float b, float a, int flags) {
    return ColorPicker(label, {r, g, b, a}, flags);
}
void mono_ImGUI_ColorPicker_Clear(MonoString* label) {
    if (monoAppDomainInfo* s = Script()) s->m_IMGUI_InputColorValues.erase(Str(label));
}
MonoArray* mono_ImGUI_ColorPicker_GetRGBA(MonoString* label) {
    monoAppDomainInfo* s = Script();
    if (s)
        if (auto it = s->m_IMGUI_InputColorValues.find(Str(label)); it != s->m_IMGUI_InputColorValues.end()) {
            const auto& c = it->second;
            return IntArray({static_cast<int32_t>(c[0] * 255), static_cast<int32_t>(c[1] * 255),
                             static_cast<int32_t>(c[2] * 255), static_cast<int32_t>(c[3] * 255)});
        }
    return IntArray({0, 0, 0, 0});
}
MonoArray* mono_ImGUI_ColorPicker_GetRGBA_Float(MonoString* label) {
    monoAppDomainInfo* s = Script();
    if (s)
        if (auto it = s->m_IMGUI_InputColorValues.find(Str(label)); it != s->m_IMGUI_InputColorValues.end()) {
            const auto& c = it->second;
            return FloatArray({c[0], c[1], c[2], c[3]});
        }
    return FloatArray({0.0f, 0.0f, 0.0f, 0.0f});
}

// ---- style

// Dear ImGui asserts on a colour or style variable out of range, and on a push of the wrong type, so they are checked
// here first: the C# side gives -1 for a name this Dear ImGui lacks (DockingPreview without docking, say). A push that
// can't be honoured still pushes, the current text colour or alpha unchanged, so the script's PopStyleColor /
// PopStyleVar counts stay right.
bool IsColour(int col) { return col >= 0 && col < ImGuiCol_COUNT; }
MonoArray* mono_ImGUI_GetStyleColorVec4(int flag) {
    if (!IsColour(flag)) return FloatArray({0.0f, 0.0f, 0.0f, 0.0f});
    const ImVec4 c = ImGui::GetStyleColorVec4(flag);
    return FloatArray({c.x, c.y, c.z, c.w});
}
uint32_t mono_ImGUI_GetColorU32(int imguiCol, float alpha_mul) {
    return IsColour(imguiCol) ? ImGui::GetColorU32(imguiCol, alpha_mul) : 0;
}
void mono_ImGUI_PushStyleColor(int which, float r, float g, float b, float a) {
    if (IsColour(which)) ImGui::PushStyleColor(which, ImVec4(r, g, b, a));
    else ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
}
void mono_ImGUI_PopStyleColor(int count) { ImGui::PopStyleColor(count); }
void mono_ImGUI_PushStyleVarFloat(int which, float value) {
    if (StyleVarSlot(which, 1)) ImGui::PushStyleVar(which, value);
    else ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha);
}
void mono_ImGUI_PushStyleVarVec2(int which, float x, float y) {
    if (StyleVarSlot(which, 2)) ImGui::PushStyleVar(which, ImVec2(x, y));
    else ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha);
}
void mono_ImGUI_PopStyleVar(int count) { ImGui::PopStyleVar(count); }

// Any float or ImVec2 style variable, where MQ2Mono's switch knew sixteen and nine; 0 for any other.
float mono_ImGUI_GetStyleVarFloat(int which) {
    const float* v = StyleVarSlot(which, 1);
    return v ? v[0] : 0.0f;
}

// E3's C# declares `out int x, out int y`, so whole numbers are written (MQ2Mono wrote floats into those ints).
void mono_ImGUI_GetStyleVarVec2(int which, int32_t* x, int32_t* y) {
    if (!x || !y) return;
    const float* v = StyleVarSlot(which, 2);
    *x = v ? static_cast<int32_t>(v[0]) : 0;
    *y = v ? static_cast<int32_t>(v[1]) : 0;
}

// ---- combos, popups, trees, tooltips

bool mono_ImGUI_BeginCombo(MonoString* label, MonoString* preview, int flags) {
    return ImGui::BeginCombo(Str(label).c_str(), Str(preview).c_str(), flags);
}
void mono_ImGUI_EndCombo() { ImGui::EndCombo(); }

bool mono_ImGUI_BeginPopupContextItem(MonoString* id, int flags) {
    const std::string s = Str(id);
    return ImGui::BeginPopupContextItem(id ? s.c_str() : nullptr, flags);
}
bool mono_ImGUI_BeginPopupContextWindow(MonoString* id, int flags) {
    const std::string s = Str(id);
    return ImGui::BeginPopupContextWindow(id ? s.c_str() : nullptr, flags);
}
void mono_ImGUI_EndPopup() { ImGui::EndPopup(); }
bool mono_ImGUI_MenuItem(MonoString* label) { return label && ImGui::MenuItem(Str(label).c_str()); }

bool mono_ImGUI_TreeNode(MonoString* label) { return label && ImGui::TreeNode(Str(label).c_str()); }
bool mono_ImGUI_TreeNodeEx(MonoString* label, int flags) { return label && ImGui::TreeNodeEx(Str(label).c_str(), flags); }
void mono_ImGUI_TreePop() { ImGui::TreePop(); }
bool mono_ImGUI_CollapsingHeader(MonoString* label, int flags) {
    return label && ImGui::CollapsingHeader(Str(label).c_str(), flags);
}

void mono_ImGUI_BeginTooltip() { ImGui::BeginTooltip(); }
void mono_ImGUI_EndTooltip() { ImGui::EndTooltip(); }

// ---- text and number input

std::string& InputTextValue(monoAppDomainInfo& s, const std::string& id, std::string current) {
    std::string& value = s.m_IMGUI_InputTextValues[id];
    value = std::move(current);   // the C# side's value wins each frame, as in MQ2Mono
    return value;
}
bool mono_ImGUI_InputText(MonoString* id, MonoString* currentValue) {
    monoAppDomainInfo* s = Script();
    if (!s) return false;
    const std::string idStr = Str(id);
    return ImGui::InputText(idStr.c_str(), &InputTextValue(*s, idStr, Str(currentValue)));
}
bool mono_ImGUI_InputTextMultiline(MonoString* id, MonoString* currentValue, float width, float height) {
    monoAppDomainInfo* s = Script();
    if (!s) return false;
    const std::string idStr = Str(id);
    const ImVec2 size(width <= 0.0f ? 0.0f : width, height <= 0.0f ? 0.0f : height);
    return ImGui::InputTextMultiline(idStr.c_str(), &InputTextValue(*s, idStr, Str(currentValue)), size);
}
MonoString* mono_ImGUI_InputText_Get(MonoString* id) {
    monoAppDomainInfo* s = Script();
    if (!s) return NewString("");
    auto it = s->m_IMGUI_InputTextValues.find(Str(id));
    return NewString(it == s->m_IMGUI_InputTextValues.end() ? "" : it->second.c_str());
}
bool mono_ImGUI_InputText_Clear(MonoString* id) {
    monoAppDomainInfo* s = Script();
    return s && s->m_IMGUI_InputTextValues.erase(Str(id)) > 0;
}

bool mono_ImGUI_InputInt(MonoString* id, int current, int steps, int fastSteps) {
    monoAppDomainInfo* s = Script();
    if (!s) return false;
    const std::string idStr = Str(id);
    int& value = s->m_IMGUI_InputIntValues[idStr];
    value = current;
    LiveEdit live;
    return ImGui::InputInt(idStr.c_str(), &value, steps, fastSteps);
}
int mono_ImGUI_InputInt_Get(MonoString* id) {
    monoAppDomainInfo* s = Script();
    if (!s) return 0;
    auto it = s->m_IMGUI_InputIntValues.find(Str(id));
    return it == s->m_IMGUI_InputIntValues.end() ? -1 : it->second;
}
bool mono_ImGUI_InputInt_Clear(MonoString* id) {
    monoAppDomainInfo* s = Script();
    return s && s->m_IMGUI_InputIntValues.erase(Str(id)) > 0;
}

bool mono_ImGUI_SliderInt(MonoString* id, int* value, int minValue, int maxValue) {
    if (!id || !value) return false;
    int v = *value;
    LiveEdit live;   // Ctrl+click types a number into a slider
    const bool changed = ImGui::SliderInt(Str(id).c_str(), &v, minValue, maxValue);
    if (changed) *value = v;
    return changed;
}
bool mono_ImGUI_SliderDouble(MonoString* id, double* value, double minValue, double maxValue, MonoString* fmt) {
    if (!id || !value) return false;
    const std::string format = fmt ? Str(fmt) : std::string("%.3f");
    double v = *value;
    LiveEdit live;
    const bool changed =
        ImGui::SliderScalar(Str(id).c_str(), ImGuiDataType_Double, &v, &minValue, &maxValue, format.c_str());
    if (changed) *value = v;
    return changed;
}

// ---- images and textures

// No texture reaches a script yet (mq_CreateTextureFromData gives none, as in MQ2Mono), and a null texture would make
// the renderer bind nothing: draw nothing, but take the image's space so the layout holds.
void mono_ImGUI_Image(void* textureId, float width, float height) {
    if (!textureId) {
        ImGui::Dummy(ImVec2(width, height));
        return;
    }
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(textureId))), ImVec2(width, height));
}
// The C# side passes a byte[] (a MonoArray, not the bytes). A placeholder in MQ2Mono too.
void* mono_CreateTextureFromData(MonoArray*, int, int, int) { return nullptr; }
void mono_DestroyTexture(void*) {}

// The spell's icon, size x size, as the client's own windows draw it (the host's ui_icon). MQ2Mono draws the cell
// from A_SpellGems; here the number is a cell of A_SpellIcons, which is what Hailstone's ${Spell.SpellIcon} and the
// spell records give (MQ's SpellIcon). Without the icon (a client without ui_icon, a cell or sheet the game folder
// lacks) the icon's space is taken and nothing drawn, so the layout holds; MQ2Mono draws nothing there.
void DrawSpellIcon(int32_t icon, float size) {
    HailstoneUiImage image{};
    image.size = sizeof image;
    if (HAILSTONE_HOST_HAS(host, ui_icon) && host->ui_icon(host->host, HAILSTONE_UI_ICON_SPELL, icon, &image)) {
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(image.texture)), ImVec2(size, size),
                     ImVec2(image.u0, image.v0), ImVec2(image.u1, image.v1));
        return;
    }
    ImGui::Dummy(ImVec2(size, size));
}
void mono_ImGUI_DrawSpellIconByIconIndex(int iconIndex, float size) {
    if (iconIndex >= 0) DrawSpellIcon(iconIndex, size);
}
// The spell's spell_icon (get_spell). A client older than HailstoneSpell's spell_icon leaves the -1: the space only.
void mono_ImGUI_DrawSpellIconBySpellID(int spellId, float size) {
    if (spellId <= 0) return;
    HailstoneSpell spell{};
    spell.size = sizeof spell;
    spell.spell_icon = -1;
    if (host->get_spell(host->host, static_cast<uint32_t>(spellId), &spell) && spell.spell_icon >= 0) {
        DrawSpellIcon(spell.spell_icon, size);
        return;
    }
    ImGui::Dummy(ImVec2(size, size));
}

// ---- fonts

// The pre-1.92 PushFont(font): the font at the size it was added with. MQ2Mono's calls of it mean that; the client
// builds without the obsolete one-argument overload (IMGUI_DISABLE_OBSOLETE_FUNCTIONS).
void PushFontAtItsSize(ImFont* font) { ImGui::PushFont(font, font->LegacySize); }

// Into the client's atlas, which the renderer updates as fonts come and go (1.92's dynamic fonts). MQ2Mono zeroed the
// whole ImFontConfig first; in 1.92 that also zeroes fields whose defaults matter (RasterizerMultiply 0 draws nothing,
// GlyphMaxAdvanceX 0 collapses the advance), so the config keeps its defaults and only MQ2Mono's settings change.
void* mono_ImGUI_AddFontFromFileTTF(MonoString* path, float size_pixels, const uint16_t* ranges, int range_count,
                                    bool merge_mode) {
    const std::string file = Str(path);
    if (file.empty()) return nullptr;
    ImFontConfig cfg;
    cfg.MergeMode = merge_mode;
    cfg.PixelSnapH = true;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    // The atlas keeps the ranges' pointer and reads it whenever it loads more glyphs, for as long as the font is in the
    // client's atlas: after this plugin unloads too (a merged font can't be taken out on its own). So the ranges are
    // never freed; not even a static's destructor may free them at unload. A few bytes a font.
    static auto* keptRanges = new std::deque<std::vector<ImWchar>>;
    const ImWchar* wranges = nullptr;
    if (ranges && range_count > 0) {
        wranges = keptRanges->emplace_back(ranges, ranges + range_count).data();
    }
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(file.c_str(), size_pixels, &cfg, wranges);
}

float mono_ImGUI_Style_GetFontSizeBase() { return ImGui::GetStyle().FontSizeBase; }

// By the font's name in the client's atlas. An empty name with a size keeps the current font at that size. The client
// ships MQ's "RobotoRegular" and "RobotoRegular (Large)", with FontAwesome 4.7 merged in (E3's "robo").
bool mono_ImGUI_PushFont(MonoString* name, float font_size) {
    const std::string wanted = Str(name);
    if (wanted.empty() && font_size > 0) {
        ImGui::PushFont(nullptr, font_size);
        return true;
    }
    for (ImFont* font : ImGui::GetIO().Fonts->Fonts)
        if (font && wanted == font->GetDebugName()) {
            PushFontAtItsSize(font);
            return true;
        }
    return false;
}

// MQ's GetEQImFont: EverQuest's font table (0 .. 10), which the client hands out (ui_font); at its own size when
// `size` is 0. False, and nothing pushed, for a font the client lacks (as MQ2Mono for a missing one), and on a client
// without ui_font. E3's "EQ-Thin" is 4 and "EQ-Bold" 10.
bool mono_ImGUI_PushEQFont(int fontId, float size) {
    if (!HAILSTONE_HOST_HAS(host, ui_font)) return false;
    ImFont* font = static_cast<ImFont*>(host->ui_font(host->host, HAILSTONE_UI_FONT_EQ, fontId));
    if (!font) return false;
    ImGui::PushFont(font, size > 0 ? size : font->LegacySize);
    return true;
}

void mono_ImGUI_PopFont() { ImGui::PopFont(); }

// The first font in the atlas that has a Material Design glyph; nothing when none has (as in MQ2Mono). Looked for
// again until found, since fonts can be added while running.
void mono_ImGUI_PushMaterialIconsFont() {
    static ImFont* mdFont = nullptr;
    if (!mdFont) {
        constexpr ImWchar kProbe = static_cast<ImWchar>(0xE616);   // ICON_MD_EVENT_NOTE
        for (ImFont* f : ImGui::GetIO().Fonts->Fonts)
            if (f && f->GetFontBaked(ImGui::GetFontSize())->FindGlyphNoFallback(kProbe)) {
                mdFont = f;
                break;
            }
    }
    if (mdFont) PushFontAtItsSize(mdFont);
}

// ---- the window's draw list

ImDrawList* DrawList() { return ImGui::GetWindowDrawList(); }

// 1.93 draws nothing for a rectangle whose min is past its max; Dear ImGui drew it before, and scripts rely on that.
void Ordered(float& lo, float& hi) {
    if (lo > hi) std::swap(lo, hi);
}

// 1.93 moved outlines by up to a pixel and changed their joins; ImDrawFlags_StrokeLegacy keeps MQ2Mono's. Every outline
// a script draws gets it, unless the script chose a stroke position itself (AddRect's flags).
int Stroke(int flags = 0) { return (flags & ImDrawFlags_StrokeMask_) ? flags : flags | ImDrawFlags_StrokeLegacy; }

// float[] in E3's C#; MQ2Mono returned nothing, which C# would take as an array. Null, which C# can hold safely.
MonoArray* mono_ImGUI_GetWindowDrawList_AddLine(float x, float y, float x_end, float y_end, unsigned int color,
                                                float thickness) {
    if (ImDrawList* d = DrawList()) d->AddLine(ImVec2(x, y), ImVec2(x_end, y_end), color, thickness, Stroke());
    return nullptr;
}
void mono_ImGUI_GetWindowDrawList_AddRect(float p_min_x, float p_min_y, float p_max_x, float p_max_y, unsigned int color,
                                          float rounding, int rounding_corners_flags, float thickness) {
    ImDrawList* d = DrawList();
    if (!d) return;
    Ordered(p_min_x, p_max_x);
    Ordered(p_min_y, p_max_y);
    // Thickness before flags since 1.92.8; the C# side keeps MQ2Mono's order of arguments.
    d->AddRect(ImVec2(p_min_x, p_min_y), ImVec2(p_max_x, p_max_y), color, rounding, thickness,
               Stroke(rounding_corners_flags));
}
void mono_ImGUI_GetWindowDrawList_AddRectFilled(float x1, float y1, float x2, float y2, uint32_t color, float rounding,
                                                int rounding_corners_flags) {
    ImDrawList* d = DrawList();
    if (!d) return;
    Ordered(x1, x2);
    Ordered(y1, y2);
    d->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y2), color, rounding, rounding_corners_flags);
}
void mono_ImGUI_GetWindowDrawList_AddRectFilledMultiColor(float p_min_x, float p_min_y, float p_max_x, float p_max_y,
                                                          unsigned int col_upr_left, unsigned int col_upr_right,
                                                          unsigned int col_bot_right, unsigned int col_bot_left) {
    if (ImDrawList* d = DrawList())
        d->AddRectFilledMultiColor(ImVec2(p_min_x, p_min_y), ImVec2(p_max_x, p_max_y), col_upr_left, col_upr_right,
                                   col_bot_right, col_bot_left);
}
void mono_ImGUI_GetWindowDrawList_AddQuad(float p1_x, float p1_y, float p2_x, float p2_y, float p3_x, float p3_y,
                                          float p4_x, float p4_y, unsigned int color, float thickness) {
    if (ImDrawList* d = DrawList())
        d->AddQuad(ImVec2(p1_x, p1_y), ImVec2(p2_x, p2_y), ImVec2(p3_x, p3_y), ImVec2(p4_x, p4_y), color, thickness, Stroke());
}
void mono_ImGUI_GetWindowDrawList_AddQuadFilled(float p1_x, float p1_y, float p2_x, float p2_y, float p3_x, float p3_y,
                                                float p4_x, float p4_y, unsigned int color) {
    if (ImDrawList* d = DrawList())
        d->AddQuadFilled(ImVec2(p1_x, p1_y), ImVec2(p2_x, p2_y), ImVec2(p3_x, p3_y), ImVec2(p4_x, p4_y), color);
}
void mono_ImGUI_GetWindowDrawList_AddTriangle(float p1_x, float p1_y, float p2_x, float p2_y, float p3_x, float p3_y,
                                              unsigned int color, float thickness) {
    if (ImDrawList* d = DrawList())
        d->AddTriangle(ImVec2(p1_x, p1_y), ImVec2(p2_x, p2_y), ImVec2(p3_x, p3_y), color, thickness, Stroke());
}
void mono_ImGUI_GetWindowDrawList_AddTriangleFilled(float p1_x, float p1_y, float p2_x, float p2_y, float p3_x, float p3_y,
                                                    unsigned int color) {
    if (ImDrawList* d = DrawList()) d->AddTriangleFilled(ImVec2(p1_x, p1_y), ImVec2(p2_x, p2_y), ImVec2(p3_x, p3_y), color);
}
// The *Filled shapes below fill. MQ2Mono drew their outlines (it called AddCircle, AddNgon and AddEllipse): a bug fixed
// here.
void mono_ImGUI_GetWindowDrawList_AddCircle(float center_x, float center_y, float radius, unsigned int color,
                                            int num_segments, float thickness) {
    if (ImDrawList* d = DrawList())
        d->AddCircle(ImVec2(center_x, center_y), radius, color, num_segments, thickness, Stroke());
}
void mono_ImGUI_GetWindowDrawList_AddCircleFilled(float center_x, float center_y, float radius, unsigned int color,
                                                  int num_segments) {
    if (ImDrawList* d = DrawList()) d->AddCircleFilled(ImVec2(center_x, center_y), radius, color, num_segments);
}
void mono_ImGUI_GetWindowDrawList_AddNgon(float center_x, float center_y, float radius, unsigned int color,
                                          int num_segments, float thickness) {
    if (ImDrawList* d = DrawList())
        d->AddNgon(ImVec2(center_x, center_y), radius, color, num_segments, thickness, Stroke());
}
void mono_ImGUI_GetWindowDrawList_AddNgonFilled(float center_x, float center_y, float radius, unsigned int col,
                                                int num_segments) {
    if (ImDrawList* d = DrawList()) d->AddNgonFilled(ImVec2(center_x, center_y), radius, col, num_segments);
}
void mono_ImGUI_GetWindowDrawList_AddEllipse(float center_x, float center_y, float radius_x, float radius_y,
                                             unsigned int col, float rot, int num_segments, float thickness) {
    if (ImDrawList* d = DrawList())
        d->AddEllipse(ImVec2(center_x, center_y), ImVec2(radius_x, radius_y), col, rot, num_segments, thickness,
                      Stroke());
}
void mono_ImGUI_GetWindowDrawList_AddEllipseFilled(float center_x, float center_y, float radius_x, float radius_y,
                                                   unsigned int col, float rot, int num_segments) {
    if (ImDrawList* d = DrawList())
        d->AddEllipseFilled(ImVec2(center_x, center_y), ImVec2(radius_x, radius_y), col, rot, num_segments);
}
void mono_ImGUI_GetWindowDrawList_AddBezierCubic(float p1_x, float p1_y, float p2_x, float p2_y, float p3_x, float p3_y,
                                                 float p4_x, float p4_y, unsigned int col, float thickness,
                                                 int num_segments) {
    if (ImDrawList* d = DrawList())
        d->AddBezierCubic(ImVec2(p1_x, p1_y), ImVec2(p2_x, p2_y), ImVec2(p3_x, p3_y), ImVec2(p4_x, p4_y), col, thickness,
                          num_segments, Stroke());
}
void mono_ImGUI_GetWindowDrawList_AddBezierQuadratic(float p1_x, float p1_y, float p2_x, float p2_y, float p3_x,
                                                     float p3_y, unsigned int col, float thickness, int num_segments) {
    if (ImDrawList* d = DrawList())
        d->AddBezierQuadratic(ImVec2(p1_x, p1_y), ImVec2(p2_x, p2_y), ImVec2(p3_x, p3_y), col, thickness, num_segments,
                              Stroke());
}
void mono_ImGUI_GetWindowDrawList_AddText(float x, float y, uint32_t color, MonoString* text) {
    ImDrawList* d = DrawList();
    if (d && text) d->AddText(ImVec2(x, y), color, Str(text).c_str());
}

// ---- the Dear ImGui a script draws with

int mono_ImGUI_GetVersionNum() { return IMGUI_VERSION_NUM; }
bool mono_ImGUI_HasDocking() {
#ifdef IMGUI_HAS_DOCK
    return true;
#else
    return false;
#endif
}

} // namespace

// ---- each frame

static bool BeginScriptsImGui() {
    // The client's context, into this plugin's copy of Dear ImGui. Null without the 'ui' capability.
    void* context = host && host->imgui_context ? host->imgui_context(host->host) : nullptr;
    if (!context) return false;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
    return ImGui::GetCurrentContext()->WithinFrameScope;
}

std::string FocusedWindowName() {
    void* context = host && host->imgui_context ? host->imgui_context(host->host) : nullptr;
    if (!context) return {};
    const ImGuiWindow* window = static_cast<ImGuiContext*>(context)->NavWindow;
    if (!window || !window->Name) return {};
    // A title's "##id" (or "###id") part is Dear ImGui's, not the window's name; a window named by its id alone
    // ("##castbar") keeps it.
    std::string_view name(window->Name);
    const size_t id = name.find("##");
    if (id != 0 && id != std::string_view::npos) name = name.substr(0, id);
    return std::string(name);
}

void RunScriptsImGui() {
    if (!g_rootDomain || monoAppDomains.empty() || !BeginScriptsImGui()) return;
    for (auto& [name, script] : monoAppDomains) {
        if (!script.m_OnUpdateImGui) continue;
        // Whatever the script leaves open (an exception between Begin and End, a missing Pop) is closed, so the next
        // script's windows and the client's own don't nest in it. The client does the same for Lua scripts.
        ImGuiErrorRecoveryState state;
        ImGui::ErrorRecoveryStoreState(&state);
        Invoke(script, script.m_OnUpdateImGui, "OnUpdateImGui");
        ImGui::ErrorRecoveryTryToRecoverState(&state);
    }
}

// From RegisterInternalCalls (hsmono.cpp): MQ2Mono's E3ImGUI names. (Two are registered twice, as in MQ2Mono.)
void RegisterImGuiInternalCalls() {
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ColorPicker4", mono_ImGUI_ColorPicker4);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ColorPicker_GetRGBA", mono_ImGUI_ColorPicker_GetRGBA);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ColorPicker4_Float", mono_ImGUI_ColorPicker4_Float);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ColorPicker_GetRGBA_Float", mono_ImGUI_ColorPicker_GetRGBA_Float);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetStyleColorVec4", mono_ImGUI_GetStyleColorVec4);

    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ColorPicker_Clear", mono_ImGUI_ColorPicker_Clear);

    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Begin", mono_ImGUI_Begin);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Button", mono_ImGUI_Button);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ButtonEx", mono_ImGUI_ButtonEx);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InvisibleButton", mono_ImGUI_InvisibleButton);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SmallButton", mono_ImGUI_SmallButton);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_End", mono_ImGUI_End);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Begin_OpenFlagSet", mono_ImGUI_Begin_OpenFlagSet);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Begin_OpenFlagGet", mono_ImGUI_Begin_OpenFlagGet);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Text", mono_ImGUI_Text);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Separator", mono_ImGUI_Separator);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SameLine", mono_ImGUI_SameLine);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SameLineEx", mono_ImGUI_SameLineEx);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Checkbox", mono_ImGUI_Checkbox);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Checkbox_Clear", mono_ImGUI_Checkbox_Clear);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Checkbox_Get", mono_ImGUI_Checkbox_Get);

    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginTabBar", mono_ImGUI_BeginTabBar);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndTabBar", mono_ImGUI_EndTabBar);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginTabItem", mono_ImGUI_BeginTabItem);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndTabItem", mono_ImGUI_EndTabItem);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushID", mono_ImGUI_PushID);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PopID", mono_ImGUI_PopID);

    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushItemWidth", mono_ImGUI_PushItemWidth);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PopItemWidth", mono_ImGUI_PopItemWidth);

    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginChild", mono_ImGUI_BeginChild);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndChild", mono_ImGUI_EndChild);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Selectable", mono_ImGUI_Selectable);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Selectable_WithFlags", mono_ImGUI_Selectable_WithFlags);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetContentRegionAvailX", mono_ImGUI_GetContentRegionAvailX);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetContentRegionAvailY", mono_ImGUI_GetContentRegionAvailY);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputText", mono_ImGUI_InputText);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputText_Clear", mono_ImGUI_InputText_Clear);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_CalcTextSizeX", mono_ImGUI_CalcTextSizeX);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_CalcTextSize", mono_ImGUI_CalcTextSize);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowContentRegionMinX", mono_ImGUI_GetWindowContentRegionMin_X);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowContentRegionMinY", mono_ImGUI_GetWindowContentRegionMin_Y);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowContentRegionMaxX", mono_ImGUI_GetWindowContentRegionMax_X);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowContentRegionMaxY", mono_ImGUI_GetWindowContentRegionMax_Y);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextWindowPos", mono_ImGUI_SetNextWindowPos);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetItemRectSize", mono_ImGUI_GetItemRectSize);

    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowPosX", mono_ImGUI_GetWindowPosX);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowPosY", mono_ImGUI_GetWindowPosY);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowSizeX", mono_ImGUI_GetWindowSizeX);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowSizeY", mono_ImGUI_GetWindowSizeY);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextWindowFocus", mono_ImGUI_SetNextWindowFocus);
    //mono_ImGUI_InputTextClear
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputTextMultiline", mono_ImGUI_InputTextMultiline);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputText_Get", mono_ImGUI_InputText_Get);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextItemWidth", mono_ImGUI_SetNextItemWidth);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowHeight", mono_ImGUI_GetWindowHeight);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowWidth", mono_ImGUI_GetWindowWidth);

    // Combo wrappers
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginCombo", mono_ImGUI_BeginCombo);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndCombo", mono_ImGUI_EndCombo);
    // Right align helper
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_RightAlignButton", mono_ImGUI_RightAlignButton);

    // Context menus / popups
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginPopupContextItem", mono_ImGUI_BeginPopupContextItem);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginPopupContextWindow", mono_ImGUI_BeginPopupContextWindow);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndPopup", mono_ImGUI_EndPopup);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_MenuItem", mono_ImGUI_MenuItem);

    //Progress bar
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ProgressBar", mono_ImGUI_ProgressBar);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_ProgressBarGradient", mono_ImGUI_ProgressBarGradient);

    // Tables
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginTable", mono_ImGUI_BeginTable);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginTableS", mono_ImGUI_BeginTableSimple);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndTable", mono_ImGUI_EndTable);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableSetupColumn", mono_ImGUI_TableSetupColumn);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableSetupColumn_Default", mono_ImGUI_TableSetupColumn_Default);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableSetBgColor", mono_ImGUI_TableSetBgColor);

    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableSetColumnIndex", mono_ImGUI_TableSetColumnIndex);

    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableHeadersRow", mono_ImGUI_TableHeadersRow);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableHeader", mono_ImGUI_TableHeader);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableGetColumnName", mono_ImGUI_TableGetColumnName);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableNextRow", mono_ImGUI_TableNextRow);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableNextColumn", mono_ImGUI_TableNextColumn);

    // Colors / styled text
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TextColored", mono_ImGUI_TextColored);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushStyleColor", mono_ImGUI_PushStyleColor);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PopStyleColor", mono_ImGUI_PopStyleColor);

    // Style variables
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushStyleVarFloat", mono_ImGUI_PushStyleVarFloat);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushStyleVarVec2", mono_ImGUI_PushStyleVarVec2);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PopStyleVar", mono_ImGUI_PopStyleVar);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetStyleVarFloat", mono_ImGUI_GetStyleVarFloat);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetStyleVarVec2", mono_ImGUI_GetStyleVarVec2);

    // Text wrapping and window sizing
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TextWrapped", mono_ImGUI_TextWrapped);
    // Expose unformatted text render to avoid printf-style format crashes
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TextUnformatted", mono_ImGUI_TextUnformatted);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushTextWrapPos", mono_ImGUI_PushTextWrapPos);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PopTextWrapPos", mono_ImGUI_PopTextWrapPos);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextWindowSizeConstraints", mono_ImGUI_SetNextWindowSizeConstraints);

    // New window control functions
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextWindowBgAlpha", mono_ImGUI_SetNextWindowBgAlpha);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextWindowSize", mono_ImGUI_SetNextWindowSize);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetNextWindowSizeWithCond", mono_ImGUI_SetNextWindowSizeWithCond);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_IsWindowHovered", mono_ImGUI_IsWindowHovered);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowWidth", mono_ImGUI_GetWindowWidth);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowHeight", mono_ImGUI_GetWindowHeight);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_IsMouseClicked", mono_ImGUI_IsMouseClicked);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_IsMouseDoubleClicked", mono_ImGUI_IsMouseDoubleClicked);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_IsItemClicked", mono_ImGUI_IsItemClicked);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetClipboardText", mono_ImGUI_SetClipboardText);

    // Input int
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputInt", mono_ImGUI_InputInt);
    //mono_ImGUI_InputTextClear
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputInt_Get", mono_ImGUI_InputInt_Get);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_InputInt_Clear", mono_ImGUI_InputInt_Clear);
    //bool mono_ImGUI_InputIntClear(MonoString* id)

    //mono_ImGUI_InputInt

    // Sliders
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SliderInt", mono_ImGUI_SliderInt);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SliderDouble", mono_ImGUI_SliderDouble);

    // Tree nodes
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TreeNode", mono_ImGUI_TreeNode);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TreeNodeEx", mono_ImGUI_TreeNodeEx);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TreePop", mono_ImGUI_TreePop);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_CollapsingHeader", mono_ImGUI_CollapsingHeader);

    // Tooltips and hover detection
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_IsItemHovered", mono_ImGUI_IsItemHovered);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_BeginTooltip", mono_ImGUI_BeginTooltip);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_EndTooltip", mono_ImGUI_EndTooltip);

    // Image display
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Image", mono_ImGUI_Image);

    // Fonts
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_AddFontFromFileTTF", mono_ImGUI_AddFontFromFileTTF);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushFont", mono_ImGUI_PushFont);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PopFont", mono_ImGUI_PopFont);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushEQFont", mono_ImGUI_PushEQFont);

    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Style_GetFontSizeBase", mono_ImGUI_Style_GetFontSizeBase);

    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_PushMaterialIconsFont", mono_ImGUI_PushMaterialIconsFont);

    // Spell icon drawing
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_DrawSpellIconByIconIndex", mono_ImGUI_DrawSpellIconByIconIndex);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_DrawSpellIconBySpellID", mono_ImGUI_DrawSpellIconBySpellID);

    // Drawing functions for custom backgrounds
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_TableNextRowEx", mono_ImGUI_TableNextRowEx);

    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetCursorPosX", mono_ImGUI_GetCursorPosX);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetCursorPosX", mono_ImGUI_SetCursorPosX);

    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetCursorPosY", mono_ImGUI_GetCursorPosY);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_SetCursorPosY", mono_ImGUI_SetCursorPosY);

    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetCursorScreenPosX", mono_ImGUI_GetCursorScreenPosX);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetCursorScreenPosY", mono_ImGUI_GetCursorScreenPosY);

    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetTextLineHeightWithSpacing", mono_ImGUI_GetTextLineHeightWithSpacing);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetTextLineHeight", mono_ImGUI_GetTextLineHeight);

    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetFrameHeight", mono_ImGUI_GetFrameHeight);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddRectFilled", mono_ImGUI_GetWindowDrawList_AddRectFilled);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddRect", mono_ImGUI_GetWindowDrawList_AddRect);

    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddRectFilledMultiColor", mono_ImGUI_GetWindowDrawList_AddRectFilledMultiColor);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddQuad", mono_ImGUI_GetWindowDrawList_AddQuad);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddQuadFilled", mono_ImGUI_GetWindowDrawList_AddQuadFilled);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddTriangle", mono_ImGUI_GetWindowDrawList_AddTriangle);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddTriangleFilled", mono_ImGUI_GetWindowDrawList_AddTriangleFilled);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddCircle", mono_ImGUI_GetWindowDrawList_AddCircle);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddCircleFilled", mono_ImGUI_GetWindowDrawList_AddCircleFilled);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddNgon", mono_ImGUI_GetWindowDrawList_AddNgon);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddNgonFilled", mono_ImGUI_GetWindowDrawList_AddNgonFilled);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddEllipse", mono_ImGUI_GetWindowDrawList_AddEllipse);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddEllipseFilled", mono_ImGUI_GetWindowDrawList_AddEllipseFilled);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddBezierCubic", mono_ImGUI_GetWindowDrawList_AddBezierCubic);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddBezierQuadratic", mono_ImGUI_GetWindowDrawList_AddBezierQuadratic);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Internal_ItemSize", mono_ImGUI_Internal_ItemSize);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Internal_ItemAdd", mono_ImGUI_Internal_ItemAdd);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_Internal_CalcItemSize", mono_ImGUI_Internal_CalcItemSize);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddLine", mono_ImGUI_GetWindowDrawList_AddLine);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetWindowDrawList_AddText", mono_ImGUI_GetWindowDrawList_AddText);

    // Item rect + color helpers
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetItemRectMinX", mono_ImGUI_GetItemRectMinX);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetItemRectMinY", mono_ImGUI_GetItemRectMinY);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetItemRectMaxX", mono_ImGUI_GetItemRectMaxX);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetItemRectMaxY", mono_ImGUI_GetItemRectMaxY);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetItemRectMin", mono_ImGUI_GetItemRectMin);

    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetColorU32", mono_ImGUI_GetColorU32);

    // Texture creation from raw data
    HSMONO_ICALL("MonoCore.E3ImGUI::mq_CreateTextureFromData", mono_CreateTextureFromData);
    HSMONO_ICALL("MonoCore.E3ImGUI::mq_DestroyTexture", mono_DestroyTexture);

    // HSMono's own: which Dear ImGui the script's enums must match (E3ImGUI.ImGuiVersion.Init). MQ2Mono has neither.
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_GetVersionNum", mono_ImGUI_GetVersionNum);
    HSMONO_ICALL("MonoCore.E3ImGUI::imgui_HasDocking", mono_ImGUI_HasDocking);
}

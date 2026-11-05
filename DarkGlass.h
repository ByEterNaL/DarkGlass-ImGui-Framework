// File: DarkGlass_UI/DarkGlass.h
#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include "IconsFontAwesome6.h"
#include "imgui_notify.h"
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <d3d11.h>

namespace DarkGlass {
    struct GifFrame {
        ID3D11ShaderResourceView* Texture;
        int DelayMs;
    };

    static inline ID3D11ShaderResourceView* g_BackgroundImage = nullptr;
    static inline std::vector<GifFrame> g_BackgroundGifFrames;
    static inline int g_CurrentGifFrame = 0;
    static inline float g_GifTimer = 0.0f;
    static inline bool g_IsGif = false;
    static inline bool g_UseBackgroundImage = false;
    
    static inline bool g_ShowMediaWidget = true;
    static inline bool g_MediaDocked = true;
    
    static inline ImVec2 g_MainUIPos;
    static inline ImVec2 g_MainUISize;
    
    void LoadBackgroundImage(const char* filepath);
    void OpenImageFileDialog();

    enum class Language {
        English = 0,
        Turkish = 1
    };
    static inline Language g_Lang = Language::English;
    
    // Transition Animation State
    static inline bool g_IsTransitioning = true; // Start with animation
    static inline bool g_IsFadingOut = false;
    static inline float g_TransitionAlpha = 1.0f; // Start black
    static inline Language g_TargetLang = Language::English;

    static inline const char* Loc(const char* en, const char* tr) {
        return (g_Lang == Language::Turkish) ? tr : en;
    }

    enum class AppTheme {
        Purple = 0,
        White,
        Gray,
        Red,
        Blue
    };
    static inline AppTheme g_Theme = AppTheme::Purple;
    static inline AppTheme g_TargetTheme = AppTheme::Purple;

    enum class BackgroundEffect {
        None = 0,
        Snow,
        Neural,
        Starfield
    };
    static inline BackgroundEffect g_BgEffect = BackgroundEffect::Snow;

    namespace Theme {
        struct Colors {
            // DarkGlass Studio & Performance Overlay Palette (Dynamic)
            static inline ImVec4 BackgroundTop = ImColor(15, 15, 20, 255);    
            static inline ImVec4 BackgroundBottom = ImColor(5, 5, 5, 255);    
            static inline ImVec4 TopBar = ImColor(12, 12, 14, 210);           
            static inline ImVec4 Panel = ImColor(10, 10, 12, 160);            
            static inline ImVec4 Accent = ImColor(139, 92, 246, 255);         
            static inline ImVec4 AccentHover = ImColor(167, 139, 250, 255);
            static inline ImVec4 Text = ImColor(240, 240, 240, 255);
            static inline ImVec4 TextMuted = ImColor(110, 110, 120, 255);
            static inline ImVec4 Border = ImColor(35, 35, 45, 255);           
        };
        
        static inline void ApplyTheme(AppTheme theme) {
            switch (theme) {
                case AppTheme::Purple:
                    Colors::Accent = ImColor(139, 92, 246, 255);
                    Colors::AccentHover = ImColor(167, 139, 250, 255);
                    break;
                case AppTheme::White:
                    Colors::Accent = ImColor(240, 240, 240, 255);
                    Colors::AccentHover = ImColor(255, 255, 255, 255);
                    break;
                case AppTheme::Gray:
                    Colors::Accent = ImColor(160, 160, 170, 255);
                    Colors::AccentHover = ImColor(190, 190, 200, 255);
                    break;
                case AppTheme::Red:
                    Colors::Accent = ImColor(220, 38, 38, 255);
                    Colors::AccentHover = ImColor(248, 113, 113, 255);
                    break;
                case AppTheme::Blue:
                    Colors::Accent = ImColor(59, 130, 246, 255);
                    Colors::AccentHover = ImColor(96, 165, 250, 255);
                    break;
            }

            // Dynamically update scrollbar colors to match the theme
            ImGuiStyle& style = ImGui::GetStyle();
            style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(Colors::Accent.x, Colors::Accent.y, Colors::Accent.z, 0.4f);
            style.Colors[ImGuiCol_ScrollbarGrabHovered] = Colors::Accent;
            style.Colors[ImGuiCol_ScrollbarGrabActive] = Colors::AccentHover;
        }

        struct Sizes {
            static inline float WindowRounding = 8.0f; // Sharper, industrial look
            static inline float PanelRounding = 4.0f;  // Sharp corners for panels
            static inline float TopBarHeight = 65.0f;  // New Top Bar
            static inline ImVec2 WindowSize = ImVec2(900, 650); // Wider landscape layout
            static inline ImVec2 Padding = ImVec2(24, 24);
            static inline ImVec2 ItemSpacing = ImVec2(20, 16);
        };
    }

    class UI {
    private:
        bool m_IsOpen = true;
        int m_SelectedTab = 0;
        int m_PreviousTab = -1;
        float m_ContentAlpha = 1.0f;
        
        struct Tab {
            const char* Icon;
            const char* Name;
        };
        std::vector<Tab> m_Tabs = { 
            { ICON_FA_CHART_PIE, "Dashboard" }, 
            { ICON_FA_MICROCHIP, "Engine" }, 
            { ICON_FA_EYE, "Viewport" }, 
            { ICON_FA_SLIDERS, "Audio & DSP" }, 
            { ICON_FA_GEAR, "Settings" } 
        };
        
        // Animation State Pool for custom widgets
        std::map<ImGuiID, float> m_AnimationState;

        // Helper to get or create animation state
        float& GetAnimState(ImGuiID id);

        // UI Components
        void DrawTopBar(ImVec2 size);
        void DrawContent();
        
        // Custom Widgets
        bool TabButton(const char* icon, const char* label, int index, bool selected);
        void DrawPanel(const char* title, ImVec2 size, std::function<void()> content);
        
        bool CustomButton(const char* label, const ImVec2& size_arg = ImVec2(0, 0));
        bool CustomCheckbox(const char* label, bool* v, int* keybind = nullptr);
        bool CustomSliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = "%.2f");
        void CustomProgressBar(const char* label, float value, const ImVec2& size);
        bool CustomCombobox(const char* label, int* current_item, const char* const items[], int items_count);
        bool CustomMultiCombobox(const char* label, std::vector<bool>& selected_items, const char* const items[], int items_count);
        bool KeybindBox(int* key);
        void Tooltip(const char* text);

    public:
        UI() = default;
        ~UI() = default;

        void Render();
        bool IsOpen() const { return m_IsOpen; }
        void SetOpen(bool open) { m_IsOpen = open; }
    };
}

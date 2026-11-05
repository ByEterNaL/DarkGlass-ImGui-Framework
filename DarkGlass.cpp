#include "DarkGlass.h"
#include <string>
#include <cmath>
#include <random>
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

extern ID3D11Device* g_pd3dDevice;

namespace DarkGlass {
    void ClearBackground() {
        if (g_BackgroundImage) {
            g_BackgroundImage->Release();
            g_BackgroundImage = nullptr;
        }
        for (auto& frame : g_BackgroundGifFrames) {
            if (frame.Texture) frame.Texture->Release();
        }
        g_BackgroundGifFrames.clear();
        g_IsGif = false;
        g_CurrentGifFrame = 0;
        g_GifTimer = 0.0f;
    }

    bool EndsWith(const std::string& str, const std::string& suffix) {
        if (str.length() >= suffix.length()) {
            return (0 == str.compare(str.length() - suffix.length(), suffix.length(), suffix));
        }
        return false;
    }

    ID3D11ShaderResourceView* CreateTextureFromMem(unsigned char* image_data, int width, int height) {
        D3D11_TEXTURE2D_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;

        ID3D11Texture2D *pTexture = NULL;
        D3D11_SUBRESOURCE_DATA subResource;
        subResource.pSysMem = image_data;
        subResource.SysMemPitch = width * 4;
        subResource.SysMemSlicePitch = 0;
        
        g_pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);
        if (pTexture == NULL) return nullptr;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;

        ID3D11ShaderResourceView* srv = nullptr;
        g_pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, &srv);
        pTexture->Release();
        return srv;
    }

    void LoadBackgroundImage(const char* filepath) {
        if (!g_pd3dDevice) return;
        
        ClearBackground();
        std::string path(filepath);
        std::transform(path.begin(), path.end(), path.begin(), ::tolower);

        if (EndsWith(path, ".gif")) {
            FILE* f = nullptr;
            fopen_s(&f, filepath, "rb");
            if (!f) return;
            
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            unsigned char* buffer = (unsigned char*)malloc(size);
            fread(buffer, 1, size, f);
            fclose(f);

            int *delays = nullptr;
            int w, h, frames, comp;
            unsigned char* gif_data = stbi_load_gif_from_memory(buffer, size, &delays, &w, &h, &frames, &comp, 4);
            free(buffer);

            if (gif_data) {
                for (int i = 0; i < frames; i++) {
                    unsigned char* frame_data = gif_data + (w * h * 4 * i);
                    ID3D11ShaderResourceView* tex = CreateTextureFromMem(frame_data, w, h);
                    if (tex) {
                        GifFrame gf;
                        gf.Texture = tex;
                        gf.DelayMs = delays[i] > 0 ? delays[i] : 100; // fallback delay
                        g_BackgroundGifFrames.push_back(gf);
                    }
                }
                stbi_image_free(gif_data);
                if (delays) stbi_image_free(delays); // internally stb uses its own allocator, so stbi_image_free is safe for delays
                
                if (!g_BackgroundGifFrames.empty()) {
                    g_IsGif = true;
                    g_UseBackgroundImage = true;
                    ImGui::InsertNotification({ ImGuiToastType_Success, 3000, Loc("GIF Background loaded!", "GIF Arka Plan yüklendi!") });
                    return;
                }
            }
        }

        // Fallback or static image
        int image_width = 0, image_height = 0, channels = 0;
        unsigned char* image_data = stbi_load(filepath, &image_width, &image_height, &channels, 4);
        if (image_data == NULL) {
            ImGui::InsertNotification({ ImGuiToastType_Error, 3000, "Failed to load image! Check format/path." });
            return;
        }

        g_BackgroundImage = CreateTextureFromMem(image_data, image_width, image_height);
        stbi_image_free(image_data);
        
        if (g_BackgroundImage) {
            g_IsGif = false;
            g_UseBackgroundImage = true;
            ImGui::InsertNotification({ ImGuiToastType_Success, 3000, Loc("Custom background loaded!", "Özel arka plan yüklendi!") });
        } else {
            ImGui::InsertNotification({ ImGuiToastType_Error, 3000, "Failed to create texture!" });
        }
    }

    void OpenImageFileDialog() {
        OPENFILENAMEA ofn;
        CHAR szFile[260] = {0};
        ZeroMemory(&ofn, sizeof(OPENFILENAMEA));
        ofn.lStructSize = sizeof(OPENFILENAMEA);
        ofn.hwndOwner = NULL;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = "Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0All\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrFileTitle = NULL;
        ofn.nMaxFileTitle = 0;
        ofn.lpstrInitialDir = NULL;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

        if (GetOpenFileNameA(&ofn) == TRUE) {
            LoadBackgroundImage(ofn.lpstrFile);
        }
    }

    // --- Universal Media Controller ---
    void SendMediaKey(int vkCode) {
        keybd_event(vkCode, 0, KEYEVENTF_EXTENDEDKEY, 0);
        keybd_event(vkCode, 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
    }

    // --- Background Effects System ---
    struct Particle {
        ImVec2 pos;
        ImVec2 vel;
        float size;
        float alpha;
        float life;
    };

    static inline std::vector<Particle> g_Particles;
    static inline BackgroundEffect g_LastBgEffect = BackgroundEffect::None;

    void UpdateAndDrawBackgroundEffects(ImDrawList* drawList, ImVec2 pos, ImVec2 size) {
        if (g_BgEffect == BackgroundEffect::None) return;

        ImGuiIO& io = ImGui::GetIO();
        float dt = io.DeltaTime;

        if (g_LastBgEffect != g_BgEffect) {
            g_Particles.clear();
            g_LastBgEffect = g_BgEffect;
        }

        drawList->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y));

        if (g_BgEffect == BackgroundEffect::Snow) {
            if (g_Particles.size() < 150) {
                Particle p;
                p.pos = ImVec2(pos.x + (rand() % (int)size.x), pos.y - 10.0f - (rand() % (int)size.y));
                p.vel = ImVec2((rand() % 100) / 50.0f - 1.0f, (rand() % 50 + 50) / 1.0f);
                p.size = (rand() % 30) / 10.0f + 1.0f;
                p.alpha = (rand() % 100) / 100.0f + 0.5f;
                p.life = 1.0f;
                g_Particles.push_back(p);
            }

            for (auto it = g_Particles.begin(); it != g_Particles.end();) {
                it->pos.x += it->vel.x * dt * 20.0f;
                it->pos.y += it->vel.y * dt * 2.0f;
                it->pos.x += sin(ImGui::GetTime() + it->pos.y * 0.01f) * 0.5f; // Sway
                
                if (it->pos.y > pos.y + size.y) {
                    it->pos.y = pos.y - 10.0f;
                    it->pos.x = pos.x + (rand() % (int)size.x);
                }
                
                drawList->AddCircleFilled(it->pos, it->size, ImColor(255, 255, 255, (int)(it->alpha * 200)));
                ++it;
            }
        }
        else if (g_BgEffect == BackgroundEffect::Neural) {
            if (g_Particles.size() < 60) {
                Particle p;
                p.pos = ImVec2(pos.x + (rand() % (int)size.x), pos.y + (rand() % (int)size.y));
                p.vel = ImVec2((rand() % 100) / 50.0f - 1.0f, (rand() % 100) / 50.0f - 1.0f);
                p.size = 2.0f;
                p.alpha = 1.0f;
                g_Particles.push_back(p);
            }

            for (size_t i = 0; i < g_Particles.size(); i++) {
                g_Particles[i].pos.x += g_Particles[i].vel.x * dt * 30.0f;
                g_Particles[i].pos.y += g_Particles[i].vel.y * dt * 30.0f;

                // Bounce
                if (g_Particles[i].pos.x < pos.x || g_Particles[i].pos.x > pos.x + size.x) g_Particles[i].vel.x *= -1;
                if (g_Particles[i].pos.y < pos.y || g_Particles[i].pos.y > pos.y + size.y) g_Particles[i].vel.y *= -1;

                drawList->AddCircleFilled(g_Particles[i].pos, g_Particles[i].size, ImColor(Theme::Colors::Accent.x, Theme::Colors::Accent.y, Theme::Colors::Accent.z, g_Particles[i].alpha * 0.8f));

                for (size_t j = i + 1; j < g_Particles.size(); j++) {
                    float dist = hypot(g_Particles[i].pos.x - g_Particles[j].pos.x, g_Particles[i].pos.y - g_Particles[j].pos.y);
                    if (dist < 90.0f) {
                        float alpha = (1.0f - (dist / 90.0f)) * 0.5f;
                        ImVec4 lineColor = Theme::Colors::Accent;
                        drawList->AddLine(g_Particles[i].pos, g_Particles[j].pos, ImColor(lineColor.x, lineColor.y, lineColor.z, alpha), 1.0f);
                    }
                }
            }
        }
        else if (g_BgEffect == BackgroundEffect::Starfield) {
            if (g_Particles.size() < 200) {
                Particle p;
                p.pos = ImVec2(pos.x + (rand() % (int)size.x), pos.y + (rand() % (int)size.y));
                p.vel = ImVec2(((rand() % 100) - 50) / 10.0f, ((rand() % 100) - 50) / 10.0f);
                p.size = (rand() % 20) / 10.0f + 0.5f;
                p.alpha = (rand() % 100) / 100.0f;
                g_Particles.push_back(p);
            }

            ImVec2 center = ImVec2(pos.x + size.x / 2.0f, pos.y + size.y / 2.0f);

            for (auto it = g_Particles.begin(); it != g_Particles.end();) {
                it->pos.x -= (it->size * 15.0f) * dt; 
                if (it->pos.x < pos.x) {
                    it->pos.x = pos.x + size.x;
                    it->pos.y = pos.y + (rand() % (int)size.y);
                }
                
                it->alpha += it->vel.x * dt;
                if (it->alpha > 0.8f || it->alpha < 0.1f) it->vel.x = -it->vel.x;
                
                drawList->AddCircleFilled(it->pos, it->size * 0.5f, ImColor(255, 255, 255, (int)(it->alpha * 255)));
                ++it;
            }
        }
        drawList->PopClipRect();
    }
    // ---------------------------------

    static int* g_ActiveKeybind = nullptr;
    static bool g_KeybindListening = false;
    static int g_KeybindSkipFrames = 0;

    float& UI::GetAnimState(ImGuiID id) {
        if (m_AnimationState.find(id) == m_AnimationState.end()) {
            m_AnimationState[id] = 0.0f;
        }
        return m_AnimationState[id];
    }

    void UI::Render() {
        if (!m_IsOpen) return;

        // Handle Keybind Listening
        if (g_KeybindListening && g_ActiveKeybind) {
            if (g_KeybindSkipFrames > 0) {
                g_KeybindSkipFrames--;
            } else {
                for (int i = ImGuiKey_NamedKey_BEGIN; i < ImGuiKey_NamedKey_END; i++) {
                    if (ImGui::IsKeyPressed((ImGuiKey)i)) {
                        if (i == ImGuiKey_Escape) *g_ActiveKeybind = 0;
                        else *g_ActiveKeybind = i;
                        g_KeybindListening = false;
                        g_ActiveKeybind = nullptr;
                        break;
                    }
                }
                if (g_KeybindListening) {
                    for (int i = 0; i < 5; i++) {
                        if (ImGui::IsMouseClicked(i)) {
                            *g_ActiveKeybind = i + 1; // 1-5 for mouse
                            g_KeybindListening = false;
                            g_ActiveKeybind = nullptr;
                            break;
                        }
                    }
                }
            }
        }

        // Setup Window Style (Floating within the transparent fullscreen OS Window)
        ImGui::SetNextWindowSize(ImVec2(850, 650), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2((ImGui::GetIO().DisplaySize.x - 850) / 2.0f, (ImGui::GetIO().DisplaySize.y - 650) / 2.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.0f); // Background will be drawn manually

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Theme::Sizes::WindowRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        // Window Flags
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
        
        if (ImGui::Begin("DarkGlass_UI", &m_IsOpen, flags)) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetWindowPos();
            ImVec2 s = ImGui::GetWindowSize();
            
            g_MainUIPos = p;
            g_MainUISize = s;

            // 1. Draw Custom Window Background (Midnight Gradient)
            if (g_UseBackgroundImage && g_BackgroundImage) {
                drawList->AddImage((ImTextureID)g_BackgroundImage, p, ImVec2(p.x + s.x, p.y + s.y));
                // Add a slight dark overlay so the UI is still readable
                drawList->AddRectFilled(p, ImVec2(p.x + s.x, p.y + s.y), ImGui::GetColorU32(ImVec4(0, 0, 0, 0.6f)));
            } else {
                drawList->AddRectFilledMultiColor(p, ImVec2(p.x + s.x, p.y + s.y), 
                    ImGui::GetColorU32(Theme::Colors::BackgroundTop), 
                    ImGui::GetColorU32(Theme::Colors::BackgroundTop), 
                    ImGui::GetColorU32(Theme::Colors::BackgroundBottom), 
                    ImGui::GetColorU32(Theme::Colors::BackgroundBottom));
            }
            
            // Draw Background Effects (Snow, Neural, etc.)
            UpdateAndDrawBackgroundEffects(drawList, p, s);

            // Draw Window Border
            drawList->AddRect(p, ImVec2(p.x + s.x, p.y + s.y), ImGui::GetColorU32(Theme::Colors::Border), Theme::Sizes::WindowRounding, 0, 1.0f);

            // 2. Draw Top Bar (Integrated Navigation & Title)
            DrawTopBar(ImVec2(s.x, Theme::Sizes::TopBarHeight));

            // Main Content Layout (Below Top Bar)
            ImGui::SetCursorPos(ImVec2(0, Theme::Sizes::TopBarHeight));
            ImGui::BeginChild("##Content", ImVec2(s.x, s.y - Theme::Sizes::TopBarHeight), false, ImGuiWindowFlags_NoBackground);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, Theme::Sizes::ItemSpacing);
            DrawContent();
            ImGui::PopStyleVar();
            ImGui::EndChild();

            // Transition Animation OVERLAY (Must be drawn last over everything)
            if (g_IsTransitioning) {
                float dt = ImGui::GetIO().DeltaTime;
                if (g_IsFadingOut) {
                    g_TransitionAlpha += dt * 1.5f; // Faster transition (0.66 seconds)
                    if (g_TransitionAlpha >= 1.0f) {
                        g_TransitionAlpha = 1.0f;
                        g_Lang = g_TargetLang;
                        g_Theme = g_TargetTheme;
                        Theme::ApplyTheme(g_Theme); // Update theme colors
                        g_IsFadingOut = false; // Start fading back in
                    }
                } else {
                    g_TransitionAlpha -= dt * 1.5f; // Faster transition (0.66 seconds)
                    if (g_TransitionAlpha <= 0.0f) {
                        g_TransitionAlpha = 0.0f;
                        g_IsTransitioning = false;
                    }
                }

                if (g_TransitionAlpha > 0.0f) {
                    ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();
                    // Black overlay with slight blur/darkness matching the theme
                    fgDrawList->AddRectFilled(p, ImVec2(p.x + s.x, p.y + s.y), ImGui::GetColorU32(ImVec4(0.04f, 0.04f, 0.05f, g_TransitionAlpha)));
                    
                    // Centered Logo
                    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); // Bold font
                    ImVec2 textSize = ImGui::CalcTextSize("DarkGlass_UI");
                    fgDrawList->AddText(ImVec2(p.x + (s.x - textSize.x) * 0.5f, p.y + (s.y - textSize.y) * 0.5f), ImGui::GetColorU32(ImVec4(Theme::Colors::Text.x, Theme::Colors::Text.y, Theme::Colors::Text.z, g_TransitionAlpha)), "DarkGlass_UI");
                    ImGui::PopFont();
                }
            }
        }
        ImGui::End();

        ImGui::PopStyleVar(3);
              if (g_ShowMediaWidget) {
            
            if (g_MediaDocked) {
                ImGui::SetNextWindowPos(ImVec2(g_MainUIPos.x + g_MainUISize.x + 16.0f, g_MainUIPos.y), ImGuiCond_Always);
            } else {
                ImGui::SetNextWindowPos(ImVec2(g_MainUIPos.x + g_MainUISize.x + 16.0f, g_MainUIPos.y), ImGuiCond_FirstUseEver);
            }
            
            // Auto resize so it adapts to the content perfectly
            ImGui::SetNextWindowSizeConstraints(ImVec2(340, 0), ImVec2(340, 500));
            
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Theme::Sizes::PanelRounding);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 16));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(Theme::Colors::Panel.x, Theme::Colors::Panel.y, Theme::Colors::Panel.z, 0.98f)); // Solid color, no image
            ImGui::PushStyleColor(ImGuiCol_Border, Theme::Colors::Border);
            
            ImGuiWindowFlags mediaFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize;
            
            if (ImGui::Begin("Media Controller", &g_ShowMediaWidget, mediaFlags)) {
                
                // Force window to always be on top of the main UI
                ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
                
                // --- CUSTOM TOP BAR ---
                ImVec2 topP = ImGui::GetCursorScreenPos();
                float w = ImGui::GetContentRegionAvail().x;
                
                ImGui::TextColored(Theme::Colors::Accent, ICON_FA_MUSIC " Media");
                
                // Docking Toggle Button (Top Right)
                ImGui::SameLine(w - 24.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.1f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1,1,1,0.2f));
                ImGui::PushStyleColor(ImGuiCol_Text, g_MediaDocked ? Theme::Colors::Accent : ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                
                if (ImGui::Button(ICON_FA_THUMBTACK, ImVec2(24, 24))) {
                    g_MediaDocked = !g_MediaDocked;
                }
                ImGui::PopStyleColor(4);
                
                ImGui::Dummy(ImVec2(0, 8)); // Space after title bar
                // ----------------------
                
                static float trackProgress = 0.0f;
                static float trackDuration = 210.0f; // Fake 3:30 duration
                
                trackProgress += ImGui::GetIO().DeltaTime;
                if (trackProgress > trackDuration) trackProgress = trackDuration; // Cap it

                ImDrawList* drawList = ImGui::GetWindowDrawList();
                ImVec2 p = ImGui::GetCursorScreenPos();
                
                // 1. Draw Album Art Placeholder (Using Accent Color instead of green)
                float artSize = 64.0f;
                ImRect artRect(p, ImVec2(p.x + artSize, p.y + artSize));
                drawList->AddRectFilled(artRect.Min, artRect.Max, ImGui::GetColorU32(ImVec4(Theme::Colors::Accent.x, Theme::Colors::Accent.y, Theme::Colors::Accent.z, 0.15f)), 8.0f); // Accent glow
                drawList->AddRect(artRect.Min, artRect.Max, ImGui::GetColorU32(ImVec4(Theme::Colors::Accent.x, Theme::Colors::Accent.y, Theme::Colors::Accent.z, 0.8f)), 8.0f);
                
                // Draw Icon in center of art
                ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_PLAY); // DON'T push Fonts[1] which breaks the icon!
                drawList->AddText(ImVec2(p.x + (artSize - iconSize.x) * 0.5f, p.y + (artSize - iconSize.y) * 0.5f), ImGui::GetColorU32(Theme::Colors::Accent), ICON_FA_PLAY);

                // 2. Track Title and Artist
                ImGui::SetCursorPos(ImVec2(16 + artSize + 12, ImGui::GetCursorPosY() + 8));
                ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); // Bold
                
                ImGui::TextColored(ImVec4(1, 1, 1, 1), "Windows Media");
                ImGui::PopFont();
                
                ImGui::SetCursorPos(ImVec2(16 + artSize + 12, ImGui::GetCursorPosY() - 2));
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "Universal Control");

                ImGui::Dummy(ImVec2(0, 32)); // Space after art

                // 3. Fake Progress Bar
                p = ImGui::GetCursorScreenPos();
                float barWidth = ImGui::GetContentRegionAvail().x;
                drawList->AddRectFilled(p, ImVec2(p.x + barWidth, p.y + 4.0f), ImColor(50, 50, 60, 255), 2.0f);
                if (trackProgress > 0) {
                    float fillWidth = (trackProgress / trackDuration) * barWidth;
                    drawList->AddRectFilled(p, ImVec2(p.x + fillWidth, p.y + 4.0f), ImGui::GetColorU32(Theme::Colors::Accent), 2.0f);
                    drawList->AddCircleFilled(ImVec2(p.x + fillWidth, p.y + 2.0f), 4.0f, ImColor(255, 255, 255, 255));
                }
                ImGui::Dummy(ImVec2(0, 6));
                
                // Times
                int curMin = (int)trackProgress / 60;
                int curSec = (int)trackProgress % 60;
                int totMin = (int)trackDuration / 60;
                int totSec = (int)trackDuration % 60;
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%d:%02d", curMin, curSec);
                
                char totTimeBuf[16];
                snprintf(totTimeBuf, sizeof(totTimeBuf), "%d:%02d", totMin, totSec);
                ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(totTimeBuf).x - 16);
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", totTimeBuf);

                // 4. Playback Controls
                float btnW = 32.0f;
                float totalW = btnW * 3 + 16.0f;
                ImGui::SetCursorPosX((ImGui::GetWindowWidth() - totalW) * 0.5f);
                
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.1f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1,1,1,0.2f));
                if (ImGui::Button(ICON_FA_BACKWARD_STEP, ImVec2(btnW, btnW))) { SendMediaKey(VK_MEDIA_PREV_TRACK); trackProgress = 0; }
                ImGui::SameLine();
                // Play button is slightly highlighted
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 1.0f, 1.0f, 30.0f/255.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                if (ImGui::Button(ICON_FA_PLAY, ImVec2(btnW, btnW))) { SendMediaKey(VK_MEDIA_PLAY_PAUSE); }
                ImGui::PopStyleColor(2);
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_FORWARD_STEP, ImVec2(btnW, btnW))) { SendMediaKey(VK_MEDIA_NEXT_TRACK); trackProgress = 0; }
                ImGui::PopStyleColor(3);

                ImGui::Dummy(ImVec2(0, 4));
                
                // 5. Search Bar (Now searches YouTube)
                static char searchBuf[128] = "";
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 40.0f);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(Theme::Colors::Accent.x, Theme::Colors::Accent.y, Theme::Colors::Accent.z, 0.15f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(Theme::Colors::Accent.x, Theme::Colors::Accent.y, Theme::Colors::Accent.z, 0.25f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(Theme::Colors::Accent.x, Theme::Colors::Accent.y, Theme::Colors::Accent.z, 0.35f));
                ImGui::InputTextWithHint("##spotsearch", Loc("Search YouTube...", "YouTube'da Ara..."), searchBuf, IM_ARRAYSIZE(searchBuf));
                ImGui::PopStyleColor(3);
                ImGui::PopItemWidth();
                ImGui::SameLine();
                
                ImGui::PushStyleColor(ImGuiCol_Button, Theme::Colors::Accent);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Colors::AccentHover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Colors::Accent);
                if (ImGui::Button(ICON_FA_MAGNIFYING_GLASS, ImVec2(32, 24))) {
                    if (strlen(searchBuf) > 0) {
                        std::string query = "https://www.youtube.com/results?search_query=";
                        query += searchBuf;
                        for(char& c : query) { if(c == ' ') c = '+'; }
                        ShellExecuteA(NULL, "open", query.c_str(), NULL, NULL, SW_SHOWNORMAL);
                        memset(searchBuf, 0, sizeof(searchBuf));
                    }
                }
                ImGui::PopStyleColor(3);
                
            }
            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Theme::Sizes::WindowRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::RenderNotifications();
        ImGui::PopStyleVar(2);
    }

    void UI::DrawTopBar(ImVec2 size) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();

        // Top Bar Background
        drawList->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(Theme::Colors::TopBar), Theme::Sizes::WindowRounding, ImDrawFlags_RoundCornersTop);
        // Separator line under top bar
        drawList->AddLine(ImVec2(p.x, p.y + size.y), ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(Theme::Colors::Border));

        // Logo/Title Area
        ImGui::SetCursorScreenPos(ImVec2(p.x + 24.0f, p.y + (size.y - 20.0f) * 0.5f));
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); // Bold font
        ImGui::TextColored(Theme::Colors::Text, "DarkGlass_UI"); 
        float titleWidth = ImGui::CalcTextSize("DarkGlass_UI").x;
        ImGui::PopFont();
        
        // Vurgu Noktası (Accent dot next to logo)
        drawList->AddCircleFilled(ImVec2(p.x + 24.0f + titleWidth + 6.0f, p.y + size.y * 0.5f), 3.5f, ImGui::GetColorU32(Theme::Colors::Accent));

        // Center Tabs (Horizontal Layout)
        float totalTabWidth = m_Tabs.size() * 120.0f;
        ImGui::SetCursorScreenPos(ImVec2(p.x + (size.x - totalTabWidth) * 0.5f, p.y));
        
        ImGui::BeginGroup();
        for (int i = 0; i < m_Tabs.size(); ++i) {
            if (TabButton(m_Tabs[i].Icon, m_Tabs[i].Name, i, m_SelectedTab == i)) {
                if (m_SelectedTab != i) {
                    m_ContentAlpha = 0.0f; // Start fade in
                }
                m_SelectedTab = i;
            }
            ImGui::SameLine(0, 0);
        }
        ImGui::EndGroup();
    }

    bool UI::TabButton(const char* icon, const char* label, int index, bool selected) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;
        
        ImGuiContext& g = *GImGui;
        const ImGuiID id = window->GetID(label);

        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size(120.0f, Theme::Sizes::TopBarHeight); // Horizontal fixed size
        ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));

        ImGui::ItemSize(size, 0.0f);
        if (!ImGui::ItemAdd(bb, id)) return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

        float& animState = GetAnimState(id);
        animState = ImLerp(animState, (selected || hovered) ? 1.0f : 0.0f, g.IO.DeltaTime * 14.0f);

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Colors
        ImVec4 textCol = ImLerp(Theme::Colors::TextMuted, Theme::Colors::Text, animState);
        
        // Icon and Text layout
        ImVec2 iconSize = ImGui::CalcTextSize(icon);
        ImGui::PushFont(g.IO.Fonts->Fonts[1]);
        ImVec2 labelSize = ImGui::CalcTextSize(label);
        ImGui::PopFont();
        
        float totalWidth = iconSize.x + 8.0f + labelSize.x;
        float startX = pos.x + (size.x - totalWidth) * 0.5f;
        float centerY = pos.y + (size.y - labelSize.y) * 0.5f;

        drawList->AddText(ImVec2(startX, centerY + 1.5f), ImGui::GetColorU32(textCol), icon); // Slightly offset icon for baseline sync
        
        ImGui::PushFont(g.IO.Fonts->Fonts[1]);
        drawList->AddText(ImVec2(startX + iconSize.x + 8.0f, centerY), ImGui::GetColorU32(textCol), label);
        ImGui::PopFont();

        // Bottom Underline Indicator (Elite style)
        if (animState > 0.01f) {
            float lineY = pos.y + size.y - 2.0f;
            float lineWidth = totalWidth * animState;
            float lineX = pos.x + (size.x - lineWidth) * 0.5f;
            drawList->AddRectFilled(ImVec2(lineX, lineY - 1.0f), ImVec2(lineX + lineWidth, lineY + 1.0f), ImGui::GetColorU32(Theme::Colors::Accent));
        }

        return pressed;
    }

    void UI::DrawPanel(const char* title, ImVec2 size, std::function<void()> content) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImGuiID id = window->GetID(title);
        
        ImGui::BeginGroup();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        
        // Hover Animation
        ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
        bool hovered = ImGui::IsMouseHoveringRect(bb.Min, bb.Max);
        float& hoverAnim = GetAnimState(id);
        hoverAnim = ImLerp(hoverAnim, hovered ? 1.0f : 0.0f, ImGui::GetIO().DeltaTime * 10.0f);
        
        // Sharp Panel Background with Hover effect (Glassmorphism)
        ImVec4 bgCol = ImLerp(Theme::Colors::Panel, ImColor(14, 14, 17, 180), hoverAnim);
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(bgCol), Theme::Sizes::PanelRounding);
        
        // 1px Sharp Border with subtle hover glow
        ImVec4 borderCol = ImLerp(Theme::Colors::Border, ImColor(45, 45, 55, 255), hoverAnim);
        drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(borderCol), Theme::Sizes::PanelRounding, 0, 1.0f);

        // Panel Title
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); // Bold font
        drawList->AddText(ImVec2(pos.x + 20.0f, pos.y + 16.0f), ImGui::GetColorU32(Theme::Colors::Text), title);
        ImGui::PopFont();
        
        // Accent Dot next to title
        drawList->AddRectFilled(ImVec2(pos.x + 8.0f, pos.y + 19.0f), ImVec2(pos.x + 12.0f, pos.y + 27.0f), ImGui::GetColorU32(Theme::Colors::Accent));
        
        // Fading Gradient Separator (Premium Look)
        ImU32 sepStart = ImGui::GetColorU32(Theme::Colors::Border);
        ImU32 sepEnd = ImGui::GetColorU32(ImVec4(0, 0, 0, 0));
        drawList->AddRectFilledMultiColor(ImVec2(pos.x, pos.y + 45.0f), ImVec2(pos.x + size.x, pos.y + 46.0f), sepStart, sepEnd, sepEnd, sepStart);

        // Reserve space for the entire card in layout
        ImGui::Dummy(size);
        
        // Setup child window inside card for safe clipping
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 20.0f, pos.y + 60.0f));
        ImGui::BeginChild(title, ImVec2(size.x - 40.0f, size.y - 80.0f), false, ImGuiWindowFlags_NoBackground);
        
        // Call the custom content rendering function
        if (content) {
            content();
        }
        
        ImGui::EndChild();
        ImGui::EndGroup();
    }

    bool UI::CustomButton(const char* label, const ImVec2& size_arg) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;

        ImGuiContext& g = *GImGui;
        const ImGuiID id = window->GetID(label);
        
        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size = ImGui::CalcItemSize(size_arg, ImGui::CalcTextSize(label, NULL, true).x + 40.0f, 32.0f);

        const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
        ImGui::ItemSize(size, 0.0f);
        if (!ImGui::ItemAdd(bb, id)) return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

        float& animHover = GetAnimState(id);
        animHover = ImLerp(animHover, (hovered || held) ? 1.0f : 0.0f, g.IO.DeltaTime * 14.0f);
        
        float& animClick = GetAnimState(window->GetID((std::string(label) + "_click").c_str()));
        if (pressed) animClick = 1.0f;
        animClick = ImLerp(animClick, 0.0f, g.IO.DeltaTime * 5.0f);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        
        // Background
        ImVec4 bgCol = ImLerp(Theme::Colors::Border, Theme::Colors::AccentHover, animHover);
        drawList->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(bgCol), 4.0f);
        
        // Click Ripple/Glow
        if (animClick > 0.01f) {
            drawList->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImVec4(1,1,1, animClick * 0.4f)), 4.0f);
            drawList->AddRect(ImVec2(bb.Min.x - animClick * 4.0f, bb.Min.y - animClick * 4.0f), 
                              ImVec2(bb.Max.x + animClick * 4.0f, bb.Max.y + animClick * 4.0f), 
                              ImGui::GetColorU32(ImLerp(Theme::Colors::Accent, ImColor(0,0,0,0), 1.0f - animClick)), 4.0f, 0, 2.0f);
        }

        // Text
        ImVec2 text_size = ImGui::CalcTextSize(label, NULL, true);
                    ImVec2 text_pos = ImVec2(pos.x + (size.x - text_size.x) * 0.5f, pos.y + (size.y - text_size.y) * 0.5f);
                    drawList->AddText(text_pos, ImGui::GetColorU32(Theme::Colors::Text), label);

                    return pressed;
                }

                const char* GetKeyName(int key) {
                    if (key == 0) return "[-]";
                    if (key >= ImGuiKey_NamedKey_BEGIN && key < ImGuiKey_NamedKey_END) {
                        return ImGui::GetKeyName((ImGuiKey)key);
                    }
                    if (key == 1) return "[LMB]";
                    if (key == 2) return "[RMB]";
                    if (key == 3) return "[MMB]";
                    if (key == 4) return "[MB4]";
                    if (key == 5) return "[MB5]";
                    return "[?]";
                }

                bool UI::KeybindBox(int* key) {
                    const char* keyName = (g_KeybindListening && g_ActiveKeybind == key) ? "[...]" : GetKeyName(*key);
                    ImVec2 size = ImGui::CalcTextSize(keyName);
                    float width = size.x + 16.0f;
                    if (width < 60.0f) width = 60.0f; // Fix small hitbox
                    
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    ImRect bb(pos, ImVec2(pos.x + width, pos.y + 24.0f));
                    
                    ImGuiID id = ImGui::GetID(key);
                    ImGui::ItemSize(bb);
                    if (!ImGui::ItemAdd(bb, id)) return false;

                    bool hovered, held;
                    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
                    
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    ImVec4 bgCol = hovered ? ImColor(50, 50, 65, 255) : Theme::Colors::Border;
                    if (g_KeybindListening && g_ActiveKeybind == key) bgCol = Theme::Colors::Accent;

                    drawList->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(bgCol), 4.0f);
                    
                    ImVec2 text_size = ImGui::CalcTextSize(keyName);
                    drawList->AddText(ImVec2(pos.x + (width - text_size.x) * 0.5f, pos.y + (24.0f - text_size.y) * 0.5f), 
                        ImGui::GetColorU32((hovered || (g_KeybindListening && g_ActiveKeybind == key)) ? ImVec4(1,1,1,1) : Theme::Colors::TextMuted), keyName);
                    
                    if (pressed) {
                        g_ActiveKeybind = key;
                        g_KeybindListening = true;
                        g_KeybindSkipFrames = 2; // skip a few frames to prevent immediate click capture
                    }
                    
                    return pressed;
                }

                bool UI::CustomCheckbox(const char* label, bool* v, int* keybind) {
                    ImGuiWindow* window = ImGui::GetCurrentWindow();
                    if (window->SkipItems) return false;

                    ImGuiContext& g = *GImGui;
                    const ImGuiID id = window->GetID(label);
                    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

                    ImVec2 pos = window->DC.CursorPos;
                    float width = ImGui::GetContentRegionAvail().x - 12.0f;
                    
                    float switch_width = 30.0f;
                    float switch_height = 16.0f;
                    ImVec2 switch_pos(pos.x + width - switch_width, pos.y);
                    
                    // Calculate hitboxes for label and switch separately to avoid overlapping with keybind
                    ImRect label_bb(pos, ImVec2(pos.x + label_size.x + 8.0f, pos.y + ImMax(label_size.y, switch_height) + 2.0f));
                    ImRect switch_bb(switch_pos, ImVec2(switch_pos.x + switch_width, switch_pos.y + switch_height));
                    
                    // Total bb for item advance
                    ImRect total_bb(pos, ImVec2(pos.x + width, pos.y + ImMax(label_size.y, switch_height) + 2.0f));
                    ImGui::ItemSize(total_bb, 0.0f);
                    
                    bool hovered = false, held = false;
                    bool pressed = false;
                    
                    if (ImGui::ItemAdd(label_bb, id)) {
                        bool l_hovered, l_held;
                        if (ImGui::ButtonBehavior(label_bb, id, &l_hovered, &l_held)) pressed = true;
                        hovered |= l_hovered; held |= l_held;
                    }
                    
                    ImGuiID switch_id = window->GetID((std::string(label) + "_switch").c_str());
                    if (ImGui::ItemAdd(switch_bb, switch_id)) {
                        bool s_hovered, s_held;
                        if (ImGui::ButtonBehavior(switch_bb, switch_id, &s_hovered, &s_held)) pressed = true;
                        hovered |= s_hovered; held |= s_held;
                    }

                    if (pressed) {
                        *v = !(*v);
                    }

                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->AddText(ImVec2(pos.x, pos.y + (switch_height - label_size.y) * 0.5f), ImGui::GetColorU32(Theme::Colors::Text), label);
                    
                    if (keybind) {
                        float keybind_w = ImGui::CalcTextSize(GetKeyName(*keybind)).x + 16.0f;
                        if (keybind_w < 60.0f) keybind_w = 60.0f;
                        ImGui::SetCursorScreenPos(ImVec2(pos.x + width - switch_width - keybind_w - 8.0f, pos.y));
                        KeybindBox(keybind);
                    }

                    float& anim = GetAnimState(id);
                    anim = ImLerp(anim, *v ? 1.0f : 0.0f, g.IO.DeltaTime * 14.0f);

                    float& hoverAnim = GetAnimState(window->GetID((std::string(label) + "_hover").c_str()));
                    hoverAnim = ImLerp(hoverAnim, hovered ? 1.0f : 0.0f, g.IO.DeltaTime * 14.0f);

                    ImVec4 bgCol = ImLerp(Theme::Colors::Border, Theme::Colors::Accent, anim);
                    if (hovered && !*v) bgCol = ImLerp(bgCol, ImColor(60, 60, 75, 255), hoverAnim);
                    
                    drawList->AddRectFilled(switch_pos, ImVec2(switch_pos.x + switch_width, switch_pos.y + switch_height), ImGui::GetColorU32(bgCol), switch_height * 0.5f);
                    
                    ImVec2 circle_pos = ImVec2(switch_pos.x + switch_height * 0.5f + (switch_width - switch_height) * anim, switch_pos.y + switch_height * 0.5f);
                    drawList->AddCircleFilled(circle_pos, switch_height * 0.5f - 2.0f, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));

                    return pressed;
                }

    bool UI::CustomSliderFloat(const char* label, float* v, float v_min, float v_max, const char* format) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;

        ImGuiContext& g = *GImGui;
        const ImGuiID id = window->GetID(label);

        // Layout: Text and Value on top, Thin slider track below
        ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
        ImVec2 pos = window->DC.CursorPos;
        float width = ImGui::GetContentRegionAvail().x - 12.0f;
        float height = label_size.y + 4.0f + 10.0f; // Text + spacing + track
        
        ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));
        ImGui::ItemSize(bb, 0.0f);
        if (!ImGui::ItemAdd(bb, id)) return false;

        // Slider Bar Bounds (Thin track)
        ImRect slider_bb(ImVec2(pos.x, pos.y + label_size.y + 4.0f), ImVec2(pos.x + width, pos.y + height));

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(slider_bb, id, &hovered, &held);

        // Standard slider behavior for smooth drag
        ImRect grab_bb;
        ImGui::SliderBehavior(slider_bb, id, ImGuiDataType_Float, v, &v_min, &v_max, format, ImGuiSliderFlags_None, &grab_bb);

        float& animHover = GetAnimState(window->GetID((std::string(label) + "_hover").c_str()));
        animHover = ImLerp(animHover, (hovered || held) ? 1.0f : 0.0f, g.IO.DeltaTime * 14.0f);

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Label
        drawList->AddText(ImVec2(pos.x, pos.y), ImGui::GetColorU32(Theme::Colors::Text), label);
        
        // Value Text
        char value_buf[64];
        snprintf(value_buf, sizeof(value_buf), format, *v);
        ImVec2 val_size = ImGui::CalcTextSize(value_buf);
        drawList->AddText(ImVec2(pos.x + width - val_size.x, pos.y), ImGui::GetColorU32(Theme::Colors::TextMuted), value_buf);

        // Thick Track Background (Brightened so it's clearly visible and not empty on the left)
        float track_y = slider_bb.Min.y + 4.0f;
        ImU32 trackCol = ImColor(80, 80, 95, 255);
        drawList->AddRectFilled(ImVec2(slider_bb.Min.x, track_y), ImVec2(slider_bb.Max.x, track_y + 6.0f), trackCol, 3.0f);

        // Filled Track
        float normalized_val = ImClamp((*v - v_min) / (v_max - v_min), 0.0f, 1.0f);
        float fill_x = slider_bb.Min.x + width * normalized_val;
        
        if (normalized_val > 0.001f) {
            ImVec4 fillCol = ImLerp(Theme::Colors::Accent, Theme::Colors::AccentHover, animHover);
            drawList->AddRectFilled(ImVec2(slider_bb.Min.x, track_y), ImVec2(fill_x, track_y + 6.0f), ImGui::GetColorU32(fillCol), 3.0f);
        }
        
        // Premium Thumb (White circle with glow)
        ImVec2 thumb_center(fill_x, track_y + 3.0f);
        
        // Drop shadow
        drawList->AddCircleFilled(ImVec2(thumb_center.x, thumb_center.y + 1.0f), 5.0f, ImGui::GetColorU32(ImVec4(0,0,0,0.5f)), 16);
        // Thumb
        drawList->AddCircleFilled(thumb_center, 4.5f, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), 16);
        // Hover Glow
        if (animHover > 0.0f) {
            drawList->AddCircleFilled(thumb_center, 10.0f, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, animHover * 0.15f)), 16);
        }

        return pressed;
    }

    void UI::CustomProgressBar(const char* label, float value, const ImVec2& size) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return;

        ImGuiContext& g = *GImGui;
        const ImGuiID id = window->GetID(label);
        ImVec2 pos = window->DC.CursorPos;
        ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y + 18.0f));
        
        ImGui::ItemSize(bb, 0.0f);
        if (!ImGui::ItemAdd(bb, id)) return;
        
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        
        // Label on top
        drawList->AddText(ImVec2(pos.x, pos.y), ImGui::GetColorU32(Theme::Colors::TextMuted), label);
        
        // Percentage text
        char val_buf[32];
        snprintf(val_buf, sizeof(val_buf), "%.0f%%", value * 100.0f);
        ImVec2 valSize = ImGui::CalcTextSize(val_buf);
        drawList->AddText(ImVec2(pos.x + size.x - valSize.x, pos.y), ImGui::GetColorU32(Theme::Colors::Text), val_buf);
        
        // Track
        ImVec2 barMin = ImVec2(pos.x, pos.y + 20.0f);
        ImVec2 barMax = ImVec2(pos.x + size.x, pos.y + 20.0f + size.y);
        drawList->AddRectFilled(barMin, barMax, ImColor(60, 60, 75, 255), 4.0f);
        
        // Fill
        if (value > 0.01f) {
            float fillWidth = size.x * ImClamp(value, 0.0f, 1.0f);
            drawList->AddRectFilledMultiColor(barMin, ImVec2(barMin.x + fillWidth, barMax.y), 
                ImGui::GetColorU32(Theme::Colors::Accent), 
                ImGui::GetColorU32(Theme::Colors::AccentHover), 
                ImGui::GetColorU32(Theme::Colors::AccentHover), 
                ImGui::GetColorU32(Theme::Colors::Accent));
                
            // Glow effect
            drawList->AddRectFilledMultiColor(ImVec2(barMin.x, barMin.y - 2.0f), ImVec2(barMin.x + fillWidth, barMax.y + 2.0f), 
                ImGui::GetColorU32(ImVec4(Theme::Colors::Accent.x, Theme::Colors::Accent.y, Theme::Colors::Accent.z, 0.0f)), 
                ImGui::GetColorU32(ImVec4(Theme::Colors::AccentHover.x, Theme::Colors::AccentHover.y, Theme::Colors::AccentHover.z, 0.3f)), 
                ImGui::GetColorU32(ImVec4(Theme::Colors::AccentHover.x, Theme::Colors::AccentHover.y, Theme::Colors::AccentHover.z, 0.3f)), 
                ImGui::GetColorU32(ImVec4(Theme::Colors::Accent.x, Theme::Colors::Accent.y, Theme::Colors::Accent.z, 0.0f)));
        }
    }

    bool UI::CustomCombobox(const char* label, int* current_item, const char* const items[], int items_count) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;

        ImGuiContext& g = *GImGui;
        const ImGuiID id = window->GetID(label);

        ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
        ImVec2 pos = window->DC.CursorPos;
        float width = ImGui::GetContentRegionAvail().x - 12.0f;
        float height = 30.0f;
        
        // Layout: Text on top, Box below
        ImRect bb(ImVec2(pos.x, pos.y + label_size.y + 4.0f), ImVec2(pos.x + width, pos.y + label_size.y + 4.0f + height));
        ImRect total_bb(pos, bb.Max);
        
        ImGui::ItemSize(total_bb, 0.0f);
        if (!ImGui::ItemAdd(total_bb, id)) return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
        
        float& animHover = GetAnimState(id);
        animHover = ImLerp(animHover, (hovered || ImGui::IsPopupOpen(label)) ? 1.0f : 0.0f, g.IO.DeltaTime * 12.0f);
        
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        
        // Label
        drawList->AddText(pos, ImGui::GetColorU32(Theme::Colors::Text), label);
        
        // Box Background (Glassmorphism)
        ImVec4 bgCol = ImLerp(Theme::Colors::Border, ImColor(30, 30, 40, 200), animHover);
        drawList->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(bgCol), 4.0f);
        
        // Box Border with Glow
        ImVec4 borderCol = ImLerp(ImColor(40, 40, 50, 255), Theme::Colors::Accent, animHover);
        drawList->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(borderCol), 4.0f, 0, 1.0f);
        
        // Selected Item Text
        const char* preview_value = (*current_item >= 0 && *current_item < items_count) ? items[*current_item] : "";
        ImVec2 text_pos = ImVec2(bb.Min.x + 10.0f, bb.Min.y + (height - ImGui::CalcTextSize(preview_value).y) * 0.5f);
        drawList->AddText(text_pos, ImGui::GetColorU32(Theme::Colors::Text), preview_value);
        
        // Chevron Icon
        ImVec2 icon_size = ImGui::CalcTextSize(ICON_FA_CHEVRON_DOWN);
        ImVec2 icon_pos = ImVec2(bb.Max.x - 10.0f - icon_size.x, bb.Min.y + (height - icon_size.y) * 0.5f);
        drawList->AddText(icon_pos, ImGui::GetColorU32(Theme::Colors::TextMuted), ICON_FA_CHEVRON_DOWN);
        
        bool value_changed = false;
        if (pressed) {
            ImGui::OpenPopupEx(id, ImGuiPopupFlags_None);
        }
        
        // Popup Window
        ImGui::SetNextWindowPos(ImVec2(bb.Min.x, bb.Max.y + 4.0f));
        ImGui::SetNextWindowSize(ImVec2(width, 0.0f)); // Auto height
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        
        if (ImGui::BeginPopupEx(id, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar)) {
            
            // Draw custom popup background
            ImDrawList* popupDraw = ImGui::GetWindowDrawList();
            ImVec2 popupPos = ImGui::GetWindowPos();
            ImVec2 popupSize = ImGui::GetWindowSize();
            popupDraw->AddRectFilled(popupPos, ImVec2(popupPos.x + popupSize.x, popupPos.y + popupSize.y), ImGui::GetColorU32(Theme::Colors::Panel), 6.0f);
            popupDraw->AddRect(popupPos, ImVec2(popupPos.x + popupSize.x, popupPos.y + popupSize.y), ImGui::GetColorU32(Theme::Colors::Border), 6.0f, 0, 1.0f);
            
            for (int i = 0; i < items_count; i++) {
                ImGui::PushID(i);
                bool item_selected = (*current_item == i);
                
                ImVec2 item_pos = ImGui::GetCursorScreenPos();
                ImVec2 item_size = ImVec2(ImGui::GetContentRegionAvail().x, 30.0f); // Slightly taller for premium feel
                ImRect item_bb(item_pos, ImVec2(item_pos.x + item_size.x, item_pos.y + item_size.y));
                
                ImGui::ItemSize(item_size, 0.0f); // Use item_size directly to avoid adding padding internally
                ImGuiID item_id = ImGui::GetID("item");
                if (ImGui::ItemAdd(item_bb, item_id)) {
                    bool item_hovered, item_held;
                    bool item_pressed = ImGui::ButtonBehavior(item_bb, item_id, &item_hovered, &item_held);
                    
                    if (item_pressed) {
                        *current_item = i;
                        value_changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                    
                    float& itemAnim = GetAnimState(ImGui::GetID("item"));
                    itemAnim = ImLerp(itemAnim, (item_hovered || item_selected) ? 1.0f : 0.0f, g.IO.DeltaTime * 14.0f);
                    
                    if (itemAnim > 0.01f) {
                        popupDraw->AddRectFilled(item_bb.Min, item_bb.Max, ImGui::GetColorU32(ImLerp(ImColor(0,0,0,0), Theme::Colors::Accent, itemAnim * 0.4f)), 4.0f);
                    }
                    
                    ImVec4 itemTextCol = ImLerp(Theme::Colors::TextMuted, Theme::Colors::Text, itemAnim);
                    popupDraw->AddText(ImVec2(item_pos.x + 12.0f, item_pos.y + (item_size.y - ImGui::CalcTextSize(items[i]).y) * 0.5f), ImGui::GetColorU32(itemTextCol), items[i]);
                }
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(3);
        
        return value_changed;
    }

    bool UI::CustomMultiCombobox(const char* label, std::vector<bool>& selected_items, const char* const items[], int items_count) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;

        ImGuiContext& g = *GImGui;
        const ImGuiID id = window->GetID(label);
        
        ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
        ImVec2 pos = window->DC.CursorPos;
        float width = ImGui::GetContentRegionAvail().x - 12.0f;
        float height = 30.0f;
        
        ImRect bb(ImVec2(pos.x, pos.y + label_size.y + 4.0f), ImVec2(pos.x + width, pos.y + label_size.y + 4.0f + height));
        ImRect total_bb(pos, bb.Max);
        
        ImGui::ItemSize(total_bb, 0.0f);
        if (!ImGui::ItemAdd(total_bb, id)) return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
        
        float& animHover = GetAnimState(id);
        animHover = ImLerp(animHover, (hovered || ImGui::IsPopupOpen(label)) ? 1.0f : 0.0f, g.IO.DeltaTime * 12.0f);
        
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        
        // Label
        drawList->AddText(pos, ImGui::GetColorU32(Theme::Colors::Text), label);
        
        // Box Background
        ImVec4 bgCol = ImLerp(Theme::Colors::Border, ImColor(30, 30, 40, 200), animHover);
        drawList->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(bgCol), 4.0f);
        
        // Box Border with Glow
        ImVec4 borderCol = ImLerp(ImColor(40, 40, 50, 255), Theme::Colors::Accent, animHover);
        drawList->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(borderCol), 4.0f, 0, 1.0f);
        
        // Generate Preview Text
        std::string preview_text = "";
        int select_count = 0;
        for(int i=0; i<items_count; i++) {
            if (selected_items[i]) {
                select_count++;
                if (preview_text.empty()) preview_text = items[i];
                else preview_text += ", " + std::string(items[i]);
            }
        }
        if (select_count == 0) preview_text = Loc("None", "Hiçbiri");
        else if (select_count > 2) preview_text = std::to_string(select_count) + (g_Lang == Language::Turkish ? " Seçim" : " Selected");
        
        ImVec2 text_pos = ImVec2(bb.Min.x + 10.0f, bb.Min.y + (height - ImGui::CalcTextSize(preview_text.c_str()).y) * 0.5f);
        drawList->AddText(text_pos, ImGui::GetColorU32(select_count > 0 ? Theme::Colors::Accent : Theme::Colors::TextMuted), preview_text.c_str());
        
        // Chevron Icon
        ImVec2 icon_size = ImGui::CalcTextSize(ICON_FA_CHEVRON_DOWN);
        ImVec2 icon_pos = ImVec2(bb.Max.x - 10.0f - icon_size.x, bb.Min.y + (height - icon_size.y) * 0.5f);
        drawList->AddText(icon_pos, ImGui::GetColorU32(Theme::Colors::TextMuted), ICON_FA_CHEVRON_DOWN);
        
        bool value_changed = false;
        if (pressed) ImGui::OpenPopupEx(id, ImGuiPopupFlags_None);
        
        ImGui::SetNextWindowPos(ImVec2(bb.Min.x, bb.Max.y + 4.0f));
        ImGui::SetNextWindowSize(ImVec2(width, 0.0f)); 
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        
        if (ImGui::BeginPopupEx(id, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar)) {
            
            ImDrawList* popupDraw = ImGui::GetWindowDrawList();
            ImVec2 popupPos = ImGui::GetWindowPos();
            ImVec2 popupSize = ImGui::GetWindowSize();
            popupDraw->AddRectFilled(popupPos, ImVec2(popupPos.x + popupSize.x, popupPos.y + popupSize.y), ImGui::GetColorU32(Theme::Colors::Panel), 6.0f);
            popupDraw->AddRect(popupPos, ImVec2(popupPos.x + popupSize.x, popupPos.y + popupSize.y), ImGui::GetColorU32(Theme::Colors::Border), 6.0f, 0, 1.0f);
            
            for (int i = 0; i < items_count; i++) {
                ImGui::PushID(i);
                bool item_selected = selected_items[i];
                
                ImVec2 item_pos = ImGui::GetCursorScreenPos();
                ImVec2 item_size = ImVec2(ImGui::GetContentRegionAvail().x, 30.0f);
                ImRect item_bb(item_pos, ImVec2(item_pos.x + item_size.x, item_pos.y + item_size.y));
                
                ImGui::ItemSize(item_size, 0.0f);
                ImGuiID item_id = ImGui::GetID("item");
                if (ImGui::ItemAdd(item_bb, item_id)) {
                    bool item_hovered, item_held;
                    bool item_pressed = ImGui::ButtonBehavior(item_bb, item_id, &item_hovered, &item_held);
                    
                    if (item_pressed) {
                        selected_items[i] = !selected_items[i]; // Toggle
                        value_changed = true;
                    }
                    
                    float& itemAnim = GetAnimState(item_id);
                    itemAnim = ImLerp(itemAnim, (item_hovered || item_selected) ? 1.0f : 0.0f, g.IO.DeltaTime * 14.0f);
                    
                    if (itemAnim > 0.01f) {
                        popupDraw->AddRectFilled(item_bb.Min, item_bb.Max, ImGui::GetColorU32(ImLerp(ImColor(0,0,0,0), Theme::Colors::Accent, itemAnim * 0.4f)), 4.0f);
                    }
                    
                    ImVec4 itemTextCol = ImLerp(Theme::Colors::TextMuted, Theme::Colors::AccentHover, itemAnim);
                    popupDraw->AddText(ImVec2(item_pos.x + 24.0f, item_pos.y + (item_size.y - ImGui::CalcTextSize(items[i]).y) * 0.5f), ImGui::GetColorU32(itemTextCol), items[i]);
                    
                    // Add dot if selected
                    if (itemAnim > 0.01f) {
                        popupDraw->AddCircleFilled(ImVec2(item_pos.x + 12.0f, item_pos.y + item_size.y * 0.5f), 3.0f * itemAnim, ImGui::GetColorU32(Theme::Colors::Accent));
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(3);
        
        return value_changed;
    }

    void UI::Tooltip(const char* text) {
        ImGui::SameLine();
        ImGui::TextColored(Theme::Colors::TextMuted, "(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
            ImGui::TextUnformatted(text);
            ImGui::PopStyleColor();
            ImGui::EndTooltip();
        }
    }

    void UI::DrawContent() {
        m_ContentAlpha = ImLerp(m_ContentAlpha, 1.0f, ImGui::GetIO().DeltaTime * 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_ContentAlpha * ImGui::GetStyle().Alpha);
        
        ImGui::SetCursorPos(Theme::Sizes::Padding);
        
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]);
        ImGui::TextColored(Theme::Colors::Text, "%s Overview", m_Tabs[m_SelectedTab].Name);
        ImGui::PopFont();
        
        ImGui::Spacing();
        
        static bool bMasterSwitch = true, bDrawFrustum = false, bFrustumFill = false, bAsyncCompute = false;
        static bool bDRS = true, bVSyncLock = false, bRayShadows = false, bTessellation = false, bFrameGen = false;
        static bool bBoundsOverlay = true, bEntityLabels = false, bFrameGraph = true, bLightGizmos = true, bBoneRig = false, bMotionVectors = false;
        static bool bSceneGrid = false, bMinimap = true, bDarkStudio = false, bMatOverride = true, bOutlines = false, bCenterReticle = false, bCameraList = false, bWatermark = true;
        static bool bSpatialAudio = true, bCompressor = false, bAsioBuffer = false, bNoiseGate = false, bReverbConv = false, bSubEnhancer = false;
        static bool bFreeLook = false, bAutoSnap = false, bLiveProfiler = false;
        static int kMaster = 0, kAsync = 0, kGate = 0, kFreeLook = 0;
        
        static float fFov = 90.0f;
        static float fSmooth = 2.5f;
        static float fFrameBudget = 16.6f;
        static float fRenderDistance = 400.0f;
        static float fCameraFov = 110.0f;

        if (m_SelectedTab == 0) {
            ImGui::SetCursorPosX(Theme::Sizes::Padding.x);
            DrawPanel(Loc("System Telemetry", "Sistem Telemetrisi"), ImVec2(400, 470), [&]() {
                ImGui::TextColored(Theme::Colors::TextMuted, Loc("Workstation Tier", "İstasyon Seviyesi"));
                ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize("Studio Pro").x - 5.0f);
                ImGui::TextColored(Theme::Colors::Accent, "Studio Pro");
                
                ImGui::TextColored(Theme::Colors::TextMuted, Loc("Render Backend", "Render Motoru"));
                ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize("DirectX 11").x - 5.0f);
                ImGui::TextColored(Theme::Colors::Text, "DirectX 11");
                
                ImGui::TextColored(Theme::Colors::TextMuted, Loc("Active Process", "Aktif Süreç"));
                ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize("RenderStudio.exe").x - 5.0f);
                ImGui::TextColored(Theme::Colors::Text, "RenderStudio.exe");
                
                ImGui::TextColored(Theme::Colors::TextMuted, Loc("Pipeline State", "Boru Hattı Durumu"));
                ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(Loc("Active", "Aktif")).x - 5.0f);
                ImGui::TextColored(Theme::Colors::Text, Loc("Active", "Aktif"));
                
                ImGui::TextColored(Theme::Colors::TextMuted, Loc("Shader Cache ID", "Shader Önbellek ID"));
                ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize("0x8F...42A").x - 5.0f);
                ImGui::TextColored(Theme::Colors::Text, "0x8F...42A");
                
                ImGui::TextColored(Theme::Colors::TextMuted, Loc("Frame Pacing", "Kare Senkronu"));
                ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(Loc("OPTIMAL", "OPTİMAL")).x - 5.0f);
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), Loc("OPTIMAL", "OPTİMAL"));
                
                static float gpuProgress = 0.68f;
                CustomProgressBar(Loc("GPU Core Utilization", "GPU Çekirdek Kullanımı"), gpuProgress, ImVec2(ImGui::GetContentRegionAvail().x, 4.0f));
                
                static float shaderProgress = 1.0f;
                CustomProgressBar(Loc("Shader Compilation", "Shader Derleme Durumu"), shaderProgress, ImVec2(ImGui::GetContentRegionAvail().x, 4.0f));
                
                static float memProgress = 0.35f;
                CustomProgressBar(Loc("VRAM Allocation", "VRAM Kullanımı"), memProgress, ImVec2(ImGui::GetContentRegionAvail().x, 4.0f));
            });
            
            ImGui::SameLine(0, Theme::Sizes::ItemSpacing.x);
            
            DrawPanel(Loc("Cloud Workspace", "Bulut Çalışma Alanı"), ImVec2(400, 470), [&]() {
                ImGui::TextColored(Theme::Colors::TextMuted, Loc("Cloud Sync", "Bulut Senkronu"));
                ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(Loc("Active", "Aktif")).x - 5.0f);
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), Loc("Active", "Aktif"));
                
                ImGui::TextColored(Theme::Colors::TextMuted, Loc("Edition", "Sürüm"));
                ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(Loc("Enterprise", "Kurumsal")).x - 5.0f);
                ImGui::TextColored(Theme::Colors::Accent, Loc("Enterprise", "Kurumsal"));

                ImGui::TextColored(Theme::Colors::TextMuted, Loc("Session Uptime", "Oturum Süresi"));
                ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(Loc(" Unlimited", "Süresiz")).x - 5.0f);
                ImGui::TextColored(Theme::Colors::Text, Loc("Unlimited", "Süresiz"));
                
                if (CustomButton(Loc("Sync Studio Presets", "Stüdyo Profillerini Eşitle"), ImVec2(ImGui::GetContentRegionAvail().x, 32.0f))) {
                    ImGui::InsertNotification({ ImGuiToastType_Info, 3000, Loc("Studio Presets Synchronized!", "Stüdyo profilleri senkronize edildi!") });
                }
            });
        } 
        else if (m_SelectedTab == 1) {
            ImGui::SetCursorPosX(Theme::Sizes::Padding.x);
            DrawPanel(Loc("Render Pipeline", "Render Boru Hattı"), ImVec2(400, 470), [&]() {
                if (CustomCheckbox(Loc("Master Pipeline", "Ana Render Motoru"), &bMasterSwitch, &kMaster)) {
                    ImGui::InsertNotification({ bMasterSwitch ? ImGuiToastType_Success : ImGuiToastType_Error, 3000, bMasterSwitch ? Loc("Pipeline Enabled", "Render Motoru Açık") : Loc("Pipeline Disabled", "Render Motoru Kapalı") });
                }
                if (CustomCheckbox(Loc("Draw Camera Frustum", "Kamera Frustum Çiz"), &bDrawFrustum)) {
                    ImGui::InsertNotification({ ImGuiToastType_Info, 3000, Loc("Frustum gizmo toggled", "Frustum görünümü değiştirildi") });
                }
                CustomCheckbox(Loc("Frustum Volume Fill", "Frustum Hacim Dolgusu"), &bFrustumFill);
                
                if (CustomCheckbox(Loc("Async Compute Queue", "Asenkron Hesaplama Kuyruğu"), &bAsyncCompute, &kAsync)) {
                    ImGui::InsertNotification({ ImGuiToastType_Success, 3000, bAsyncCompute ? Loc("Async Compute ON", "Asenkron Hesaplama AÇIK") : Loc("Async Compute OFF", "Asenkron Hesaplama KAPALI") });
                }
                
                if (CustomSliderFloat(Loc("Camera Field of View", "Kamera Görüş Açısı"), &fFov, 30.0f, 140.0f, "%.0f deg")) {
                    ImGui::InsertNotification({ ImGuiToastType_Info, 1000, Loc("FOV Adjusted", "Görüş Açısı Ayarlandı") });
                }
                CustomSliderFloat(Loc("Temporal Smoothing", "Zamansal Yumuşatma (TAA)"), &fSmooth, 1.0f, 20.0f, "%.1f");
                CustomSliderFloat(Loc("Target Frame Budget", "Hedef Kare Süresi"), &fFrameBudget, 4.0f, 33.3f, "%.1f ms");
                CustomSliderFloat(Loc("Max Draw Distance", "Maksimum Çizim Mesafesi"), &fRenderDistance, 50.0f, 2000.0f, "%.0f m");
            });
            
            ImGui::SameLine(0, Theme::Sizes::ItemSpacing.x);
            
            DrawPanel(Loc("Shader & Post-FX", "Shader & Efektler"), ImVec2(400, 470), [&]() {
                ImGui::TextColored(Theme::Colors::TextMuted, Loc("Shader Profile", "Shader Profili"));
                static int current_profile = 0;
                const char* profiles[] = { "PBR Metallic-Roughness", "Deferred Studio Lit", "Unlit Wireframe Debug" };
                if (CustomCombobox("##shader_profile", &current_profile, profiles, 3)) {
                    ImGui::InsertNotification({ ImGuiToastType_Info, 3000, Loc("Loaded shader profile", "Shader profili yüklendi") });
                }

                static std::vector<bool> selected_passes = { true, false, true, false };
                const char* passes[] = { "SSAO", "SSR Reflections", "Bloom", "Volumetric Fog" };
                if (CustomMultiCombobox(Loc("Active Render Passes", "Aktif Render Katmanları"), selected_passes, passes, 4)) {
                    ImGui::InsertNotification({ ImGuiToastType_Info, 3000, Loc("Render passes updated", "Render katmanları güncellendi") });
                }

                if (CustomCheckbox(Loc("Dynamic Resolution (DRS)", "Dinamik Çözünürlük (DRS)"), &bDRS)) {
                    ImGui::InsertNotification({ bDRS ? ImGuiToastType_Success : ImGuiToastType_Error, 2000, bDRS ? "DRS Enabled" : "DRS Disabled" });
                }
                Tooltip(Loc("Automatically scales internal resolution to maintain target FPS.", "Hedef FPS değerini korumak için iç çözünürlüğü dinamik ölçekler."));
                
                CustomCheckbox(Loc("Hardware V-Sync Lock", "Donanımsal V-Sync Kilidi"), &bVSyncLock); 
                Tooltip(Loc("Synchronizes frame presentation with monitor refresh rate.", "Kare sunumunu monitör yenileme hızıyla senkronize eder."));
                
                CustomCheckbox(Loc("Raytraced Soft Shadows", "Işın İzlemeli Yumuşak Gölgeler"), &bRayShadows);
                CustomCheckbox(Loc("Hardware Tessellation", "Donanımsal Mozaikleme"), &bTessellation);
                Tooltip(Loc("Subdivides mesh geometry dynamically based on camera distance.", "Kamera mesafesine göre mesh geometrisini dinamik detaylandırır."));
                CustomCheckbox(Loc("Optical Frame Generation", "Optik Kare Üretimi"), &bFrameGen);
            });
        }
        else if (m_SelectedTab == 2) {
            ImGui::SetCursorPosX(Theme::Sizes::Padding.x);
            DrawPanel(Loc("Viewport Overlays", "Sahne Katmanları"), ImVec2(400, 470), [&]() {
                if (CustomCheckbox(Loc("Bounding Volumes (AABB)", "Sınır Kutuları (AABB)"), &bBoundsOverlay)) ImGui::InsertNotification({ bBoundsOverlay ? ImGuiToastType_Success : ImGuiToastType_Info, 1000, "AABB Bounds Toggled" });
                if (CustomCheckbox(Loc("Entity Identifier Tags", "Obje Kimlik Etiketleri"), &bEntityLabels)) ImGui::InsertNotification({ bEntityLabels ? ImGuiToastType_Success : ImGuiToastType_Info, 1000, "Entity Labels Toggled" });
                if (CustomCheckbox(Loc("Frame-Time Telemetry Bar", "Kare Süresi Çubuğu"), &bFrameGraph)) ImGui::InsertNotification({ bFrameGraph ? ImGuiToastType_Success : ImGuiToastType_Info, 1000, "Telemetry Bar Toggled" });
                CustomCheckbox(Loc("Light Source Gizmos", "Işık Kaynağı İkonları"), &bLightGizmos);
                CustomCheckbox(Loc("Armature / Bone Rig", "İskelet / Kemik Hiyerarşisi"), &bBoneRig);
                CustomCheckbox(Loc("Velocity Motion Vectors", "Hareket Vektör Çizgileri"), &bMotionVectors);
                CustomCheckbox(Loc("Occlusion Culling Check", "Görünürlük Testi (Culling)"), &bTessellation);
                CustomSliderFloat(Loc("Overlay Cull Distance", "Katman Çizim Mesafesi"), &fRenderDistance, 50.0f, 1000.0f, "%.0fm");
            });
            
            ImGui::SameLine(0, Theme::Sizes::ItemSpacing.x);
            
            DrawPanel(Loc("Scene & Shading", "Sahne & Gölgelendirme"), ImVec2(400, 470), [&]() {
                ImGui::TextColored(Theme::Colors::TextMuted, Loc("Viewport Shading Mode", "Sahne Gölgelendirme Modu"));
                static int current_shading = 0;
                const char* shading_en[] = { "Textured Lit", "Clay Matcap", "Wireframe", "Emissive Glow" };
                const char* shading_tr[] = { "Dokulu Işıklı", "Kil Model", "Tel Kafes", "Parlak Emisyon" };
                if (CustomCombobox("##shading", &current_shading, g_Lang == Language::English ? shading_en : shading_tr, 4)) {
                    ImGui::InsertNotification({ ImGuiToastType_Info, 3000, Loc("Shading mode updated!", "Gölgelendirme modu güncellendi!") });
                }

                if (CustomCheckbox(Loc("World Reference Grid", "Dünya Referans Izgarası"), &bSceneGrid)) {
                    ImGui::InsertNotification({ ImGuiToastType_Success, 3000, Loc("World Grid Toggled", "Referans Izgarası Değiştirildi") });
                }
                CustomCheckbox(Loc("Top-Down Ortho Minimap", "Üstten Görünüm Haritası"), &bMinimap);
                CustomCheckbox(Loc("Dark Studio Environment", "Karanlık Stüdyo Ortamı"), &bDarkStudio);
                CustomCheckbox(Loc("Material Override Pass", "Materyal Geçersiz Kılma"), &bMatOverride);
                CustomCheckbox(Loc("Selection Highlight Outline", "Seçim Vurgu Çerçevesi"), &bOutlines);
                CustomCheckbox(Loc("Viewport Center Reticle", "Merkez Hizalama Artısı"), &bCenterReticle);
                CustomCheckbox(Loc("Active Camera List", "Aktif Kamera Listesi"), &bCameraList);
                CustomCheckbox(Loc("Performance Watermark", "Performans Filigranı"), &bWatermark);
            });
        }
        else if (m_SelectedTab == 3) {
            ImGui::SetCursorPosX(Theme::Sizes::Padding.x);
            DrawPanel(Loc("Audio DSP Engine", "Ses & DSP Motoru"), ImVec2(400, 470), [&]() {
                if (CustomCheckbox(Loc("Spatial 3D HRTF Audio", "Uzamsal 3D HRTF Ses"), &bSpatialAudio)) {
                    ImGui::InsertNotification({ ImGuiToastType_Success, 3000, bSpatialAudio ? Loc("Spatial Audio ON", "3D Ses AÇIK") : Loc("Spatial Audio OFF", "3D Ses KAPALI") });
                }
                if (CustomCheckbox(Loc("Dynamic Range Compressor", "Dinamik Aralık Kompresörü"), &bCompressor)) {
                    ImGui::InsertNotification({ ImGuiToastType_Success, 1000, "Compressor toggled" });
                }
                CustomCheckbox(Loc("Low-Latency ASIO Buffer", "Düşük Gecikmeli ASIO Tamponu"), &bAsioBuffer);
                Tooltip(Loc("Bypasses standard mixer for sub-5ms audio monitoring.", "5ms altı ses takibi için standart karıştırıcıyı devre dışı bırakır."));
                CustomCheckbox(Loc("Studio Noise Gate", "Stüdyo Gürültü Kapısı"), &bNoiseGate, &kGate);
                CustomCheckbox(Loc("Convolution Reverb", "Evrişimli Yankı (Reverb)"), &bReverbConv);
                CustomCheckbox(Loc("Sub-Harmonic Bass Boost", "Alt Harmonik Bas Güçlendirici"), &bSubEnhancer);
            });

            ImGui::SameLine();
            DrawPanel(Loc("Camera & Profiler", "Kamera & Profilleyici"), ImVec2(400, 470), [&]() {
                if (CustomCheckbox(Loc("Free-Look Fly Camera", "Serbest Uçuş Kamerası"), &bFreeLook, &kFreeLook)) {
                    ImGui::InsertNotification({ ImGuiToastType_Success, 3000, Loc("Camera mode changed", "Kamera modu değişti") });
                }
                CustomSliderFloat(Loc("Fly Camera FOV", "Serbest Kamera Açısı"), &fCameraFov, 60.0f, 150.0f, "%.0f deg");
                CustomCheckbox(Loc("Auto-Save Frame Snapshots", "Kare Görüntülerini Otomatik Kaydet"), &bAutoSnap);
                CustomCheckbox(Loc("Live Telemetry Stream", "Canlı Telemetri Akışı"), &bLiveProfiler);
            });
        }
        else {
            ImGui::SetCursorPosX(Theme::Sizes::Padding.x);
            DrawPanel(Loc("Configuration", "Yapılandırma"), ImVec2(400, 470), [&]() {
                
                // Scan Configs Directory
                static std::vector<std::string> configFiles;
                static int current_cfg = 0;
                static bool firstRun = true;
                
                if (firstRun) {
                    configFiles.clear();
                    WIN32_FIND_DATAA findData;
                    HANDLE hFind = FindFirstFileA("Configs\\*.json", &findData);
                    if (hFind != INVALID_HANDLE_VALUE) {
                        do {
                            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                                std::string filename = findData.cFileName;
                                configFiles.push_back(filename.substr(0, filename.find_last_of('.')));
                            }
                        } while (FindNextFileA(hFind, &findData));
                        FindClose(hFind);
                    }
                    if (configFiles.empty()) {
                        configFiles.push_back("Default");
                    }
                    firstRun = false;
                }

                ImGui::TextColored(Theme::Colors::TextMuted, Loc("Config Profile", "Ayar Dosyası"));
                ImGui::Dummy(ImVec2(0, 8));
                
                // Convert to C-strings for Combobox
                std::vector<const char*> cfg_c_str;
                for (const auto& str : configFiles) {
                    cfg_c_str.push_back(str.c_str());
                }
                
                if (current_cfg >= configFiles.size()) current_cfg = configFiles.size() - 1;
                CustomCombobox(Loc("Profile", "Profil"), &current_cfg, cfg_c_str.data(), cfg_c_str.size());
                
                ImGui::Dummy(ImVec2(0, 8));
                
                static char newConfigName[64] = "";
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.12f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                ImGui::InputTextWithHint("##NewConfig", Loc("New Config Name...", "Yeni Ayar Adı..."), newConfigName, IM_ARRAYSIZE(newConfigName));
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                
                ImGui::SameLine();
                if (ImGui::Button(Loc("Create", "Oluştur"), ImVec2(70, 0))) {
                    if (strlen(newConfigName) > 0) {
                        std::string newName(newConfigName);
                        bool exists = false;
                        for (const auto& cfg : configFiles) {
                            if (cfg == newName) { exists = true; break; }
                        }
                        if (!exists) {
                            configFiles.push_back(newName);
                            current_cfg = configFiles.size() - 1;
                            std::string path = "Configs\\" + newName + ".json";
                            FILE* f;
                            fopen_s(&f, path.c_str(), "w");
                            if (f) {
                                fputs("{}", f); // Dummy content
                                fclose(f);
                            }
                            memset(newConfigName, 0, sizeof(newConfigName));
                            ImGui::InsertNotification({ ImGuiToastType_Success, 3000, Loc("Config created!", "Ayar dosyası oluşturuldu!") });
                        }
                    }
                }
                
                ImGui::Dummy(ImVec2(0, 8));
                if (CustomButton(Loc("Save Configuration", "Ayarları Kaydet"), ImVec2(ImGui::GetContentRegionAvail().x, 32.0f))) {
                    if (!configFiles.empty()) {
                        ImGui::InsertNotification({ ImGuiToastType_Success, 3000, Loc("Configuration saved successfully!", "Ayarlar başarıyla kaydedildi!") });
                    }
                }
                ImGui::Dummy(ImVec2(0, 8));
                if (CustomButton(Loc("Load Configuration", "Ayarları Yükle"), ImVec2(ImGui::GetContentRegionAvail().x, 32.0f))) {
                    if (!configFiles.empty()) {
                        ImGui::InsertNotification({ ImGuiToastType_Info, 3000, Loc("Configuration loaded!", "Ayarlar başarıyla yüklendi!") });
                    }
                }
                ImGui::Dummy(ImVec2(0, 8));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                if (CustomButton(Loc("Delete Configuration", "Ayarları Sil"), ImVec2(ImGui::GetContentRegionAvail().x, 32.0f))) {
                    if (!configFiles.empty()) {
                        std::string path = "Configs\\" + configFiles[current_cfg] + ".json";
                        remove(path.c_str());
                        configFiles.erase(configFiles.begin() + current_cfg);
                        if (current_cfg > 0) current_cfg--;
                        if (configFiles.empty()) {
                            configFiles.push_back("Default");
                            current_cfg = 0;
                        }
                        ImGui::InsertNotification({ ImGuiToastType_Error, 3000, Loc("Configuration deleted!", "Ayar dosyası silindi!") });
                    }
                }
                ImGui::PopStyleColor();
            });
            
            ImGui::SameLine(0, Theme::Sizes::ItemSpacing.x);
            
            DrawPanel(Loc("Interface", "Arayüz"), ImVec2(400, 470), [&]() {
                int current_lang = (int)g_TargetLang;
                const char* langs[] = { "English", "Turkish" };
                if (CustomCombobox(Loc("Menu Language", "Menü Dili"), &current_lang, langs, 2)) {
                    if (g_TargetLang != (Language)current_lang) {
                        g_TargetLang = (Language)current_lang;
                        g_IsTransitioning = true;
                        g_IsFadingOut = true; // Start fade to black
                        ImGui::InsertNotification({ ImGuiToastType_Success, 3000, Loc("Language changed successfully!", "Dil başarıyla değiştirildi!") });
                    }
                }

                int current_theme = (int)g_TargetTheme;
                const char* themes_en[] = { "Purple (Default)", "White", "Gray", "Red", "Blue" };
                const char* themes_tr[] = { "Mor (Varsayılan)", "Beyaz", "Gri", "Kırmızı", "Mavi" };
                if (CustomCombobox(Loc("Menu Theme", "Menü Teması"), &current_theme, g_Lang == Language::English ? themes_en : themes_tr, 5)) {
                    if (g_TargetTheme != (AppTheme)current_theme) {
                        g_TargetTheme = (AppTheme)current_theme;
                        g_IsTransitioning = true;
                        g_IsFadingOut = true; // Start fade to black
                        ImGui::InsertNotification({ ImGuiToastType_Success, 3000, Loc("Theme applied!", "Tema başarıyla uygulandı!") });
                    }
                }

                int current_bg = (int)g_BgEffect;
                const char* bgs_en[] = { "None", "Snowfall", "Neural Network", "Starfield" };
                const char* bgs_tr[] = { "Yok", "Kar Yağışı", "Nöral Ağ", "Yıldız Yağmuru" };
                if (CustomCombobox(Loc("Background Effect", "Arka Plan Efekti"), &current_bg, g_Lang == Language::English ? bgs_en : bgs_tr, 4)) {
                    g_BgEffect = (BackgroundEffect)current_bg;
                    ImGui::InsertNotification({ ImGuiToastType_Info, 3000, Loc("Background updated", "Arka plan efekti güncellendi") });
                }
                
                ImGui::Dummy(ImVec2(0, 8));
                if (CustomCheckbox(Loc("Show Media Controller", "Medya Kontrolcüsünü Göster"), &g_ShowMediaWidget)) {
                    if (g_ShowMediaWidget) ImGui::InsertNotification({ ImGuiToastType_Success, 3000, Loc("Media widget enabled!", "Medya aracı aktif!") });
                }
                
                if (g_ShowMediaWidget) {
                    if (CustomCheckbox(Loc("Dock Media Widget", "Medya Kutusunu Sabitle"), &g_MediaDocked)) {
                        ImGui::InsertNotification({ ImGuiToastType_Info, 3000, Loc("Docking updated!", "Sabitleme güncellendi!") });
                    }
                }

                if (CustomButton(Loc("Select Custom Background...", "Özel Arka Plan Seç..."), ImVec2(ImGui::GetContentRegionAvail().x, 32.0f))) {
                    OpenImageFileDialog();
                }
                
                if (g_BackgroundImage) {
                    if (CustomCheckbox(Loc("Use Custom Image", "Özel Resmi Kullan"), &g_UseBackgroundImage)) {
                        ImGui::InsertNotification({ ImGuiToastType_Info, 2000, Loc("Background updated", "Arka plan güncellendi") });
                    }
                }
            });
        }
        
        ImGui::PopStyleVar(); // Pop Content Alpha
    }
}

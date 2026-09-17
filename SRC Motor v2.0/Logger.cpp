#include "Logger.h"
#include <cstdarg>
#include <cstdio>
#include <sstream>
#include <imgui.h>

namespace Console
{
    static std::vector<std::string> g_lines;
    static std::mutex g_mutex;
    static bool g_autoscroll = true;

    void Init()
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_lines.clear();
        g_autoscroll = true;
    }

    static void PushFormatted(std::vector<std::string>& dest, const char* fmt, va_list args)
    {
        va_list tmp;
        va_copy(tmp, args);
        int size = vsnprintf(nullptr, 0, fmt, tmp);
        va_end(tmp);
        if (size < 0) return;
        std::string buf(size + 1, '\0');
        vsnprintf(&buf[0], buf.size(), fmt, args);
        if (!buf.empty() && buf.back() == '\0') buf.pop_back();
        dest.push_back(std::move(buf));
    }

    void LogInfo(const char* fmt, ...)
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        va_list args;
        va_start(args, fmt);
        PushFormatted(g_lines, fmt, args);
        va_end(args);
    }

    void LogError(const char* fmt, ...)
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        va_list args;
        va_start(args, fmt);
        std::vector<std::string> tmp;
        PushFormatted(tmp, fmt, args);
        va_end(args);
        if (!tmp.empty())
        {
            std::string s = std::string("[ERR] ") + tmp[0];
            g_lines.push_back(std::move(s));
        }
    }

    void Clear()
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_lines.clear();
    }

    void DrawConsole(const char* title, bool* p_open)
    {
        ImGui::SetNextWindowSize(ImVec2(600, 200), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(title, p_open))
        {
            ImGui::End();
            return;
        }

        if (ImGui::Button("Clear", ImVec2(60, 22))) {
            Clear();
        }

        ImGui::SameLine();

        if (ImGui::Button("Copy", ImVec2(60, 22))) {
            std::string all;
            {
                std::lock_guard<std::mutex> lk(g_mutex);
                for (auto& l : g_lines) { all += l; all += '\n'; }
            }
            ImGui::SetClipboardText(all.c_str());
        }

        ImGui::SameLine();

        ImGui::Checkbox("Autoscroll", &g_autoscroll);

        ImGui::Separator();

        ImGui::BeginChild("ConsoleRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true, ImGuiWindowFlags_HorizontalScrollbar);

        std::lock_guard<std::mutex> lk(g_mutex);
        for (size_t i = 0; i < g_lines.size(); ++i)
        {
            const std::string& line = g_lines[i];
            if (!line.empty() && line.rfind("[ERR]", 0) == 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
                ImGui::TextUnformatted(line.c_str());
                ImGui::PopStyleColor();
            }
            else {
                ImGui::TextUnformatted(line.c_str());
            }
        }

        if (g_autoscroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();

        ImGui::End();
    }
}

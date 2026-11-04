#pragma once

#include <string>
#include <vector>
#include <windows.h>
#include <gdiplus.h>
#include "render.hpp"

namespace traymenu {

struct Row {
    int id = 0;                 // 0 表示分隔线
    std::wstring label;
    bool check = false;         // 右侧开关状态
    bool danger = false;
    bool separator = false;
};

struct Metrics {
    float scale = 1.0f;
    float shadow = 0.0f;
    float cardW = 0.0f;
    float cardH = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float pad = 0.0f;
    float headerH = 0.0f;
    float rowH = 0.0f;
    float sepH = 0.0f;
    Gdiplus::RectF card;
    std::vector<Gdiplus::RectF> hit;
};

inline std::vector<Row> build(bool panelVisible, bool autostartEnabled) {
    return {
        {panelVisible ? 1002 : 1001, panelVisible ? L"收起面板" : L"显示面板"},
        {0, L"", false, false, true},
        {1005, L"添加游戏…"},
        {1003, L"重新扫描游戏库"},
        {1008, L"手柄输入隔离 / 恢复…"},
        {1004, L"打开程序目录"},
        {1007, L"编辑配置"},
        {0, L"", false, false, true},
        {1010, L"开机自启动", autostartEnabled},
        {0, L"", false, false, true},
        {1006, L"退出", false, true},
    };
}

inline Metrics layout(const std::vector<Row>& rows, float scale) {
    Metrics m;
    m.scale = scale;
    m.shadow = 14.0f * scale;
    m.pad = 7.0f * scale;
    m.headerH = 38.0f * scale;
    m.rowH = 35.0f * scale;
    m.sepH = 9.0f * scale;
    m.cardW = 252.0f * scale;
    m.width = m.cardW + m.shadow * 2;

    float y = m.pad + m.headerH + m.sepH;
    m.hit.resize(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].separator) {
            m.hit[i] = Gdiplus::RectF(m.shadow + 14 * scale, y + m.sepH * 0.5f - 0.5f,
                m.cardW - 28 * scale, 1.0f);
            y += m.sepH;
        } else {
            m.hit[i] = Gdiplus::RectF(m.shadow + 6 * scale, y, m.cardW - 12 * scale, m.rowH);
            y += m.rowH;
        }
    }
    m.cardH = y + m.pad;
    m.height = m.cardH + m.shadow * 2;
    m.card = Gdiplus::RectF(m.shadow, m.shadow, m.cardW, m.cardH);
    return m;
}

inline int rowAt(const Metrics& m, const std::vector<Row>& rows, float x, float y) {
    for (size_t i = 0; i < rows.size(); ++i) {
        if (!rows[i].separator && m.hit[i].Contains(x, y)) return (int)i;
    }
    return -1;
}

inline int firstSelectable(const std::vector<Row>& rows) {
    for (size_t i = 0; i < rows.size(); ++i) if (!rows[i].separator) return (int)i;
    return -1;
}

inline int moveSelection(const std::vector<Row>& rows, int current, int delta) {
    if (rows.empty()) return -1;
    int index = current;
    for (int step = 0; step < (int)rows.size(); ++step) {
        index = (index + delta + (int)rows.size()) % (int)rows.size();
        if (!rows[index].separator) return index;
    }
    return current;
}

inline void draw(Gdiplus::Graphics& g, const std::vector<Row>& rows, const Metrics& m,
                 const std::wstring& version, int selected) {
    auto& T = ui::theme();
    const float s = m.scale;
    const bool light = T.text.r < 128;
    const float textScale = s / std::max(0.1f, ui::g_textScale);

    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    ui::fillRoundRect(g, ui::Color(0, 0, 0, 26), m.card.X - 1 * s, m.card.Y + 8 * s,
        m.card.Width + 2 * s, m.card.Height, 16 * s);
    ui::fillRoundRect(g, ui::Color(0, 0, 0, 40), m.card.X, m.card.Y + 3 * s,
        m.card.Width, m.card.Height, 14 * s);

    Gdiplus::GraphicsPath card;
    ui::roundRectPath(card, m.card.X, m.card.Y, m.card.Width, m.card.Height, 14 * s);
    Gdiplus::LinearGradientBrush background(Gdiplus::PointF(m.card.X, m.card.Y),
        Gdiplus::PointF(m.card.X, m.card.Y + m.card.Height),
        T.panelTop.withA(250).gd(), T.panel.withA(247).gd());
    g.FillPath(&background, &card);

    Gdiplus::LinearGradientBrush wash(Gdiplus::PointF(m.card.X, m.card.Y),
        Gdiplus::PointF(m.card.X + m.card.Width, m.card.Y + m.card.Height * 0.55f),
        T.accentA.withA(light ? 8 : 13).gd(), T.accentA.withA(0).gd());
    g.FillPath(&wash, &card);

    Gdiplus::LinearGradientBrush gloss(Gdiplus::PointF(m.card.X, m.card.Y),
        Gdiplus::PointF(m.card.X, m.card.Y + m.card.Height * 0.6f),
        ui::Color(255, 255, 255, light ? 26 : 16).gd(), ui::Color(255, 255, 255, 0).gd());
    g.FillPath(&gloss, &card);
    ui::drawRoundRect(g, T.rim, m.card.X + 0.5f * s, m.card.Y + 0.5f * s,
        m.card.Width - 1 * s, m.card.Height - 1 * s, 14 * s, 1.0f * s);

    const float headerX = m.card.X + 14 * s;
    const float headerY = m.card.Y + m.pad;
    const float badge = 24 * s;
    ui::fillRoundRect(g, T.accentA.withA(light ? 24 : 34), headerX, headerY + (m.headerH - badge) / 2,
        badge, badge, 7 * s);
    ui::drawRoundRect(g, T.accentA.withA(100), headerX, headerY + (m.headerH - badge) / 2,
        badge, badge, 7 * s, 1.0f * s);
    ui::TextStyle badgeText;
    badgeText.size = 12.5f * textScale;
    badgeText.bold = true;
    badgeText.color = T.accentA;
    badgeText.halign = Gdiplus::StringAlignmentCenter;
    badgeText.ellipsis = false;
    ui::drawText(g, L"S", headerX, headerY + (m.headerH - badge) / 2, badge, badge, badgeText);

    ui::TextStyle title;
    title.size = 12.5f * textScale;
    title.bold = true;
    title.color = T.text;
    title.ellipsis = false;
    ui::drawText(g, L"SideDeck", headerX + badge + 9 * s, headerY, 100 * s, m.headerH, title);

    ui::TextStyle versionText;
    versionText.size = 9.5f * textScale;
    versionText.color = T.textFaint;
    versionText.halign = Gdiplus::StringAlignmentFar;
    versionText.ellipsis = false;
    ui::drawText(g, version, m.card.X + m.card.Width - 62 * s, headerY, 48 * s, m.headerH, versionText);

    ui::fillRoundRect(g, T.sep, headerX, headerY + m.headerH - 0.5f,
        m.card.Width - 28 * s, 1.0f * s, 0.5f * s);

    for (size_t i = 0; i < rows.size(); ++i) {
        const Row& row = rows[i];
        const Gdiplus::RectF& rect = m.hit[i];
        if (row.separator) {
            ui::fillRoundRect(g, T.sep.withA(light ? 62 : 44), rect.X, rect.Y,
                rect.Width, rect.Height, 0.5f);
            continue;
        }

        const bool active = (int)i == selected;
        if (active) {
            ui::fillRoundRect(g, T.selBg, rect.X, rect.Y, rect.Width, rect.Height, 9 * s);
            ui::fillRoundRect(g, T.accentB.withA(light ? 180 : 210), rect.X + 2 * s,
                rect.Y + 8 * s, 2.4f * s, rect.Height - 16 * s, 1.2f * s);
        }

        ui::Color textColor = row.danger
            ? (light ? ui::Color(168, 48, 62) : ui::Color(255, 154, 164))
            : (active ? T.text : T.textDim);
        ui::TextStyle label;
        label.size = 13.0f * textScale;
        label.bold = active;
        label.color = textColor;
        label.ellipsis = false;
        const float textX = rect.X + 14 * s;
        const float switchW = row.check || row.id == 1010 ? 30 * s : 0.0f;
        ui::drawText(g, row.label, textX, rect.Y, rect.Width - 24 * s - switchW, rect.Height, label);

        if (row.id == 1010) {
            const float trackW = 30 * s;
            const float trackH = 16 * s;
            const float trackX = rect.X + rect.Width - trackW - 12 * s;
            const float trackY = rect.Y + (rect.Height - trackH) / 2;
            ui::Color track = row.check ? T.accentA.withA(light ? 210 : 220)
                                        : (light ? ui::Color(15, 40, 55, 38) : ui::Color(255, 255, 255, 34));
            ui::fillRoundRect(g, track, trackX, trackY, trackW, trackH, trackH / 2);
            float knob = 12 * s;
            float knobX = row.check ? trackX + trackW - knob - 2 * s : trackX + 2 * s;
            ui::fillRoundRect(g, ui::Color(255, 255, 255, row.check ? 245 : 180),
                knobX, trackY + 2 * s, knob, knob, knob / 2);
        }
    }
}

}  // namespace traymenu

#pragma once
#include <QColor>

namespace AppColors {
    inline const QColor accent(0xEF, 0x53, 0x50);
    inline const QColor accentDark(0xC6, 0x28, 0x28);
    inline const QColor background(0x0A, 0x0A, 0x0B);
    inline const QColor containerBackground(0x14, 0x14, 0x16);
    inline const QColor containerBorder(0x2A, 0x2A, 0x2E);
    inline const QColor text(0xFF, 0xFF, 0xFF);
    inline const QColor textMuted(0x8E, 0x8E, 0x93);
    inline const QColor textSecondary(0xAE, 0xAE, 0xB2);
    inline const QColor success(0x4C, 0xAF, 0x50);
    inline const QColor warning(0xFF, 0x98, 0x00);
    inline const QColor error(0xEF, 0x53, 0x50);
    inline const QColor info(0x21, 0x96, 0xF3);
    inline const QColor divider(0x1E, 0x1E, 0x22);

    inline QString globalStyleSheet() {
        return QStringLiteral(
            "QWidget { background-color: #0A0A0B; color: #FFFFFF; font-family: -apple-system, 'Segoe UI', sans-serif; }"
            "QScrollArea { border: none; background: transparent; }"
            "QScrollBar:vertical { width: 6px; background: transparent; }"
            "QScrollBar::handle:vertical { background: #2A2A2E; border-radius: 3px; min-height: 20px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
            "QScrollBar:horizontal { height: 6px; background: transparent; }"
            "QScrollBar::handle:horizontal { background: #2A2A2E; border-radius: 3px; min-width: 20px; }"
            "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
        );
    }

    inline QString buttonStyle() {
        return QStringLiteral(
            "QPushButton {"
            "  background-color: #EF5350; color: white; border: none; border-radius: 8px;"
            "  padding: 10px 20px; font-weight: bold; font-size: 13px;"
            "}"
            "QPushButton:hover { background-color: #E53935; }"
            "QPushButton:pressed { background-color: #C62828; }"
            "QPushButton:disabled { background-color: #2A2A2E; color: #8E8E93; }"
        );
    }

    inline QString outlineButtonStyle() {
        return QStringLiteral(
            "QPushButton {"
            "  background-color: transparent; color: #FFFFFF; border: 1px solid #2A2A2E;"
            "  border-radius: 8px; padding: 8px 16px; font-size: 12px;"
            "}"
            "QPushButton:hover { background-color: #141416; border-color: #EF5350; }"
        );
    }

    inline QString inputStyle() {
        return QStringLiteral(
            "QLineEdit {"
            "  background-color: #0A0A0B; color: #FFFFFF; border: 1px solid #2A2A2E;"
            "  border-radius: 10px; padding: 10px 14px; font-size: 13px;"
            "}"
            "QLineEdit:focus { border-color: #EF5350; }"
            "QLineEdit::placeholder { color: #8E8E93; }"
        );
    }

    inline QString cardStyle() {
        return QStringLiteral(
            "background-color: #141416; border: 1px solid #2A2A2E; border-radius: 14px;"
        );
    }
}

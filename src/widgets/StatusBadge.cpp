#include "widgets/StatusBadge.h"
#include <QPainter>
#include <QPainterPath>

StatusBadge::StatusBadge(ClawStatus status, const QString &label, QWidget *parent)
    : QWidget(parent), m_status(status), m_label(label) {
    setFixedHeight(24);
    setMinimumWidth(70);

    connect(&m_pulseTimer, &QTimer::timeout, this, [this]() {
        m_pulseAlpha += m_pulseGrowing ? 0.05 : -0.05;
        if (m_pulseAlpha >= 1.0) { m_pulseAlpha = 1.0; m_pulseGrowing = false; }
        if (m_pulseAlpha <= 0.3) { m_pulseAlpha = 0.3; m_pulseGrowing = true; }
        update();
    });

    if (clawStatusShouldPulse(m_status)) m_pulseTimer.start(50);
}

void StatusBadge::setStatus(ClawStatus status, const QString &label) {
    m_status = status;
    m_label = label;
    m_pulseTimer.stop();
    if (clawStatusShouldPulse(m_status)) m_pulseTimer.start(50);
    update();
}

QString StatusBadge::text() const {
    return m_label.isEmpty() ? clawStatusName(m_status) : m_label;
}

void StatusBadge::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QColor color = clawStatusColor(m_status);
    QColor bgColor = color;
    bgColor.setAlphaF(0.15);

    QFontMetrics fm(font());
    const QString label = text();
    int textWidth = fm.horizontalAdvance(label);
    int totalWidth = 8 + 6 + textWidth + 10;
    setFixedWidth(totalWidth);

    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, width(), height()), 12, 12);
    p.fillPath(path, bgColor);

    QColor dotColor = color;
    dotColor.setAlphaF(m_pulseAlpha);
    p.setBrush(dotColor);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(14, height() / 2.0), 3, 3);

    p.setPen(color);
    QFont f = font();
    f.setPointSize(10);
    f.setWeight(QFont::Medium);
    p.setFont(f);
    p.drawText(QRect(22, 0, width() - 24, height()), Qt::AlignVCenter, label);
}

#include "widgets/SubscriptionBadge.h"
#include <QPainter>
#include <QPainterPath>
#include "theme/AppColors.h"

SubscriptionBadge::SubscriptionBadge(bool isPro, QWidget *parent)
    : QWidget(parent), m_isPro(isPro) {
    setFixedSize(m_isPro ? 60 : 50, 22);
}

void SubscriptionBadge::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QColor bg = m_isPro ? AppColors::accent : AppColors::containerBorder;
    bg.setAlphaF(m_isPro ? 0.15 : 0.5);
    QColor fg = m_isPro ? AppColors::accent : AppColors::textMuted;

    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, width(), height()), 11, 11);
    p.fillPath(path, bg);

    QFont f = font();
    f.setPixelSize(11);
    f.setBold(true);
    p.setFont(f);
    p.setPen(fg);

    QString text = m_isPro ? "★ Pro" : "Free";
    p.drawText(rect(), Qt::AlignCenter, text);
}

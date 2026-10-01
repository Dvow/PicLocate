#pragma once
#include "ui/gallery.h"
namespace piclocate {
inline QLabel *label(const QString &text, const QString &name = {}, QWidget *parent = nullptr) {
    auto *l = new QLabel(text, parent);
    l->setTextFormat(Qt::PlainText);
    if (!name.isEmpty())
        l->setObjectName(name);
    return l;
}
inline QPushButton *button(const QString &text, const QString &glyph = {},
                           const QString &name = {}) {
    auto *b = new QPushButton(text);
    b->setCursor(Qt::PointingHandCursor);
    if (!glyph.isEmpty())
        b->setIcon(icon(glyph));
    if (!name.isEmpty())
        b->setObjectName(name);
    return b;
}
} // namespace piclocate

#pragma once

#include <QFormLayout>
#include <QLabel>

// Consistent form styling for the settings panels.
inline QFormLayout *makeForm()
{
    auto *f = new QFormLayout;
    f->setContentsMargins(0, 4, 0, 4);
    f->setHorizontalSpacing(12);
    f->setVerticalSpacing(8);
    f->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return f;
}

inline QLabel *keyLabel(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName(QStringLiteral("KeyLabel"));
    return l;
}

inline QLabel *hintLabel(const QString &text = QString())
{
    auto *l = new QLabel(text);
    l->setObjectName(QStringLiteral("Hint"));
    l->setWordWrap(true);
    l->setTextFormat(Qt::RichText);
    return l;
}

#ifndef FASTQSTRINGS_H
#define FASTQSTRINGS_H

#include <QString>
#include <QStringLiteral>
#include <qcontainerfwd.h>

// For UTF-16 literals: u"hello"_f
inline QString operator""_f(const char16_t* str, std::size_t len)
{
    return Qt::Literals::StringLiterals::operator""_s(str, len);
}

#endif // FASTQSTRINGS_H

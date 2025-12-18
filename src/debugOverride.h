#ifndef DEBUG_OVERRIDE_H
#define DEBUG_OVERRIDE_H

// #define WRITEDEBUGLINES

#include <QtGlobal>
#include <QDebug>

#undef qDebug
#undef qInfo
#undef qWarning
#undef qCritical
#undef qFatal

#if defined(QT_DEBUG) && defined(WRITEDEBUGLINES)

#define qDebug()    QDebug(QtDebugMsg)
#define qInfo()     QDebug(QtInfoMsg)
#define qWarning()  QDebug(QtWarningMsg)
#define qCritical() QDebug(QtCriticalMsg)
#define qFatal()    QDebug(QtFatalMsg)

#else

#define qDebug()    if constexpr (false) QDebug(QtDebugMsg)
#define qInfo()     if constexpr (false) QDebug(QtInfoMsg)
#define qWarning()  if constexpr (false) QDebug(QtWarningMsg)
#define qCritical() if constexpr (false) QDebug(QtCriticalMsg)
#define qFatal()    if constexpr (false) QDebug(QtFatalMsg)

#endif

#endif

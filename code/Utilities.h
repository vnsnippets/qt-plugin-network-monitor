#ifndef UTILITIES_H
#define UTILITIES_H

#include <QVariant>
#include <QString>
#include <glib.h>

QVariant gvariantToString(GVariant *val);
// QVariant byteArrayVariantToString(GVariant *val);
// QString byteVariantToInt(GVariant *val);

QVariant gvariantToQVariant(GVariant *variant);

#endif // UTILITIES_H

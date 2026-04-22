#include "Utilities.h"

QVariant gvariantToString(GVariant *val) {
    GVariant *innerVal = nullptr;
    if (g_variant_is_of_type(val, G_VARIANT_TYPE_VARIANT)) {
        innerVal = g_variant_get_variant(val);
    } else {
        innerVal = g_variant_ref(val);
    }

    gchar *printed = g_variant_print(innerVal, FALSE);
    QString s = QString::fromUtf8(printed);

    g_free(printed);
    g_variant_unref(innerVal);

    return s;
}

QVariant gvariantToQVariant(GVariant *variant) {
    if (!variant) return QVariant();

    // Deep unwrap variants
    if (g_variant_is_of_type(variant, G_VARIANT_TYPE_VARIANT)) {
        GVariant *inner = g_variant_get_variant(variant);
        QVariant res = gvariantToQVariant(inner);
        g_variant_unref(inner);
        return res;
    }

    // Numbers: QML/JS handles 'double' best for all DBus numeric types
    if (g_variant_is_of_type(variant, G_VARIANT_TYPE_UINT32)) return (double)g_variant_get_uint32(variant);
    if (g_variant_is_of_type(variant, G_VARIANT_TYPE_INT32))  return (double)g_variant_get_int32(variant);
    if (g_variant_is_of_type(variant, G_VARIANT_TYPE_UINT64)) return (double)g_variant_get_uint64(variant);
    if (g_variant_is_of_type(variant, G_VARIANT_TYPE_INT64))  return (double)g_variant_get_int64(variant);
    if (g_variant_is_of_type(variant, G_VARIANT_TYPE_DOUBLE)) return g_variant_get_double(variant);
    if (g_variant_is_of_type(variant, G_VARIANT_TYPE_BOOLEAN)) return (bool)g_variant_get_boolean(variant);

    // Strings & Paths
    if (g_variant_is_of_type(variant, G_VARIANT_TYPE_STRING) || 
        g_variant_is_of_type(variant, G_VARIANT_TYPE_OBJECT_PATH)) {
        return QString::fromUtf8(g_variant_get_string(variant, nullptr));
    }

    // Byte Arrays (ay): QML cannot read QByteArray directly as a string.
    // Convert to a Hex string or UTF-8 QString so the UI can see it.
    if (g_variant_is_of_type(variant, G_VARIANT_TYPE("ay"))) {
        gsize len;
        const gchar *data = (const gchar *)g_variant_get_fixed_array(variant, &len, 1);
        return QString::fromUtf8(data, static_cast<int>(len));
    }

    // Recursively handle Arrays/Lists
    if (g_variant_is_of_type(variant, G_VARIANT_TYPE_ARRAY) && 
       !g_variant_is_of_type(variant, G_VARIANT_TYPE_DICTIONARY)) {
        QVariantList list;
        GVariantIter iter;
        g_variant_iter_init(&iter, variant);
        GVariant *child;
        while ((child = g_variant_iter_next_value(&iter))) {
            list.append(gvariantToQVariant(child));
            g_variant_unref(child);
        }
        return list;
    }

    return QVariant(); 
}

// QVariant gvariantToQVariant(GVariant *variant) {
//     if (!variant) return QVariant();
    
//     GVariant *val = nullptr;
//     if (g_variant_is_of_type(variant, G_VARIANT_TYPE_VARIANT)) {
//         val = g_variant_get_variant(variant);
//     } else {
//         val = g_variant_ref(variant);
//     }

//     QVariant result;
//     if (g_variant_is_of_type(val, G_VARIANT_TYPE_STRING)) {
//         result = QString::fromUtf8(g_variant_get_string(val, nullptr));
//     } else if (g_variant_is_of_type(val, G_VARIANT_TYPE_UINT32)) {
//         result = static_cast<quint32>(g_variant_get_uint32(val));
//     } else if (g_variant_is_of_type(val, G_VARIANT_TYPE_INT32)) {
//         result = static_cast<qint32>(g_variant_get_int32(val));
//     } else if (g_variant_is_of_type(val, G_VARIANT_TYPE_UINT64)) {
//         result = static_cast<quint64>(g_variant_get_uint64(val));
//     } else if (g_variant_is_of_type(val, G_VARIANT_TYPE_INT64)) {
//         result = static_cast<qint64>(g_variant_get_int64(val));
//     } else if (g_variant_is_of_type(val, G_VARIANT_TYPE_DOUBLE)) {
//         result = g_variant_get_double(val);
//     } else if (g_variant_is_of_type(val, G_VARIANT_TYPE_BOOLEAN)) {
//         result = g_variant_get_boolean(val);
//     } else if (g_variant_is_of_type(val, G_VARIANT_TYPE_BYTE)) {
//         guchar byteVal = g_variant_get_byte(val);
//         result = static_cast<int>(byteVal);
//     } else if (g_variant_is_of_type(val, G_VARIANT_TYPE("ay"))) {
//         // BYTE ARRAY
//         const gchar *ayVal;
//         gsize len;
//         ayVal = (const gchar *)g_variant_get_fixed_array(val, &len, 1);
//         result = QString::fromUtf8(ayVal, static_cast<int>(len));
//     } else if (g_variant_is_of_type(val, G_VARIANT_TYPE("as"))) {
//         // ARRAY OF STRINGS
//         QStringList list;
//         GVariantIter iter;
//         const gchar *str;
//         g_variant_iter_init(&iter, val);
//         while (g_variant_iter_loop(&iter, "s", &str)) {
//             list << QString::fromUtf8(str);
//         }
//         result = list;
//     } else if (g_variant_is_of_type(val, G_VARIANT_TYPE_OBJECT_PATH)) {
//         result = QString::fromUtf8(g_variant_get_string(val, nullptr));
//     } else if (g_variant_is_of_type(val, G_VARIANT_TYPE_SIGNATURE)) {
//         result = QString::fromUtf8(g_variant_get_string(val, nullptr));
//     } else {
//         // Fallback: print as string
//         gchar *printed = g_variant_print(val, FALSE);
//         result = QString::fromUtf8(printed);
//         g_free(printed);
//     }

//     g_variant_unref(val);
//     return result;
// }

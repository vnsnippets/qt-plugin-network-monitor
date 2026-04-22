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

    // Byte
    if (g_variant_is_of_type(variant, G_VARIANT_TYPE_BYTE)) {
        guchar byteVal = g_variant_get_byte(variant);
        return static_cast<int>(byteVal);
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
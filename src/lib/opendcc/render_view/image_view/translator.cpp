// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/render_view/image_view/translator.h"

#include <QApplication>
#include <QRegularExpression>
#include <QTranslator>

#include <iostream>
#include <opendcc/base/logging/logger.h>

OPENDCC_NAMESPACE_OPEN

Translator::Translator()
{
    m_i18n_dir = QDir(qApp->applicationDirPath());
    m_i18n_dir.cdUp();
    m_i18n_dir.cd("i18n");

    const auto list = m_i18n_dir.entryInfoList(QDir::Filter::Files, QDir::SortFlag::Name);
    const auto size = list.size();
    m_supported_languages.reserve(size);

    QRegularExpression reg("i18n\\.(.*)\\.qm");
    for (const auto& entry : list)
    {
        const auto match = reg.match(entry.fileName());
        if (!match.hasMatch())
        {
            continue;
        }
        const auto lang = match.captured(1);
        m_supported_languages.push_back(lang);
    }

    for (const auto& languages : m_supported_languages)
    {
        const auto language_enum = QLocale(languages).language();
        const auto beauty_name = QLocale::languageToString(language_enum);
        m_enum_name_map[beauty_name] = languages;
    }
}

Translator& Translator::instance()
{
    static Translator s_instance;
    return s_instance;
}

const QDir& Translator::get_i18n_dir() const
{
    return m_i18n_dir;
}

const QVector<QString>& Translator::get_supported_languages() const
{
    return m_supported_languages;
}

QList<QString> Translator::get_supported_beauty_languages() const
{
    return m_enum_name_map.keys();
}

QString Translator::from_beauty(const QString& beauty) const
{
    return m_enum_name_map[beauty];
}

QString Translator::to_beauty(const QString& language) const
{
    const auto language_enum = QLocale(language).language();
    return QLocale::languageToString(language_enum);
}

bool Translator::set_language(const QString& language)
{
    if (m_translator)
    {
        m_translator->deleteLater();
    }
    m_translator = new QTranslator(qApp);

    const auto locale = QLocale(language);
    auto res = m_translator->load(locale, "i18n", ".", m_i18n_dir.path());
    if (!res)
    {
        OPENDCC_ERROR("Failed to load internationalization file for '{}' language.", locale.languageToString(locale.language()).toStdString());
        return false;
    }

    if (!QApplication::installTranslator(m_translator))
    {
        OPENDCC_ERROR("Failed to install QTranslator.");
        return false;
    }
    return true;
}

OPENDCC_NAMESPACE_CLOSE

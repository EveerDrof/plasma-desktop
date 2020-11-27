/* This file is part of the KDE Project
   Copyright (c) 2014 Vishesh Handa <me@vhanda.in>
   Copyright (c) 2020 Alexander Lohnau <alexander.lohnau@gmx.de>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "kcm.h"

#include <KPluginFactory>
#include <KAboutData>
#include <KSharedConfig>
#include <QDebug>
#include <QStandardPaths>
#include <KLocalizedString>
#include <KRunner/RunnerManager>
#include <KPluginSelector>
#include <KNS3/Button>
#include <KActivities/Info>

#include <QApplication>
#include <QDBusMessage>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QVBoxLayout>
#include <QLabel>
#include <QDialog>
#include <QPainter>
#include <QFormLayout>
#include <QFileInfo>
#include <QMenu>
#include <QAction>
#include <QToolButton>

K_PLUGIN_FACTORY(SearchConfigModuleFactory, registerPlugin<SearchConfigModule>();)


SearchConfigModule::SearchConfigModule(QWidget* parent, const QVariantList& args)
    : KCModule(parent, args)
    , m_config("krunnerrc")
    , m_consumer(new KActivities::Consumer(this))
{
    KAboutData* about = new KAboutData(QStringLiteral("kcm_search"), i18nc("kcm name for About dialog", "Configure Search Bar"),
                                       QStringLiteral("0.1"), QString(), KAboutLicense::LGPL);
    about->addAuthor(i18n("Vishesh Handa"), QString(), QStringLiteral("vhanda@kde.org"));
    setAboutData(about);
    setButtons(Apply | Default);

    if(!args.at(0).toString().isEmpty()) {
        m_pluginID = args.at(0).toString();
    }

    QVBoxLayout* layout = new QVBoxLayout(this);

    QHBoxLayout *headerLayout = new QHBoxLayout(this);

    QLabel *label = new QLabel(i18n("Enable or disable plugins (used in KRunner and Application Launcher)"));

    m_clearHistoryButton = new QToolButton(this);
    m_clearHistoryButton->setIcon(QIcon::fromTheme(isRightToLeft() ? QStringLiteral("edit-clear-locationbar-ltr")
                                                                   : QStringLiteral("edit-clear-locationbar-rtl")));
    m_clearHistoryButton->setPopupMode(QToolButton::InstantPopup);
    m_clearHistoryButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(m_clearHistoryButton, &QPushButton::clicked, this, &SearchConfigModule::deleteAllHistory);

    QHBoxLayout *configHeaderLayout = new QHBoxLayout(this);
    QVBoxLayout *configHeaderLeft = new QVBoxLayout(this);
    QVBoxLayout *configHeaderRight = new QVBoxLayout(this);

    // Options where KRunner should pop up
    m_topPositioning = new QRadioButton(i18n("Top"), this);
    connect(m_topPositioning, &QRadioButton::clicked, this, &SearchConfigModule::markAsChanged);
    m_freeFloating = new QRadioButton(i18n("Center"), this);
    connect(m_freeFloating, &QRadioButton::clicked, this, &SearchConfigModule::markAsChanged);

    QFormLayout *positionLayout = new QFormLayout(this);
    positionLayout->addRow(i18n("Position on screen:"), m_topPositioning);
    positionLayout->addRow(QString(), m_freeFloating);
    configHeaderLeft->addLayout(positionLayout);
    m_enableHistory = new QCheckBox(i18n("Enable"), this);
    positionLayout->addItem(new QSpacerItem(0, 0));
    positionLayout->addRow(i18n("History:"), m_enableHistory);
    connect(m_enableHistory, &QCheckBox::clicked, this, &SearchConfigModule::markAsChanged);
    connect(m_enableHistory, &QCheckBox::clicked, m_clearHistoryButton, &QPushButton::setEnabled);
    m_retainPriorSearch = new QCheckBox(i18n("Retain previous search"), this);
    connect(m_retainPriorSearch, &QCheckBox::clicked, this, &SearchConfigModule::markAsChanged);
    positionLayout->addRow(QString(), m_retainPriorSearch);
    m_activityAware = new QCheckBox(i18n("Activity aware (previous search and history)"), this);
    connect(m_activityAware, &QCheckBox::clicked, this, &SearchConfigModule::markAsChanged);
    connect(m_activityAware, &QCheckBox::clicked, this, &SearchConfigModule::configureClearHistoryButton);
    positionLayout->addRow(QString(), m_activityAware);
    configHeaderLeft->addLayout(positionLayout);

    configHeaderRight->setSizeConstraint(QLayout::SetNoConstraint);
    configHeaderRight->setAlignment(Qt::AlignBottom);
    configHeaderRight->addWidget(m_clearHistoryButton);

    configHeaderLayout->addLayout(configHeaderLeft);
    configHeaderLayout->addStretch();
    configHeaderLayout->addLayout(configHeaderRight);

    headerLayout->addWidget(label);
    headerLayout->addStretch();

    m_pluginSelector = new KPluginSelector(this);

    connect(m_pluginSelector, &KPluginSelector::changed, this, [this] { markAsChanged(); });
    connect(m_pluginSelector, &KPluginSelector::defaulted, this, &KCModule::defaulted);

    qDBusRegisterMetaType<QByteArrayList>();
    qDBusRegisterMetaType<QHash<QString, QByteArrayList>>();
    // This will trigger the reloadConfiguration method for the runner
    connect(m_pluginSelector, &KPluginSelector::configCommitted, this, [](const QByteArray &componentName){
        QDBusMessage message = QDBusMessage::createSignal(QStringLiteral("/krunnerrc"),
                                                          QStringLiteral("org.kde.kconfig.notify"),
                                                          QStringLiteral("ConfigChanged"));
        const QHash<QString, QByteArrayList> changes = {{QStringLiteral("Runners"), {componentName}}};
        message.setArguments({QVariant::fromValue(changes)});
        QDBusConnection::sessionBus().send(message);
    });

    layout->addLayout(configHeaderLayout);
    layout->addSpacing(12);
    layout->addLayout(headerLayout);
    layout->addWidget(m_pluginSelector);

    QHBoxLayout *downloadLayout = new QHBoxLayout(this);
    KNS3::Button *downloadButton = new KNS3::Button(i18n("Get New Plugins..."), QStringLiteral("krunner.knsrc"), this);
    connect(downloadButton, &KNS3::Button::dialogFinished, this, [this](const KNS3::Entry::List &changedEntries) {
       if (!changedEntries.isEmpty()) {
           m_pluginSelector->clearPlugins();
QT_WARNING_PUSH
QT_WARNING_DISABLE_CLANG("-Wdeprecated-declarations")
QT_WARNING_DISABLE_GCC("-Wdeprecated-declarations")
           m_pluginSelector->addPlugins(Plasma::RunnerManager::listRunnerInfo(),
                                        KPluginSelector::ReadConfigFile,
                                        i18n("Available Plugins"), QString(),
                                        KSharedConfig::openConfig(QStringLiteral("krunnerrc")));
QT_WARNING_POP
       }
    });
    downloadLayout->addStretch();
    downloadLayout->addWidget(downloadButton);
    layout->addLayout(downloadLayout);
}

void SearchConfigModule::load()
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("krunnerrc"));
    const KConfigGroup general = config->group("General");
    bool freeFloating = general.readEntry("FreeFloating", false);
    m_topPositioning->setChecked(!freeFloating);
    m_freeFloating->setChecked(freeFloating);
    m_retainPriorSearch->setChecked(general.readEntry("RetainPriorSearch", true));
    bool historyEnabled = general.readEntry("HistoryEnabled", true);
    m_enableHistory->setChecked(historyEnabled);
    m_clearHistoryButton->setEnabled(historyEnabled);
    m_activityAware->setChecked(general.readEntry("ActivityAware", true));

    // Set focus on the pluginselector to pass focus to search bar.
    m_pluginSelector->setFocus(Qt::OtherFocusReason);

QT_WARNING_PUSH
QT_WARNING_DISABLE_CLANG("-Wdeprecated-declarations")
QT_WARNING_DISABLE_GCC("-Wdeprecated-declarations")
    m_pluginSelector->addPlugins(Plasma::RunnerManager::listRunnerInfo(),
                    KPluginSelector::ReadConfigFile,
                    i18n("Available Plugins"), QString(),
                    config);
QT_WARNING_POP
    m_pluginSelector->load();

    if(!m_pluginID.isEmpty()){
        m_pluginSelector->showConfiguration(m_pluginID);
    }
    configureClearHistoryButton();
}


void SearchConfigModule::save()
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("krunnerrc"));
    config->group("General").writeEntry("FreeFloating", m_freeFloating->isChecked(), KConfig::Notify);
    config->group("General").writeEntry("RetainPriorSearch", m_retainPriorSearch->isChecked(), KConfig::Notify);
    config->group("General").writeEntry("HistoryEnabled", m_enableHistory->isChecked(), KConfig::Notify);
    config->group("General").writeEntry("ActivityAware", m_activityAware->isChecked(), KConfig::Notify);

    // Combine & write history
    if (!m_activityAware->isChecked()) {
        KConfigGroup historyGrp = m_config.group("PlasmaRunnerManager").group("History");
        // The old config gets migrated to the activity specific keys, so the all key does not exist by default
        if (!historyGrp.hasKey("all")) {
            QStringList activities = m_consumer->activities();
            activities.removeOne(m_consumer->currentActivity());
            QStringList newHistory = historyGrp.readEntry(m_consumer->currentActivity(), QStringList());
            for (const QString &activity : qAsConst(activities)) {
                newHistory.append(historyGrp.readEntry(activity, QStringList()));
            }
            newHistory.removeDuplicates();
            historyGrp.writeEntry("all", newHistory, KConfig::Notify);
            historyGrp.sync();
        }
    }

    m_pluginSelector->save();

    QDBusMessage message = QDBusMessage::createSignal(QStringLiteral("/krunnerrc"),
                                                      QStringLiteral("org.kde.kconfig.notify"),
                                                      QStringLiteral("ConfigChanged"));
    const QHash<QString, QByteArrayList> changes = {{QStringLiteral("Plugins"), {}}};
    message.setArguments({QVariant::fromValue(changes)});
    QDBusConnection::sessionBus().send(message);
}

void SearchConfigModule::defaults()
{
    m_topPositioning->setChecked(true);
    m_freeFloating->setChecked(false);
    m_retainPriorSearch->setChecked(true);
    m_enableHistory->setChecked(true);
    m_clearHistoryButton->setEnabled(true);
    m_activityAware->setEnabled(true);
    m_pluginSelector->defaults();
}

void SearchConfigModule::configureClearHistoryButton()
{
    KConfigGroup historyGroup = m_config.group("PlasmaRunnerManager").group("History");
    const QStringList activities = m_consumer->activities();
    const QStringList historyKeys = historyGroup.keyList();
    m_clearHistoryButton->setEnabled(true); // We always want to show the dropdown
    if (m_activityAware->isChecked() && activities.length() > 1) {
        auto *installMenu = new QMenu(m_clearHistoryButton);
        QAction *all = installMenu->addAction(m_clearHistoryButton->icon(),
                i18nc("delete history for all activities", "For all activities"));
        installMenu->setEnabled(!historyKeys.isEmpty());
        connect(all, &QAction::triggered, this, &SearchConfigModule::deleteAllHistory);
        for (const auto &key : activities) {
            KActivities::Info info(key);
            QIcon icon;
            const QString iconStr = info.icon();
            if (iconStr.isEmpty()) {
                icon = m_clearHistoryButton->icon();
            } else if (QFileInfo::exists(iconStr)) {
                icon = QIcon(iconStr);
            } else {
                icon = QIcon::fromTheme(iconStr);
            }
            QAction *singleActivity = installMenu->addAction(icon,
                    i18nc("delete history for this activity", "For activity \"%1\"", info.name()));
            singleActivity->setEnabled(historyKeys.contains(key)); // Otherwise there would be nothing to delete
            connect(singleActivity, &QAction::triggered, this, [this, key](){ deleteHistoryGroup(key); });
            installMenu->addAction(singleActivity);
            m_clearHistoryButton->setText("Clear History...");
        }
        m_clearHistoryButton->setMenu(installMenu);
    } else {
        m_clearHistoryButton->setText("Clear History");
        m_clearHistoryButton->setMenu(nullptr);
        m_clearHistoryButton->setEnabled(!historyKeys.isEmpty());
    }
}

void SearchConfigModule::deleteHistoryGroup(const QString &key)
{
    KConfigGroup historyGroup = m_config.group("PlasmaRunnerManager").group("History");
    historyGroup.deleteEntry(key, KConfig::Notify);
    historyGroup.sync();
    configureClearHistoryButton();
}

void SearchConfigModule::deleteAllHistory()
{
    KConfigGroup historyGroup = m_config.group("PlasmaRunnerManager").group("History");
    historyGroup.deleteGroup(KConfig::Notify);
    historyGroup.sync();
    configureClearHistoryButton();
}

#include "kcm.moc"

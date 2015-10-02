/*
 * Copyright (C) 2015 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ShellApplication.h"

// Qt
#include <QLibrary>
#include <QScreen>

#include <libintl.h>

// libandroid-properties
#include <hybris/properties/properties.h>

// local
#include <paths.h>
#include "CachingNetworkManagerFactory.h"
#include "MouseTouchAdaptor.h"
#include "UnityCommandLineParser.h"

ShellApplication::ShellApplication(int & argc, char ** argv, bool isMirServer)
    : QGuiApplication(argc, argv)
{

    setApplicationName(QStringLiteral("unity8"));

    connect(this, &QGuiApplication::screenAdded, this, &ShellApplication::onScreenAdded);

    setupQmlEngine(isMirServer);

    UnityCommandLineParser parser(*this);

    if (!parser.deviceName().isEmpty()) {
        m_qmlArgs.setDeviceName(parser.deviceName());
    } else {
        char buffer[200];
        property_get("ro.product.device", buffer /* value */, "desktop" /* default_value*/);
        m_qmlArgs.setDeviceName(QString(buffer));
    }

    m_qmlArgs.setMode(parser.mode());

    // The testability driver is only loaded by QApplication but not by QGuiApplication.
    // However, QApplication depends on QWidget which would add some unneeded overhead => Let's load the testability driver on our own.
    if (parser.hasTestability() || getenv("QT_LOAD_TESTABILITY")) {
        QLibrary testLib(QStringLiteral("qttestability"));
        if (testLib.load()) {
            typedef void (*TasInitialize)(void);
            TasInitialize initFunction = (TasInitialize)testLib.resolve("qt_testability_init");
            if (initFunction) {
                initFunction();
            } else {
                qCritical("Library qttestability resolve failed!");
            }
        } else {
            qCritical("Library qttestability load failed!");
        }
    }

    bindtextdomain("unity8", translationDirectory().toUtf8().data());
    textdomain("unity8");

    m_shellView.reset(new ShellView(m_qmlEngine, &m_qmlArgs));

    if (parser.windowGeometry().isValid()) {
        m_shellView->setWidth(parser.windowGeometry().width());
        m_shellView->setHeight(parser.windowGeometry().height());
    }

    if (parser.hasFrameless()) {
        m_shellView->setFlags(Qt::FramelessWindowHint);
    }

    // You will need this if you want to interact with touch-only components using a mouse
    // Needed only when manually testing on a desktop.
    if (parser.hasMouseToTouch()) {
        m_mouseTouchAdaptor.reset(MouseTouchAdaptor::instance());
    }


    // Some hard-coded policy for now.
    // NB: We don't support more than two screens at the moment
    //
    // TODO: Support an arbitrary number of screens and different policies
    //       (eg cloned desktop, several desktops, etc)
    if (isMirServer && screens().count() == 2) {
        m_shellView->setScreen(screens().at(1));

        m_secondaryWindow.reset(new SecondaryWindow(m_qmlEngine));
        m_secondaryWindow->setScreen(screens().at(0));
        // QWindow::showFullScreen() also calls QWindow::requestActivate() and we don't want that!
        m_secondaryWindow->setWindowState(Qt::WindowFullScreen);
        m_secondaryWindow->setVisible(true);
    }

    if (isMirServer || parser.hasFullscreen()) {
        m_shellView->showFullScreen();
    } else {
        m_shellView->show();
    }
}

void ShellApplication::setupQmlEngine(bool isMirServer)
{
    m_qmlEngine = new QQmlEngine(this);

    m_qmlEngine->setBaseUrl(QUrl::fromLocalFile(::qmlDirectory()));

    prependImportPaths(m_qmlEngine, ::overrideImportPaths());
    if (!isMirServer) {
        prependImportPaths(m_qmlEngine, ::nonMirImportPaths());
    }
    appendImportPaths(m_qmlEngine, ::fallbackImportPaths());

    m_qmlEngine->setNetworkAccessManagerFactory(new CachingNetworkManagerFactory);

    QObject::connect(m_qmlEngine, &QQmlEngine::quit, this, &QGuiApplication::quit);
}

void ShellApplication::onScreenAdded(QScreen * /*screen*/)
{
    // TODO: Support an arbitrary number of screens and different policies
    //       (eg cloned desktop, several desktops, etc)
    if (screens().count() == 2) {
        m_shellView->setScreen(screens().at(1));
        // Changing the QScreen where a QWindow is drawn makes it also lose focus (besides having
        // its backing QPlatformWindow recreated). So lets refocus it.
        m_shellView->requestActivate();

        m_secondaryWindow.reset(new SecondaryWindow(m_qmlEngine));
        m_secondaryWindow->setScreen(screens().at(0));

        // QWindow::showFullScreen() also calls QWindow::requestActivate() and we don't want that!
        m_secondaryWindow->setWindowState(Qt::WindowFullScreen);
        m_secondaryWindow->setVisible(true);
    }
}

void ShellApplication::onScreenAboutToBeRemoved(QScreen *screen)
{
    // TODO: Support an arbitrary number of screens and different policies
    //       (eg cloned desktop, several desktops, etc)
    if (screen == m_shellView->screen()) {
        Q_ASSERT(screens().count() > 1);
        Q_ASSERT(screens().at(0) != screen);
        Q_ASSERT(!m_secondaryWindow.isNull());
        m_secondaryWindow.reset();
        m_shellView->setScreen(screens().first());
        // Changing the QScreen where a QWindow is drawn makes it also lose focus (besides having
        // its backing QPlatformWindow recreated). So lets refocus it.
        m_shellView->requestActivate();
    }
}

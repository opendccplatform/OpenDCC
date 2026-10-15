// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/vendor/cli11/CLI11.hpp"

#include "opendcc/opendcc.h"
#include "opendcc/base/defines.h"

#include <QApplication>
#include <QMessageBox>
#include "crash_reporter_window.h"
#include "opendcc/base/app_config/config.h"
#include "opendcc/base/fs_utils/defines.h"
#include <QProcess>
#include <QStyleFactory>
#include <QFile>
#include <QDir>

#ifdef OPENDCC_OS_WINDOWS
#include <Windows.h>
#include <TlHelp32.h>
#else
#include <unistd.h>
#endif
#include <qstylefactory.h>
#include "opendcc/base/utils/process.h"

#include <thread>

OPENDCC_NAMESPACE_USING

void wait_parent_process_die(const std::string& parent_proc_name)
{
#ifdef OPENDCC_OS_WINDOWS
    auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        exit(-1);
    }

    PROCESSENTRY32 pe32 = { 0 };
    pe32.dwSize = sizeof(pe32);

    const auto pid = GetCurrentProcessId();
    if (Process32First(snapshot, &pe32))
    {
        HANDLE parent_proc_handle = INVALID_HANDLE_VALUE;
        do
        {
            if (pe32.th32ProcessID == pid)
            {
                auto parent_proc_id = pe32.th32ParentProcessID;

                auto parent_proc_handle = OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, parent_proc_id);
                if (parent_proc_handle == INVALID_HANDLE_VALUE)
                {
                    break;
                }

                // Check that reporter was launched by main application
                CHAR parent_proc_name_buf[260];
                DWORD proc_len = 260;
                if (QueryFullProcessImageName(parent_proc_handle, 0, parent_proc_name_buf, &proc_len))
                {
                    if (std::string(parent_proc_name_buf).find(parent_proc_name) == std::string::npos)
                    {
                        break;
                    }
                }

                FILETIME parent_creation_time, parent_exit_time, parent_kernel_time, parent_user_time;
                if (!GetProcessTimes(parent_proc_handle, &parent_creation_time, &parent_exit_time, &parent_kernel_time, &parent_user_time))
                {
                    break;
                }

                FILETIME cur_creation_time, cur_exit_time, cur_kernel_time, cur_user_time;
                if (!GetProcessTimes(GetCurrentProcess(), &cur_creation_time, &cur_exit_time, &cur_kernel_time, &cur_user_time))
                {
                    break;
                }

                // Compare parent proc creation time and cur proc creation time
                // -1 means parent is earlier, otherwise parent process already dead
                if (CompareFileTime(&parent_creation_time, &cur_creation_time) == -1)
                {
                    // wait for 3 seconds
                    WaitForSingleObject(parent_proc_handle, 3'000);
                }
                break;
            }
        } while (Process32Next(snapshot, &pe32));

        if (parent_proc_handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(parent_proc_handle);
        }
    }

    CloseHandle(snapshot);
#else
    std::this_thread::sleep_for(std::chrono::seconds(3));
#endif
}

void setup_qt_ui()
{
    QApplication::setStyle("fusion");
    QPalette palette = QApplication::palette();

    palette.setColor(QPalette::Window, QColor::fromRgb(68, 68, 68));
    palette.setColor(QPalette::Base, QColor::fromRgb(48, 48, 48));
    palette.setColor(QPalette::AlternateBase, QColor::fromRgb(55, 55, 55));

    palette.setColor(QPalette::Button, QColor::fromRgb(80, 80, 80));
    palette.setColor(QPalette::Text, QColor::fromRgb(200, 200, 200));
    palette.setColor(QPalette::ButtonText, QColor::fromRgb(200, 200, 200));
    palette.setColor(QPalette::WindowText, QColor::fromRgb(200, 200, 200));
    palette.setColor(QPalette::Highlight, QColor::fromRgb(103, 141, 178));
    palette.setColor(QPalette::Light, QColor::fromRgb(80, 80, 80));

    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor::fromRgb(42, 42, 42));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor::fromRgb(100, 100, 100));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor::fromRgb(90, 90, 90));
    palette.setColor(QPalette::Disabled, QPalette::Light, QColor::fromRgb(30, 30, 30));

    QApplication::setPalette(palette);

    QApplication::setAttribute(Qt::AA_DisableWindowContextHelpButton);
    QString stylesheet_path = ":/stylesheets/application_stylesheet.qss";

    QFile app_stylesheet_file(stylesheet_path);
    app_stylesheet_file.open(QFile::ReadOnly);
    qApp->setStyleSheet(QString(app_stylesheet_file.readAll()));
    app_stylesheet_file.close();
}

QString get_config_path()
{
    QDir config_path(OPENDCC_NAMESPACE::base::utils::get_executable_path().c_str());
    config_path.cdUp();
    config_path.cdUp();
#ifdef OPENDCC_OS_MAC
    config_path.cd("Resources");
#endif
    config_path.cd("configs");
    return config_path.filePath("default.toml");
}

int main(int argc, char** argv)
{
    const auto config_path = get_config_path();
    const auto app_config = OPENDCC_NAMESPACE::base::app_config::ApplicationConfig(config_path.toLocal8Bit().toStdString());
    const auto dsn = app_config.get("sentry.dsn", std::string());

    if (dsn.empty())
    {
        return -1;
    }

    // wait while parent process has died
    const auto parent_process_name = app_config.get<std::string>("settings.app.name", "");
    if (parent_process_name.empty())
    {
        return -1;
    }
    wait_parent_process_die(parent_process_name);

    const auto db_path = std::filesystem::temp_directory_path() / (parent_process_name + "_sentry_db");

    QApplication app(argc, argv);

    setup_qt_ui();

    auto handler = CrashReportHandler::create(db_path.string(), dsn);
    CrashReporterWindow window(handler);

    window.show();
    return app.exec();
}

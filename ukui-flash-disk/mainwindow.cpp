 /*
 * Copyright (C) 2019 Tianjin KYLIN Information Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/&gt;.
 *
 */
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusObjectPath>
#include <QDBusReply>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QColor>
#include <KWindowEffects>
#include <stdio.h>
#include <string.h>
#include "clickLabel.h"
#include "MacroFile.h"

void frobnitz_result_func(GDrive *source_object,GAsyncResult *res,MainWindow *p_this)
{
    gboolean success =  FALSE;
    GError *err = nullptr;
    success = g_drive_eject_with_operation_finish (source_object, res, &err);

    if (!err)
    {
      findGDriveList()->removeOne(source_object);
      p_this->m_eject = new ejectInterface(p_this,g_drive_get_name(source_object),NORMALDEVICE);
      p_this->m_eject->show();
    }

    else /*if(g_drive_can_stop(source_object) == true)*/
    {
        int volumeNum = g_list_length(g_drive_get_volumes(source_object));

        for(int eachVolume = 0 ; eachVolume < volumeNum ;eachVolume++)
        {
            p_this->flagType = 0;

            if(g_mount_can_unmount(g_volume_get_mount((GVolume *)g_list_nth_data(g_drive_get_volumes(source_object),eachVolume))))
            {
                char *dataPath = g_file_get_path(g_mount_get_root(g_volume_get_mount((GVolume *)g_list_nth_data(g_drive_get_volumes(source_object),eachVolume))));
                QProcess p;
                p.setProgram("pkexec");
                p.setArguments(QStringList()<<"umount"<<QString(dataPath));
                p.start();
                p_this->ifSucess = p.waitForFinished();
                p_this->m_eject = new ejectInterface(p_this,g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(source_object),eachVolume)),DATADEVICE);
                p_this->m_eject->show();
                findGMountList()->removeOne(g_volume_get_mount((GVolume *)g_list_nth_data(g_drive_get_volumes(source_object),eachVolume)));
                findGDriveList()->removeOne(source_object);
            }
        }
    }
}

void frobnitz_result_func_drive(GDrive *source_object,GAsyncResult *res,MainWindow *p_this)
{
    gboolean success =  FALSE;
    GError *err = nullptr;
    success = g_drive_start_finish (source_object, res, &err);
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    findPointMount = false;
    telephoneNum = 0;
    driveVolumeNum = 0;

    const QByteArray idd(THEME_QT_SCHEMA);

    if(QGSettings::isSchemaInstalled(idd))
    {
        qtSettings = new QGSettings(idd);
    }

    const QByteArray id(AUTOLOAD);
    if(QGSettings::isSchemaInstalled(idd))
    {
        ifsettings = new QGSettings(id);
    }

    initThemeMode();

    installEventFilter(this);
    m_dataFlashDisk = FlashDiskData::getInstance();
    ui->setupUi(this);
    //框架的样式设置
    //set the style of the framework
    interfaceHideTime = new QTimer(this);
    initTransparentState();
    ui->centralWidget->setObjectName("centralWidget");
    iconSystray = QIcon::fromTheme("media-removable-symbolic");
    vboxlayout = new QVBoxLayout();
    //hboxlayout = new QHBoxLayout();
#if (QT_VERSION < QT_VERSION_CHECK(5,7,0))
    this->setWindowFlags(Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint);
#else
    this->setWindowFlags(Qt::FramelessWindowHint | Qt::Popup);
#endif
    this->setAttribute(Qt::WA_TranslucentBackground);
    m_systray = new QSystemTrayIcon;
    m_systray->setIcon(iconSystray);
    m_systray->setVisible(true);
    m_systray->setToolTip(tr("usb management tool"));
    //init the screen
    screen = qApp->primaryScreen();
    //underlying code to get the information of the usb device
    getDeviceInfo();
    connect(m_systray, &QSystemTrayIcon::activated, this, &MainWindow::iconActivated);
    connect(this,&MainWindow::convertShowWindow,this,&MainWindow::onConvertShowWindow);
    ui->centralWidget->setLayout(vboxlayout);
    QDBusConnection::sessionBus().connect(QString(), QString("/taskbar/click"), \
                                                  "com.ukui.panel.plugins.taskbar", "sendToUkuiDEApp", this, SLOT(on_clickPanelToHideInterface()));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onRequestSendDesktopNotify(QString message)
{
    QDBusInterface iface("org.freedesktop.Notifications",
                         "/org/freedesktop/Notifications",
                         "org.freedesktop.Notifications",
                         QDBusConnection::sessionBus());
    QList<QVariant> args;
    args<<(tr("ukui-flash-disk"))
       <<((unsigned int) 0)
      <<QString("media-removable-symbolic")
     <<tr("kindly reminder") //显示的是什么类型的信息
    <<message //显示的具体信息
    <<QStringList()
    <<QVariantMap()
    <<(int)-1;
    iface.callWithArgumentList(QDBus::AutoDetect,"Notify",args);
}

void MainWindow::onInsertAbnormalDiskNotify(QString message)
{
    QDBusInterface iface("org.freedesktop.Notifications",
                         "/org/freedesktop/Notifications",
                         "org.freedesktop.Notifications",
                         QDBusConnection::sessionBus());
    QList<QVariant> args;
    args<<(tr("ukui-flash-disk"))
       <<((unsigned int) 0)
      <<QString("media-removable-symbolic")
     <<tr("wrong reminder") //显示的是什么类型的信息
    <<message //显示的具体信息
    <<QStringList()
    <<QVariantMap()
    <<(int)-1;
    iface.callWithArgumentList(QDBus::AutoDetect,"Notify",args);
}

void MainWindow::onNotifyWnd(QObject* obj, QEvent *event)
{
    QString strObjName(obj->metaObject()->className());
    if (strObjName == "QSystemTrayIconSys" || obj == ui->centralWidget) {
        if(obj == ui->centralWidget && event->type() == QEvent::Enter)
        {
            disconnect(interfaceHideTime, SIGNAL(timeout()), this, SLOT(on_Maininterface_hide()));
            ui->centralWidget->show();
        }
        else if(event->type() == QEvent::Leave)
        {
            connect(interfaceHideTime, SIGNAL(timeout()), this, SLOT(on_Maininterface_hide()));
            interfaceHideTime->start(2000);
        }
    }
}

void MainWindow::initThemeMode()
{
    connect(qtSettings,&QGSettings::changed,this,[=](const QString &key)
    {
        auto style = qtSettings->get(key).toString();
        currentThemeMode = qtSettings->get(MODE_QT_KEY).toString();
    });
    currentThemeMode = qtSettings->get(MODE_QT_KEY).toString();
}

void MainWindow::on_clickPanelToHideInterface()
{
    if(!ui->centralWidget->isHidden())
        ui->centralWidget->hide();
}

void MainWindow::getDeviceInfo()
{
    // setting
    FILE *fp = NULL;
    int a = 0;
    char buf[128] = {0};

    fp = fopen("/proc/cmdline","r");
    if (fp) {
        while(fscanf(fp,"%127s",buf) >0 )
        {
            if(strcmp(buf,"live") == 0)
            {
                a++;
            }
        }
        fclose(fp);
    }
    if(a > 0)
    {
        QProcess::startDetached("gsettings set org.ukui.flash-disk.autoload ifautoload false");
    }

//callback function that to monitor the insertion and removal of the underlying equipment
    GVolumeMonitor *g_volume_monitor = g_volume_monitor_get();
    g_signal_connect (g_volume_monitor, "drive-connected", G_CALLBACK (drive_connected_callback), this);
    g_signal_connect (g_volume_monitor, "drive-disconnected", G_CALLBACK (drive_disconnected_callback), this);
    g_signal_connect (g_volume_monitor, "volume-added", G_CALLBACK (volume_added_callback), this);
    g_signal_connect (g_volume_monitor, "volume-removed", G_CALLBACK (volume_removed_callback), this);
    g_signal_connect (g_volume_monitor, "mount-added", G_CALLBACK (mount_added_callback), this);
    g_signal_connect (g_volume_monitor, "mount-removed", G_CALLBACK (mount_removed_callback), this);

    GList *lDrive = NULL, *lVolume = NULL, *lMount = NULL;
//about drive
    GList *current_drive_list = g_volume_monitor_get_connected_drives(g_volume_monitor);
    for (lDrive = current_drive_list; lDrive != NULL; lDrive = lDrive->next) {
        GDrive *gdrive = (GDrive *)lDrive->data;
        char *devPath = g_drive_get_identifier(gdrive,G_DRIVE_IDENTIFIER_KIND_UNIX_DEVICE);
        if (devPath != NULL) {
            FDDriveInfo driveInfo;
            driveInfo.strId = devPath;
            char *strName = g_drive_get_name(gdrive);
            if (strName) {
                driveInfo.strName = strName;
                g_free(strName);
            }
            driveInfo.isCanEject = g_drive_can_eject(gdrive);
            driveInfo.isCanStop = g_drive_can_stop(gdrive);
            driveInfo.isCanStart = g_drive_can_start(gdrive);
            driveInfo.isRemovable = g_drive_is_removable(gdrive);
            if(driveInfo.isCanEject || driveInfo.isCanStop) {
                if(!g_str_has_prefix(devPath,"/dev/sda")) {
                    if(g_str_has_prefix(devPath,"/dev/sr") || g_str_has_prefix(devPath,"/dev/sd")) {
                        GList* gdriveVolumes = g_drive_get_volumes(gdrive);
                        if (gdriveVolumes) {
                            for(lVolume = gdriveVolumes; lVolume != NULL; lVolume = lVolume->next){ //遍历驱动器上的所有卷设备
                                GVolume* volume = (GVolume *)lVolume->data;
                                FDVolumeInfo volumeInfo;
                                bool isValidMount = true;
                                char *volumeId = g_volume_get_uuid(volume);
                                if (volumeId) {
                                    volumeInfo.strId = volumeId;
                                    g_free(volumeId);
                                } else {
                                    continue ;
                                }
                                char *volumeName = g_volume_get_name(volume);
                                if (volumeName) {
                                    volumeInfo.strName = volumeName;
                                    g_free(volumeName);
                                }
                                volumeInfo.isCanMount = g_volume_can_mount(volume);
                                volumeInfo.isCanEject = g_volume_can_eject(volume);
                                volumeInfo.isShouldAutoMount = g_volume_should_automount(volume);
                                GMount* mount = g_volume_get_mount(volume); //get当前卷设备的挂载信息
                                if (mount) {                  //该卷设备已挂载
                                    volumeInfo.mountInfo.isCanEject = g_mount_can_eject(mount);
                                    if (volumeInfo.mountInfo.isCanEject) {
                                        char *mountId = g_mount_get_uuid(mount);
                                        if (mountId) {
                                            volumeInfo.mountInfo.strId = mountId;
                                            g_free(mountId);
                                        }
                                        char *mountName = g_mount_get_name(mount);
                                        if (mountName) {
                                            volumeInfo.mountInfo.strName = mountName;
                                            g_free(mountName);
                                        }
                                        volumeInfo.mountInfo.isCanUnmount = g_mount_can_unmount(mount);
                                        GFile *root = g_mount_get_default_location(mount);
                                        if (root) {
                                            volumeInfo.mountInfo.isNativeDev = g_file_is_native(root);     //判断设备是本地设备or网络设备
                                            char *mountUri = g_file_get_uri(root);           //get挂载点的uri路径
                                            if (mountUri) {
                                                volumeInfo.mountInfo.strUri = mountUri;
                                                if (g_str_has_prefix(mountUri,"file:///data")) {
                                                    isValidMount = false;
                                                } else {
                                                    if (volumeInfo.mountInfo.strId.empty()) {
                                                        volumeInfo.mountInfo.strId = volumeInfo.mountInfo.strUri;
                                                    }
                                                }
                                                g_free(mountUri);
                                            }
                                            char *tooltip = g_file_get_parse_name(root);      //提示，即文件的解释
                                            if (tooltip) {
                                                volumeInfo.mountInfo.strTooltip =tooltip;
                                                g_free(tooltip);
                                            }
                                            g_object_unref(root);
                                        }
                                    }
                                    g_object_unref(mount);
                                } else {
                                    if (volumeInfo.isCanEject) {
                                        if(ifsettings->get(IFAUTOLOAD).toBool()) {
                                            qDebug()<<"mount vol:"<<QString::fromStdString(volumeInfo.strName);
                                            g_volume_mount(volume,
                                                    G_MOUNT_MOUNT_NONE,
                                                    nullptr,
                                                    nullptr,
                                                    nullptr,
                                                    nullptr);
                                        }
                                    }
                                }
                                if (isValidMount) {
                                    driveInfo.listVolumes[volumeInfo.strId] = volumeInfo;
                                }
                            }
                            g_list_free(gdriveVolumes);
                        }
                        m_dataFlashDisk->addDriveInfo(driveInfo);
                    }
                }
            }
            g_free(devPath);
        }
    }
    if (current_drive_list) {
        g_list_free(current_drive_list);
    }
//about volume not associated with a drive
    GList *current_volume_list = g_volume_monitor_get_volumes(g_volume_monitor);
    if (current_volume_list) {
        for (lVolume = current_volume_list; lVolume != NULL; lVolume = lVolume->next) {
            GVolume *volume = (GVolume *)lVolume->data;
            GDrive *gdrive = g_volume_get_drive(volume);
            if (!gdrive) {
                FDVolumeInfo volumeInfo;
                bool isValidMount = true;
                char *devPath = g_volume_get_identifier(volume,G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
                if (devPath) {
                    if (!(g_str_has_prefix(devPath,"/dev/sr") || (g_str_has_prefix(devPath,"/dev/sd") 
                            && !g_str_has_prefix(devPath,"/dev/sda")))) {
                        g_free(devPath);
                        continue;
                    }
                    g_free(devPath);
                }
                char *volumeId = g_volume_get_uuid(volume);
                if (volumeId) {
                    volumeInfo.strId = volumeId;
                    g_free(volumeId);
                } else {
                    continue ;
                }
                char *volumeName = g_volume_get_name(volume);
                if (volumeName) {
                    volumeInfo.strName = volumeName;
                    g_free(volumeName);
                }
                volumeInfo.isCanMount = g_volume_can_mount(volume);
                volumeInfo.isCanEject = g_volume_can_eject(volume);
                volumeInfo.isShouldAutoMount = g_volume_should_automount(volume);
                GMount* mount = g_volume_get_mount(volume); //get当前卷设备的挂载信息
                if (mount) {                  //该卷设备已挂载
                    volumeInfo.mountInfo.isCanEject = g_mount_can_eject(mount);
                    if (volumeInfo.mountInfo.isCanEject) {
                        char *mountId = g_mount_get_uuid(mount);
                        if (mountId) {
                            volumeInfo.mountInfo.strId = mountId;
                            g_free(mountId);
                        }
                        char *mountName = g_mount_get_name(mount);
                        if (mountName) {
                            volumeInfo.mountInfo.strName = mountName;
                            g_free(mountName);
                        }
                        volumeInfo.mountInfo.isCanUnmount = g_mount_can_unmount(mount);
                        GFile *root = g_mount_get_default_location(mount);
                        if (root) {
                            volumeInfo.mountInfo.isNativeDev = g_file_is_native(root);     //判断设备是本地设备or网络设备
                            char *mountUri = g_file_get_uri(root);           //get挂载点的uri路径
                            if (mountUri) {
                                volumeInfo.mountInfo.strUri = mountUri;
                                if (g_str_has_prefix(mountUri,"file:///data")) {
                                    isValidMount = false;
                                } else {
                                    if (volumeInfo.mountInfo.strId.empty()) {
                                        volumeInfo.mountInfo.strId = volumeInfo.mountInfo.strUri;
                                    }
                                }
                                g_free(mountUri);
                            }
                            char *tooltip = g_file_get_parse_name(root);      //提示，即文件的解释
                            if (tooltip) {
                                volumeInfo.mountInfo.strTooltip =tooltip;
                                g_free(tooltip);
                            }
                            g_object_unref(root);
                        }
                    }
                    g_object_unref(mount);
                    qDebug()<<"mounted point:"<<QString::fromStdString(volumeInfo.mountInfo.strName);
                } else {
                    if (volumeInfo.isCanEject) {
                        if(ifsettings->get(IFAUTOLOAD).toBool()) {
                            qDebug()<<"mount1 vol:"<<QString::fromStdString(volumeInfo.strName);
                            g_volume_mount(volume,
                                    G_MOUNT_MOUNT_NONE,
                                    nullptr,
                                    nullptr,
                                    nullptr,
                                    nullptr);
                        }
                    }
                }
                if (isValidMount) {
                    m_dataFlashDisk->addVolumeInfo(volumeInfo);
                }
            } else {
                g_object_unref(gdrive);
            }
        }
        g_list_free(current_volume_list);
    }
//about mount not associated with a volume
    GList *current_mount_list = g_volume_monitor_get_mounts(g_volume_monitor);
    if (current_mount_list) {
        for (lMount = current_mount_list; lMount != NULL; lMount = lMount->next) {
            GMount *gmount = (GMount *)lMount->data;
            GVolume *gvolume = g_mount_get_volume(gmount);
            if (!gvolume) {
                FDMountInfo mountInfo;
                bool isValidMount = true;
                mountInfo.isCanEject = g_mount_can_eject(gmount);
                if (mountInfo.isCanEject) {
                    char *mountId = g_mount_get_uuid(gmount);
                    if (mountId) {
                        mountInfo.strId = mountId;
                        g_free(mountId);
                    }
                    char *mountName = g_mount_get_name(gmount);
                    if (mountName) {
                        mountInfo.strName = mountName;
                        g_free(mountName);
                    }
                    mountInfo.isCanUnmount = g_mount_can_unmount(gmount);
                    GFile *root = g_mount_get_default_location(gmount);
                    if (root) {
                        mountInfo.isNativeDev = g_file_is_native(root);     //判断设备是本地设备or网络设备
                        char *mountUri = g_file_get_uri(root);           //get挂载点的uri路径
                        if (mountUri) {
                            mountInfo.strUri = mountUri;
                            if (g_str_has_prefix(mountUri,"file:///data")) {
                                isValidMount = false;
                            } else {
                                if (mountInfo.strId.empty()) {
                                    mountInfo.strId = mountInfo.strUri;
                                }
                            }
                            g_free(mountUri);
                        }
                        char *tooltip = g_file_get_parse_name(root);      //提示，即文件的解释
                        if (tooltip) {
                            mountInfo.strTooltip =tooltip;
                            g_free(tooltip);
                        }
                        g_object_unref(root);
                    }
                    if (isValidMount) {
                        m_dataFlashDisk->addMountInfo(mountInfo);
                    }
                }
            } else {
                g_object_unref(gvolume);
            }
        }
        g_list_free(current_mount_list);
    }
 //determine the systray icon should be showed  or be hieded
    if(m_dataFlashDisk->getValidInfoCount() >= 1) {
        m_systray->show();
    } else {
        m_systray->hide();
    }
}

void MainWindow::onConvertShowWindow()
{
    insertorclick = true;
    MainWindowShow();
    onRequestSendDesktopNotify(tr("Please do not pull out the USB flash disk when reading or writing"));
}

//the drive-connected callback function the is triggered when the usb device is inseted
void MainWindow::drive_connected_callback(GVolumeMonitor *monitor, GDrive *drive, MainWindow *p_this)
{
    qDebug()<<"drive add";
    if(p_this->ifsettings->get(IFAUTOLOAD).toBool())
    {
        GList *lVolume = NULL;
        FDDriveInfo driveInfo;
        GDrive *gdrive = (GDrive *)drive;
        unsigned uSubVolumeSize = 0;
        char *devPath = g_drive_get_identifier(gdrive,G_DRIVE_IDENTIFIER_KIND_UNIX_DEVICE);
        if (devPath != NULL) {
            driveInfo.strId = devPath;
            char *strName = g_drive_get_name(gdrive);
            if (strName) {
                driveInfo.strName = strName;
                g_free(strName);
            }
            driveInfo.isCanEject = g_drive_can_eject(gdrive);
            driveInfo.isCanStop = g_drive_can_stop(gdrive);
            driveInfo.isCanStart = g_drive_can_start(gdrive);
            driveInfo.isRemovable = g_drive_is_removable(gdrive);
            g_free(devPath);
        }
        lVolume = g_drive_get_volumes(gdrive);
        if (lVolume) {
            uSubVolumeSize = g_list_length(lVolume);
            g_list_free(lVolume);
        }
        if (!driveInfo.strId.empty() && uSubVolumeSize > 0) {
            p_this->m_dataFlashDisk->addDriveInfo(driveInfo);
        } else {
            qDebug()<<"wrong disk has intered";
            p_this->onInsertAbnormalDiskNotify(tr("There is a problem with this device"));
        }
    }

    if(p_this->m_dataFlashDisk->getValidInfoCount() >= 1) {
        p_this->m_systray->show();
    }

    p_this->triggerType = 0;
    p_this->m_dataFlashDisk->OutputInfos();
}

//the drive-disconnected callback function the is triggered when the usb device is pull out
void MainWindow::drive_disconnected_callback (GVolumeMonitor *monitor, GDrive *drive, MainWindow *p_this)
{
    qDebug()<<"drive disconnect";

    FDDriveInfo driveInfo;
    char *devPath = g_drive_get_identifier(drive,G_DRIVE_IDENTIFIER_KIND_UNIX_DEVICE);
    if (devPath != NULL) {
        driveInfo.strId = devPath;
        g_free(devPath);
    }
    p_this->m_dataFlashDisk->removeDriveInfo(driveInfo);
    if(p_this->m_dataFlashDisk->getValidInfoCount() == 0) {
        p_this->ui->centralWidget->hide();
        p_this->m_systray->hide();
    }
    p_this->m_dataFlashDisk->OutputInfos();
}

//when the usb device is identified,we should mount every partition
void MainWindow::volume_added_callback(GVolumeMonitor *monitor, GVolume *volume, MainWindow *p_this)
{
    qDebug()<<"volume add";

    FILE *fp = NULL;
    int a = 0;
    char buf[128] = {0};

    fp = fopen("/proc/cmdline","r");
    if (fp) {
        while(fscanf(fp,"%127s",buf) > 0)
        {
            if(strcmp(buf,"live") == 0)
            {
                a++;
            }
        }
        fclose(fp);
    }
    p_this->ifautoload = p_this->ifsettings->get(IFAUTOLOAD).toBool();
    if(a > 0)
    {
        QProcess::startDetached("gsettings set org.ukui.flash-disk.autoload ifautoload false");
    }
    else
    {
        QProcess::startDetached("gsettings set org.ukui.flash-disk.autoload ifautoload true");
    }
    GDrive* gdrive = g_volume_get_drive(volume);
    if(!gdrive) {
        FDVolumeInfo volumeInfo;
        bool isValidMount = true;
        char *devPath = g_volume_get_identifier(volume,G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
        if (devPath) {
            if (!(g_str_has_prefix(devPath,"/dev/sr") || (g_str_has_prefix(devPath,"/dev/sd")
                    && !g_str_has_prefix(devPath,"/dev/sda")))) {
                g_free(devPath);
                return;
            }
            g_free(devPath);
        }
        char *volumeId = g_volume_get_uuid(volume);
        if (volumeId) {
            volumeInfo.strId = volumeId;
            g_free(volumeId);
        } else {
            return ;
        }
        char *volumeName = g_volume_get_name(volume);
        if (volumeName) {
            volumeInfo.strName = volumeName;
            g_free(volumeName);
        }
        volumeInfo.isCanMount = g_volume_can_mount(volume);
        volumeInfo.isCanEject = g_volume_can_eject(volume);
        volumeInfo.isShouldAutoMount = g_volume_should_automount(volume);
        GMount* mount = g_volume_get_mount(volume); //get当前卷设备的挂载信息
        if (mount) {                  //该卷设备已挂载
            volumeInfo.mountInfo.isCanEject = g_mount_can_eject(mount);
            if (volumeInfo.mountInfo.isCanEject) {
                char *mountId = g_mount_get_uuid(mount);
                if (mountId) {
                    volumeInfo.mountInfo.strId = mountId;
                    g_free(mountId);
                }
                char *mountName = g_mount_get_name(mount);
                if (mountName) {
                    volumeInfo.mountInfo.strName = mountName;
                    g_free(mountName);
                }
                volumeInfo.mountInfo.isCanUnmount = g_mount_can_unmount(mount);
                GFile *root = g_mount_get_default_location(mount);
                if (root) {
                    volumeInfo.mountInfo.isNativeDev = g_file_is_native(root);     //判断设备是本地设备or网络设备
                    char *mountUri = g_file_get_uri(root);           //get挂载点的uri路径
                    if (mountUri) {
                        volumeInfo.mountInfo.strUri = mountUri;
                        if (g_str_has_prefix(mountUri,"file:///data")) {
                            isValidMount = false;
                        } else {
                            if (volumeInfo.mountInfo.strId.empty()) {
                                volumeInfo.mountInfo.strId = volumeInfo.mountInfo.strUri;
                            }
                        }
                        g_free(mountUri);
                    }
                    char *tooltip = g_file_get_parse_name(root);      //提示，即文件的解释
                    if (tooltip) {
                        volumeInfo.mountInfo.strTooltip =tooltip;
                        g_free(tooltip);
                    }
                    g_object_unref(root);
                }
            }
            g_object_unref(mount);
        } else {
            if (volumeInfo.isCanEject) {
                if(p_this->ifsettings->get(IFAUTOLOAD).toBool()) {
                    qDebug()<<"mount1 vol:"<<QString::fromStdString(volumeInfo.strName);
                    g_volume_mount(volume,
                                G_MOUNT_MOUNT_NONE,
                                nullptr,
                                nullptr,
                                GAsyncReadyCallback(frobnitz_result_func_volume),
                                p_this);
                }
            }
        }
        if (isValidMount) {
            p_this->m_dataFlashDisk->addVolumeInfo(volumeInfo);
        }
    } else {
        char *devPath = g_drive_get_identifier(gdrive,G_DRIVE_IDENTIFIER_KIND_UNIX_DEVICE);
        if (devPath != NULL) {
            FDDriveInfo driveInfo;
            FDVolumeInfo volumeInfo;
            driveInfo.strId = devPath;
            char *strName = g_drive_get_name(gdrive);
            if (strName) {
                driveInfo.strName = strName;
                g_free(strName);
            }
            driveInfo.isCanEject = g_drive_can_eject(gdrive);
            driveInfo.isCanStop = g_drive_can_stop(gdrive);
            driveInfo.isCanStart = g_drive_can_start(gdrive);
            driveInfo.isRemovable = g_drive_is_removable(gdrive);

            if(driveInfo.isCanEject || driveInfo.isCanStop) {
                if(!g_str_has_prefix(devPath,"/dev/sda")) {
                    if(g_str_has_prefix(devPath,"/dev/sr") || g_str_has_prefix(devPath,"/dev/sd")) {
                        char *volumeId = g_volume_get_uuid(volume);
                        if (volumeId) {
                            volumeInfo.strId = volumeId;
                            g_free(volumeId);
                            char *volumeName = g_volume_get_name(volume);
                            if (volumeName) {
                                volumeInfo.strName = volumeName;
                                g_free(volumeName);
                            }
                            volumeInfo.isCanMount = g_volume_can_mount(volume);
                            volumeInfo.isCanEject = g_volume_can_eject(volume);
                            volumeInfo.isShouldAutoMount = g_volume_should_automount(volume);
                            GMount* mount = g_volume_get_mount(volume); //get当前卷设备的挂载信息
                            if (mount) {                  //该卷设备已挂载
                                volumeInfo.mountInfo.isCanEject = g_mount_can_eject(mount);
                                if (volumeInfo.mountInfo.isCanEject) {
                                    char *mountId = g_mount_get_uuid(mount);
                                    if (mountId) {
                                        volumeInfo.mountInfo.strId = mountId;
                                        g_free(mountId);
                                    }
                                    char *mountName = g_mount_get_name(mount);
                                    if (mountName) {
                                        volumeInfo.mountInfo.strName = mountName;
                                        g_free(mountName);
                                    }
                                    volumeInfo.mountInfo.isCanUnmount = g_mount_can_unmount(mount);
                                    GFile *root = g_mount_get_default_location(mount);
                                    if (root) {
                                        volumeInfo.mountInfo.isNativeDev = g_file_is_native(root);     //判断设备是本地设备or网络设备
                                        char *mountUri = g_file_get_uri(root);           //get挂载点的uri路径
                                        if (mountUri) {
                                            volumeInfo.mountInfo.strUri = mountUri;
                                            if (!g_str_has_prefix(mountUri,"file:///data")) {
                                                if (volumeInfo.mountInfo.strId.empty()) {
                                                    volumeInfo.mountInfo.strId = volumeInfo.mountInfo.strUri;
                                                }
                                            }
                                            g_free(mountUri);
                                        }
                                        char *tooltip = g_file_get_parse_name(root);      //提示，即文件的解释
                                        if (tooltip) {
                                            volumeInfo.mountInfo.strTooltip =tooltip;
                                            g_free(tooltip);
                                        }
                                        g_object_unref(root);
                                    }
                                }
                                g_object_unref(mount);
                            } else {
                                if (volumeInfo.isCanEject) {
                                    if(p_this->ifsettings->get(IFAUTOLOAD).toBool()) {
                                        qDebug()<<"mount vol:"<<QString::fromStdString(volumeInfo.strName);
                                        g_volume_mount(volume,
                                                    G_MOUNT_MOUNT_NONE,
                                                    nullptr,
                                                    nullptr,
                                                    GAsyncReadyCallback(frobnitz_result_func_volume),
                                                    p_this);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            g_free(devPath);
            p_this->m_dataFlashDisk->addVolumeInfoWithDrive(driveInfo, volumeInfo);
        }
        g_object_unref(gdrive);
    }
    if(p_this->m_dataFlashDisk->getValidInfoCount() > 0)
    {
        p_this->m_systray->show();
    }
    p_this->m_dataFlashDisk->OutputInfos();
}

//when the U disk is pull out we should reduce all its partitions
void MainWindow::volume_removed_callback(GVolumeMonitor *monitor, GVolume *volume, MainWindow *p_this)
{
    qDebug()<<"volume removed";
    FDVolumeInfo volumeInfo;
    char *volumeId = g_volume_get_uuid(volume);
    if (volumeId) {
        volumeInfo.strId = volumeId;
        g_free(volumeId);
    }
    p_this->m_dataFlashDisk->removeVolumeInfo(volumeInfo);
    if(p_this->m_dataFlashDisk->getValidInfoCount() == 0) {
        p_this->m_systray->hide();
    }
    p_this->m_dataFlashDisk->OutputInfos();
}

//when the volumes were mounted we add its mounts number
void MainWindow::mount_added_callback(GVolumeMonitor *monitor, GMount *mount, MainWindow *p_this)
{
    qDebug()<<"mount add";

    GDrive* gdrive = g_mount_get_drive(mount);
    GVolume* gvolume = g_mount_get_volume(mount);
    string strVolumePath = "";
    FDDriveInfo driveInfo;
    FDVolumeInfo volumeInfo;
    FDMountInfo mountInfo;
    bool isValidMount = true;
    if (gdrive) {
        char *devPath = g_drive_get_identifier(gdrive,G_DRIVE_IDENTIFIER_KIND_UNIX_DEVICE);
        if (devPath != NULL) {
            driveInfo.strId = devPath;
            char *strName = g_drive_get_name(gdrive);
            if (strName) {
                driveInfo.strName = strName;
                g_free(strName);
            }
            driveInfo.isCanEject = g_drive_can_eject(gdrive);
            driveInfo.isCanStop = g_drive_can_stop(gdrive);
            driveInfo.isCanStart = g_drive_can_start(gdrive);
            driveInfo.isRemovable = g_drive_is_removable(gdrive);
            g_free(devPath);
        }
        g_object_unref(gdrive);
    }
    if (gvolume) {
        char *volumeId = g_volume_get_uuid(gvolume);
        char *devVolumePath = g_volume_get_identifier(gvolume,G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
        if (devVolumePath) {
            strVolumePath = devVolumePath;
            g_free(devVolumePath);
        }
        if (volumeId) {
            volumeInfo.strId = volumeId;
            g_free(volumeId);
            char *volumeName = g_volume_get_name(gvolume);
            if (volumeName) {
                volumeInfo.strName = volumeName;
                g_free(volumeName);
            }

            volumeInfo.isCanMount = g_volume_can_mount(gvolume);
            volumeInfo.isCanEject = g_volume_can_eject(gvolume);
            volumeInfo.isShouldAutoMount = g_volume_should_automount(gvolume);
        }
        g_object_unref(gvolume);
    }
    mountInfo.isCanEject = g_mount_can_eject(mount);
    char *mountId = g_mount_get_uuid(mount);
    if (mountId) {
        mountInfo.strId = mountId;
        g_free(mountId);
    }
    char *mountName = g_mount_get_name(mount);
    if (mountName) {
        mountInfo.strName = mountName;
        g_free(mountName);
    }
    mountInfo.isCanUnmount = g_mount_can_unmount(mount);
    GFile *root = g_mount_get_default_location(mount);
    if (root) {
        mountInfo.isNativeDev = g_file_is_native(root);     //判断设备是本地设备or网络设备
        char *mountUri = g_file_get_uri(root);           //get挂载点的uri路径
        if (mountUri) {
            mountInfo.strUri = mountUri;
            if (g_str_has_prefix(mountUri,"file:///data")) {
                isValidMount = false;
            } else {
                if (mountInfo.strId.empty()) {
                    mountInfo.strId = mountInfo.strUri;
                }
            }
            g_free(mountUri);
        }
        char *tooltip = g_file_get_parse_name(root);      //提示，即文件的解释
        if (tooltip) {
            mountInfo.strTooltip = tooltip;
            g_free(tooltip);
        }
        g_object_unref(root);
    }

    if (driveInfo.strId.empty()) {
       Q_EMIT p_this->telephoneMount();
    }

    if(isValidMount && !g_str_has_prefix(strVolumePath.c_str(),"/dev/sda") && (mountInfo.isCanEject || g_str_has_prefix(strVolumePath.c_str(),"/dev/bus")
            || g_str_has_prefix(strVolumePath.c_str(),"/dev/sr"))) {
        qDebug() << "real mount loaded";
        if (!driveInfo.strId.empty()) {
            if (!volumeInfo.strId.empty()) {
                volumeInfo.mountInfo = mountInfo;
                p_this->m_dataFlashDisk->addVolumeInfoWithDrive(driveInfo, volumeInfo);
            } else {
                p_this->m_dataFlashDisk->addMountInfo(mountInfo);
            }
        } else if (!volumeInfo.strId.empty()) {
            volumeInfo.mountInfo = mountInfo;
            p_this->m_dataFlashDisk->addVolumeInfo(volumeInfo);
        } else {
            p_this->m_dataFlashDisk->addMountInfo(mountInfo);
        }
    }
    else
    {
        qDebug()<<"不符合过滤条件的设备已被挂载";
    }

    if(p_this->m_dataFlashDisk->getValidInfoCount() >= 1)
    {
        p_this->m_systray->show();
    }
    p_this->m_dataFlashDisk->OutputInfos();
}

//when the mountes were uninstalled we reduce mounts number
void MainWindow::mount_removed_callback(GVolumeMonitor *monitor, GMount *mount, MainWindow *p_this)
{
    qDebug()<<mount<<"mount remove";
    FDMountInfo mountInfo;
    mountInfo.isCanEject = g_mount_can_eject(mount);
    char *mountId = g_mount_get_uuid(mount);
    if (mountId) {
        mountInfo.strId = mountId;
        g_free(mountId);
    }
    char *mountName = g_mount_get_name(mount);
    if (mountName) {
        mountInfo.strName = mountName;
        g_free(mountName);
    }
    mountInfo.isCanUnmount = g_mount_can_unmount(mount);
    GFile *root = g_mount_get_default_location(mount);
    if (root) {
        mountInfo.isNativeDev = g_file_is_native(root);     //判断设备是本地设备or网络设备
        char *mountUri = g_file_get_uri(root);           //get挂载点的uri路径
        if (mountUri) {
            mountInfo.strUri = mountUri;
            g_free(mountUri);
            if (mountInfo.strId.empty()) {
                mountInfo.strId = mountInfo.strUri;
            }
        }
        char *tooltip = g_file_get_parse_name(root);      //提示，即文件的解释
        if (tooltip) {
            mountInfo.strTooltip = tooltip;
            g_free(tooltip);
        }
        g_object_unref(root);
    }

    p_this->m_dataFlashDisk->removeMountInfo(mountInfo);

    if(p_this->m_dataFlashDisk->getValidInfoCount() == 0)
    {
        p_this->m_systray->hide();
    }
    p_this->m_dataFlashDisk->OutputInfos();
}

//it stands that when you insert a usb device when all the  U disk partitions
void MainWindow::frobnitz_result_func_volume(GVolume *source_object,GAsyncResult *res,MainWindow *p_this)
{
    gboolean success =  FALSE;
    GError *err = nullptr;
    bool bMountSuccess = false;
    success = g_volume_mount_finish (source_object, res, &err);
    if(!err)
    {
        GMount* gmount = g_volume_get_mount(source_object);
        GDrive* gdrive = g_volume_get_drive(source_object);
        FDDriveInfo driveInfo;
        FDVolumeInfo volumeInfo;
        FDMountInfo mountInfo;
        if (gdrive) {
            char *devPath = g_drive_get_identifier(gdrive,G_DRIVE_IDENTIFIER_KIND_UNIX_DEVICE);
            if (devPath != NULL) {
                driveInfo.strId = devPath;
                char *strName = g_drive_get_name(gdrive);
                if (strName) {
                    driveInfo.strName = strName;
                    g_free(strName);
                }
                driveInfo.isCanEject = g_drive_can_eject(gdrive);
                driveInfo.isCanStop = g_drive_can_stop(gdrive);
                driveInfo.isCanStart = g_drive_can_start(gdrive);
                driveInfo.isRemovable = g_drive_is_removable(gdrive);
                g_free(devPath);
            }
            g_object_unref(gdrive);
        }
        char *volumeId = g_volume_get_uuid(source_object);
        if (volumeId) {
            volumeInfo.strId = volumeId;
            g_free(volumeId);
            char *volumeName = g_volume_get_name(source_object);
            if (volumeName) {
                volumeInfo.strName = volumeName;
                g_free(volumeName);
            }

            volumeInfo.isCanMount = g_volume_can_mount(source_object);
            volumeInfo.isCanEject = g_volume_can_eject(source_object);
            volumeInfo.isShouldAutoMount = g_volume_should_automount(source_object);
        }
        if (gmount) {
            bMountSuccess = true;
            mountInfo.isCanEject = g_mount_can_eject(gmount);
            char *mountId = g_mount_get_uuid(gmount);
            if (mountId) {
                mountInfo.strId = mountId;
                g_free(mountId);
            }
            char *mountName = g_mount_get_name(gmount);
            if (mountName) {
                mountInfo.strName = mountName;
                g_free(mountName);
            }
            mountInfo.isCanUnmount = g_mount_can_unmount(gmount);
            GFile *root = g_mount_get_default_location(gmount);
            if (root) {
                mountInfo.isNativeDev = g_file_is_native(root);     //判断设备是本地设备or网络设备
                char *mountUri = g_file_get_uri(root);           //get挂载点的uri路径
                if (mountUri) {
                    mountInfo.strUri = mountUri;
                    if (g_str_has_prefix(mountUri,"file:///data")) {
                        bMountSuccess = false;
                    }else {
                        if (mountInfo.strId.empty()) {
                            mountInfo.strId = mountInfo.strUri;
                        }
                    }
                    g_free(mountUri);
                }
                char *tooltip = g_file_get_parse_name(root);      //提示，即文件的解释
                if (tooltip) {
                    mountInfo.strTooltip = tooltip;
                    g_free(tooltip);
                }
                g_object_unref(root);
            }
            g_object_unref(gmount);
        }        
        if (bMountSuccess) {
            if (!driveInfo.strId.empty()) {
                if (!volumeInfo.strId.empty()) {
                    volumeInfo.mountInfo = mountInfo;
                    p_this->m_dataFlashDisk->addVolumeInfoWithDrive(driveInfo, volumeInfo);
                } else {
                    p_this->m_dataFlashDisk->addMountInfo(mountInfo);
                }
            } else if (!volumeInfo.strId.empty()) {
                volumeInfo.mountInfo = mountInfo;
                p_this->m_dataFlashDisk->addVolumeInfo(volumeInfo);
            } else {
                p_this->m_dataFlashDisk->addMountInfo(mountInfo);
            }
            qDebug()<<"sig has emited";
            Q_EMIT p_this->convertShowWindow();     //emit a signal to trigger the MainMainShow slot
        } 
    }
    else
    {
        qDebug()<<"sorry mount failed";
    }
}

//here we begin painting the main interface
void MainWindow::iconActivated(QSystemTrayIcon::ActivationReason reason)
{
    insertorclick = false;
    triggerType = 1;  //It represents how we open the interface

    if (ui->centralWidget && !ui->centralWidget->isHidden()) {
        ui->centralWidget->hide();
        return ;
    }

    tmpPath = "/tmp/ukui-flash-disk-" + QDir::home().dirName();
    QString cmd = "gio list computer:/// >" + tmpPath;
    system(cmd.toUtf8().data());
    ifgetPinitMount();
    if(findPointMount == true)
    {
        qDebug()<<"gparted has started";
    }

    qDebug()<<"findPointMount"<<findPointMount;
    if(findPointMount == false)
    {
        MainWindowShow();
    }
    else
    {
        qDebug()<<"show gparted window";
        GpartedStartedWindowShow();
    }
}

void MainWindow::hideEvent(QHideEvent event)
{
    // delete open_widget;
}


/*
 * newarea use all the information of the U disk to paint the main interface and add line
*/
void MainWindow::newarea(int No,
                         GDrive *Drive,
                         GVolume *Volume,
                         QString Drivename,
                         QString nameDis1,
                         QString nameDis2,
                         QString nameDis3,
                         QString nameDis4,
                         qlonglong capacityDis1,
                         qlonglong capacityDis2,
                         qlonglong capacityDis3,
                         qlonglong capacityDis4,
                         QString pathDis1,
                         QString pathDis2,
                         QString pathDis3,
                         QString pathDis4,
                         int linestatus)
{
    open_widget = new QClickWidget(ui->centralWidget,No,Drive,Volume,Drivename,nameDis1,nameDis2,nameDis3,nameDis4,
                                   capacityDis1,capacityDis2,capacityDis3,capacityDis4,
                                   pathDis1,pathDis2,pathDis3,pathDis4);
    connect(open_widget,&QClickWidget::clickedConvert,this,[=]()
    {
        ui->centralWidget->hide();
    });
    connect(open_widget,&QClickWidget::noDeviceSig,this,[=]()
    {
        m_systray->hide();
    });


    line = new QWidget;
    line->setFixedHeight(1);
    line->setObjectName("lineWidget");
    if(currentThemeMode == "ukui-dark" || currentThemeMode == "ukui-black" || currentThemeMode == "ukui-default")
    {
        line->setStyleSheet("background-color:rgba(255,255,255,0.2);");
    }
    else
    {
        line->setStyleSheet("background-color:rgba(0,0,0,0.2);");
    }

    //when the drive is only or the drive is the first one,we make linestatus become  1
    line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    line->setFixedSize(276,1);
    if (linestatus == 2)
    {
        this->vboxlayout->addWidget(line);
    }

    this->vboxlayout->addWidget(open_widget);
    vboxlayout->setContentsMargins(2,8,2,8);

    if (linestatus == 0)
    {
        this->vboxlayout->addWidget(line);
    }
}

void MainWindow::moveBottomRight()
{
////////////////////////////////////////get the loacation of the mouse
    /*QPoint globalCursorPos = QCursor::pos();

    QRect availableGeometry = qApp->primaryScreen()->availableGeometry();
    QRect screenGeometry = qApp->primaryScreen()->geometry();

    QDesktopWidget* pDesktopWidget = QApplication::desktop();

    QRect screenRect = pDesktopWidget->screenGeometry();//area of the screen

    if (screenRect.height() != availableGeometry.height())
    {
        this->move(globalCursorPos.x()-125, availableGeometry.height() - this->height());
    }
    else
    {
        this->move(globalCursorPos.x()-125, availableGeometry.height() - this->height() - 40);
    }*/
    //////////////////////////////////////origin code base on the location of the mouse
//    int screenNum = QGuiApplication::screens().count();
//    qDebug()<<"ScreenNum:"<<"-------------"<<screenNum;
//    int panelHeight = getPanelHeight("PanelHeight");
//    int position =0;
//    position = getPanelPosition("PanelPosion");
//    int screen = 0;
//    QRect rect;
//    int localX ,availableWidth,totalWidth;
//    int localY,availableHeight,totalHeight;

//    qDebug() << "任务栏位置"<< position;
//    if (screenNum > 1)
//    {
//        if (position == rightPosition)                                  //on the right
//        {
//            screen = screenNum - 1;

//            //Available screen width and height
//            availableWidth =QGuiApplication::screens().at(screen)->geometry().x() +  QGuiApplication::screens().at(screen)->size().width()-panelHeight;
//            qDebug()<<QGuiApplication::screens().at(screen)->geometry().x()<<"QGuiApplication::screens().at(screen)->geometry().x()";
//            qDebug()<<QGuiApplication::screens().at(screen)->size().width()<<"QGuiApplication::screens().at(screen)->size().width()";
//            availableHeight = QGuiApplication::screens().at(screen)->availableGeometry().height();

//            //total width
//            totalWidth =  QGuiApplication::screens().at(0)->size().width() + QGuiApplication::screens().at(screen)->size().width();
//            totalHeight = QGuiApplication::screens().at(screen)->size().height();
//            this->move(totalWidth,totalHeight);
//        }
//        else if(position  ==downPosition || upPosition ==1)                  //above or bellow
//        {
//            availableHeight = QGuiApplication::screens().at(0)->size().height() - panelHeight;
//            availableWidth = QGuiApplication::screens().at(0)->size().width();
//            totalHeight = QGuiApplication::screens().at(0)->size().height();
//            totalWidth = QGuiApplication::screens().at(0)->size().width();
//        }
//        else
//        {
//            availableHeight = QGuiApplication::screens().at(0)->availableGeometry().height();
//            availableWidth = QGuiApplication::screens().at(0)->availableGeometry().width();
//            totalHeight = QGuiApplication::screens().at(0)->size().height();
//            totalWidth = QGuiApplication::screens().at(0)->size().width();
//        }
//    }

//    else
//    {
//        availableHeight = QGuiApplication::screens().at(0)->availableGeometry().height();
//        availableWidth = QGuiApplication::screens().at(0)->availableGeometry().width();
//        totalHeight = QGuiApplication::screens().at(0)->size().height();
//        totalWidth = QGuiApplication::screens().at(0)->size().width();

//        //show the location of the systemtray
//        rect = m_systray->geometry();
//        localX = rect.x() - (this->width()/2 - rect.size().width()/2) ;
//        localY = availableHeight - this->height();

//        //modify location
//        if (position == downPosition)
//        { //下
//            if (availableWidth - rect.x() - rect.width()/2 < this->width() / 2)
//                this->setGeometry(availableWidth-this->width(),availableHeight-this->height()-DistanceToPanel,this->width(),this->height());
//            else
//                this->setGeometry(localX,availableHeight-this->height()-DistanceToPanel,this->width(),this->height());
//        }
//        else if (position == upPosition)
//        { //上
//            if (availableWidth - rect.x() - rect.width()/2 < this->width() / 2)
//                this->setGeometry(availableWidth-this->width(),totalHeight-availableHeight+DistanceToPanel,this->width(),this->height());
//            else
//                this->setGeometry(localX,totalHeight-availableHeight+DistanceToPanel,this->width(),this->height());
//        }
//        else if (position == leftPosition)
//        {
//            if (availableHeight - rect.y() - rect.height()/2 > this->height() /2)
//                this->setGeometry(panelHeight + DistanceToPanel,rect.y() + (rect.width() /2) -(this->height()/2) ,this->width(),this->height());
//            else
//                this->setGeometry(panelHeight+DistanceToPanel,localY,this->width(),this->height());//左
//        }
//        else if (position == rightPosition)
//        {
//            localX = availableWidth - this->width();
//            if (availableHeight - rect.y() - rect.height()/2 > this->height() /2)
//                this->setGeometry(availableWidth - this->width() -DistanceToPanel,rect.y() + (rect.width() /2) -(this->height()/2),this->width(),this->height());
//            else
//                this->setGeometry(localX-DistanceToPanel,localY,this->width(),this->height());
//        }
//    }
    QRect availableGeometry = qApp->primaryScreen()->availableGeometry();
    QRect screenGeometry = qApp->primaryScreen()->geometry();

    QDesktopWidget* desktopWidget = QApplication::desktop();
    QRect deskMainRect = desktopWidget->availableGeometry(0);//获取可用桌面大小
    QRect screenMainRect = desktopWidget->screenGeometry(0);//获取设备屏幕大小
    QRect deskDupRect = desktopWidget->availableGeometry(1);//获取可用桌面大小
    QRect screenDupRect = desktopWidget->screenGeometry(1);//获取设备屏幕大小

    int PanelPosition = getPanelPosition("position");     //n
    int PanelHeight = getPanelHeight("height");                   //m
//    int d = 2; //窗口边沿到任务栏距离

    if (screenGeometry.width() == availableGeometry.width() && screenGeometry.height() == availableGeometry.height()) {
        if (PanelPosition == 0) {
            //任务栏在下侧
            this->move(availableGeometry.x() + availableGeometry.width() - this->width(), screenMainRect.y() + availableGeometry.height() - this->height() - PanelHeight - DistanceToPanel);
        } else if(PanelPosition == 1) {
            //任务栏在上侧
            this->move(availableGeometry.x() + availableGeometry.width() - this->width(), screenMainRect.y() + screenGeometry.height() - availableGeometry.height() + PanelHeight + DistanceToPanel);
        } else if (PanelPosition == 2) {
            //任务栏在左侧
            if (screenGeometry.x() == 0) {//主屏在左侧
                this->move(PanelHeight + DistanceToPanel, screenMainRect.y() + screenMainRect.height() - this->height());
            } else {//主屏在右侧
                this->move(screenMainRect.x() + PanelHeight + DistanceToPanel, screenMainRect.y() + screenMainRect.height() - this->height());
            }
        } else if (PanelPosition == 3) {
            //任务栏在右侧
            if (screenGeometry.x() == 0) {//主屏在左侧
                this->move(screenMainRect.width() - this->width() - PanelHeight - DistanceToPanel, screenMainRect.y() + screenMainRect.height() - this->height());
            } else {//主屏在右侧
                this->move(screenMainRect.x() + screenMainRect.width() - this->width() - PanelHeight - DistanceToPanel, screenMainRect.y() + screenMainRect.height() - this->height());
            }
        }
    } else if(screenGeometry.width() == availableGeometry.width() ) {
        if (m_systray->geometry().y() > availableGeometry.height()/2) {
            //任务栏在下侧
            this->move(availableGeometry.x() + availableGeometry.width() - this->width(), screenMainRect.y() + availableGeometry.height() - this->height() - DistanceToPanel);
        } else {
            //任务栏在上侧
            this->move(availableGeometry.x() + availableGeometry.width() - this->width(), screenMainRect.y() + screenGeometry.height() - availableGeometry.height() + DistanceToPanel);
        }
    } else if (screenGeometry.height() == availableGeometry.height()) {
        if (m_systray->geometry().x() > availableGeometry.width()/2) {
            //任务栏在右侧
            this->move(availableGeometry.x() + availableGeometry.width() - this->width() - DistanceToPanel, screenMainRect.y() + screenGeometry.height() - this->height());
        } else {
            //任务栏在左侧
            this->move(screenGeometry.width() - availableGeometry.width() + DistanceToPanel, screenMainRect.y() + screenGeometry.height() - this->height());
        }
    }
}

/*
    use dbus to get the location of the panel
*/
int MainWindow::getPanelPosition(QString str)
{
    QDBusInterface interface( "com.ukui.panel.desktop",
                              "/",
                              "com.ukui.panel.desktop",
                              QDBusConnection::sessionBus() );
    QDBusReply<int> reply = interface.call("GetPanelPosition", str);

    return reply;
}

/*
    use the dbus to get the height of the panel
*/
int MainWindow::getPanelHeight(QString str)
{
    QDBusInterface interface( "com.ukui.panel.desktop",
                              "/",
                              "com.ukui.panel.desktop",
                              QDBusConnection::sessionBus() );
    QDBusReply<int> reply = interface.call("GetPanelSize", str);
    return reply;
}


void MainWindow::resizeEvent(QResizeEvent *event)
{
}

void MainWindow::moveBottomDirect(GDrive *drive)
{
//    ejectInterface *ForEject = new ejectInterface(nullptr,g_drive_get_name(drive));
//    int screenNum = QGuiApplication::screens().count();
//    int panelHeight = getPanelHeight("PanelHeight");
//    int position =0;
//    position = getPanelPosition("PanelPosion");
//    int screen = 0;
//    QRect rect;
//    int localX ,availableWidth,totalWidth;
//    int localY,availableHeight,totalHeight;

//    qDebug() << "任务栏位置"<< position;
//    if (screenNum > 1)
//    {
//        if (position == rightPosition)                                  //on the right
//        {
//            screen = screenNum - 1;

//            //Available screen width and height
//            availableWidth =QGuiApplication::screens().at(screen)->geometry().x() +  QGuiApplication::screens().at(screen)->size().width()-panelHeight;
//            availableHeight = QGuiApplication::screens().at(screen)->availableGeometry().height();

//            //total width
//            totalWidth =  QGuiApplication::screens().at(0)->size().width() + QGuiApplication::screens().at(screen)->size().width();
//            totalHeight = QGuiApplication::screens().at(screen)->size().height();
//        }
//        else if(position  ==downPosition || position ==upPosition)                  //above or bellow
//        {
//            availableHeight = QGuiApplication::screens().at(0)->size().height() - panelHeight;
//            availableWidth = QGuiApplication::screens().at(0)->size().width();
//            totalHeight = QGuiApplication::screens().at(0)->size().height();
//            totalWidth = QGuiApplication::screens().at(0)->size().width();
//        }
//        else
//        {
//            availableHeight = QGuiApplication::screens().at(0)->availableGeometry().height();
//            availableWidth = QGuiApplication::screens().at(0)->availableGeometry().width();
//            totalHeight = QGuiApplication::screens().at(0)->size().height();
//            totalWidth = QGuiApplication::screens().at(0)->size().width();
//        }
//    }

//    else
//    {
//        availableHeight = QGuiApplication::screens().at(0)->availableGeometry().height();
//        availableWidth = QGuiApplication::screens().at(0)->availableGeometry().width();
//        totalHeight = QGuiApplication::screens().at(0)->size().height();
//        totalWidth = QGuiApplication::screens().at(0)->size().width();
//    }
//    //show the location of the systemtray
//    rect = m_systray->geometry();
//    localX = rect.x() - (ForEject->width()/2 - rect.size().width()/2) ;
//    localY = availableHeight - ForEject->height();
//    //modify location
//    if (position == downPosition)
//    { //下
//        if (availableWidth - rect.x() - rect.width()/2 < ForEject->width() / 2)
//            ForEject->setGeometry(availableWidth-ForEject->width(),availableHeight-ForEject->height()-DistanceToPanel,ForEject->width(),ForEject->height());
//        else
//            ForEject->setGeometry(localX-16,availableHeight-ForEject->height()-DistanceToPanel,ForEject->width(),ForEject->height());
//    }
//    else if (position == upPosition)
//    { //上
//        if (availableWidth - rect.x() - rect.width()/2 < ForEject->width() / 2)
//            ForEject->setGeometry(availableWidth-ForEject->width(),totalHeight-availableHeight+DistanceToPanel,ForEject->width(),ForEject->height());
//        else
//            ForEject->setGeometry(localX-16,totalHeight-availableHeight+DistanceToPanel,ForEject->width(),ForEject->height());
//    }
//    else if (position == leftPosition)
//    {
//        if (availableHeight - rect.y() - rect.height()/2 > ForEject->height() /2)
//            ForEject->setGeometry(panelHeight + DistanceToPanel,rect.y() + (rect.width() /2) -(ForEject->height()/2) ,ForEject->width(),ForEject->height());
//        else
//            ForEject->setGeometry(panelHeight+DistanceToPanel,localY,ForEject->width(),ForEject->height());//左
//    }
//    else if (position == rightPosition)
//    {
//        localX = availableWidth - ForEject->width();
//        if (availableHeight - rect.y() - rect.height()/2 > ForEject->height() /2)
//        {
//            ForEject->setGeometry(availableWidth - ForEject->width() -DistanceToPanel,rect.y() + (rect.height() /2) -(ForEject->height()/2),ForEject->width(),ForEject->height());
//        }
//        else
//            ForEject->setGeometry(localX-DistanceToPanel,localY,ForEject->width(),ForEject->height());
//    }
//    ForEject->show();
}

void MainWindow::MainWindowShow()
{
    this->getTransparentData();
    m_dataFlashDisk->getValidInfoCount();
    m_dataFlashDisk->OutputInfos();
    QString strTrans;
    strTrans =  QString::number(m_transparency, 10, 2);
#if (QT_VERSION < QT_VERSION_CHECK(5,7,0))
    QString convertStyle = "#centralWidget{background:rgba(19,19,20,0.95);}";
#else
//    QString convertStyle = "#centralWidget{background:rgba(19,19,20," + strTrans + ");}";
#endif

    if(ui->centralWidget != NULL)
    {
        //cancel the connection between the timeout signal and main interface hiding
        if(insertorclick == false)
            disconnect(interfaceHideTime, SIGNAL(timeout()), this, SLOT(on_Maininterface_hide()));
        else
        {
            connect(interfaceHideTime, SIGNAL(timeout()), this, SLOT(on_Maininterface_hide()));
            interfaceHideTime->start(5000);
        }
    }
    num = 0;
    QList<QClickWidget *> listMainWindow = ui->centralWidget->findChildren<QClickWidget *>();
    for(QClickWidget *listItem:listMainWindow)
    {
        listItem->deleteLater();
    }

    QList<QWidget *> listLine = ui->centralWidget->findChildren<QWidget *>();
    for(QWidget *listItem:listLine)
    {
        if(listItem->objectName() == "lineWidget") {
            listItem->deleteLater();
        }
    }
    if (m_dataFlashDisk->getValidInfoCount() > 0) {
        hign = m_dataFlashDisk->getValidInfoCount() * 95;
        ui->centralWidget->setFixedSize(280, hign);
    } else {
        hign = 0;
        ui->centralWidget->setFixedSize(0, 0);
    }
    //Convenient interface layout for all drives
    for(auto cacheDrive : *findGDriveList())
    {
        GVolume *g_volume = (GVolume *)g_list_nth(g_drive_get_volumes(cacheDrive),0);
        char *devPath = g_volume_get_identifier(g_volume,G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
        int singleSignal = 0;
        int cdSignal = 0;
        listVolumes = g_drive_get_volumes(cacheDrive);
        hign = findGMountList()->size() *40 + findGDriveList()->size() *55;
        for(vList = listVolumes; vList != NULL; vList = vList->next)
        {
            volume = (GVolume *)vList->data;
            if(g_volume_get_mount(volume) != NULL)
            {
                root = g_mount_get_default_location(g_volume_get_mount(volume));
                mount_uri = g_file_get_uri(root);
                if(g_str_has_prefix(mount_uri,"file:///"))
                {
                    singleSignal += 1;
                }

                if(g_str_has_prefix(mount_uri,"burn:///") || g_str_has_prefix(mount_uri,"cdda://"))
                    cdSignal += 1;
                g_object_unref(volume);
                g_object_unref(root);
                g_free(mount_uri);
            }
        }
        g_list_free(listVolumes);
        ui->centralWidget->setFixedSize(280,hign);
        qDebug()<<__FUNCTION__<<"---------------window height:"<<hign;

        if(cdSignal)
        {
            switch(g_list_length(g_drive_get_volumes(cacheDrive)))
            {
                case 1:
                {
                    num++;
                    //when the drive's volume number is 1
                    /*determine whether the drive is only one and whether if the drive is the fisrst one,
                    *if the answer is yes,we set the last parameter is 1.*/
                    /*if(findGDriveList()->size() == 1)
                    {
                        newarea(1,cacheDrive,g_drive_get_name(cacheDrive),
                                g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),0)),
                                NULL,NULL,NULL, 1,NULL,NULL,NULL, "burn:///",NULL,NULL,NULL,1);
                    }*/
                    if(num == 1)
                    {
                        newarea(1,cacheDrive,NULL,g_drive_get_name(cacheDrive),
                                g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),0)),
                                NULL,NULL,NULL, 1,NULL,NULL,NULL, "burn:///",NULL,NULL,NULL,1);
                    }
                    else
                    {
                        newarea(1,cacheDrive,NULL,g_drive_get_name(cacheDrive),
                                g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),0)),
                                NULL,NULL,NULL,1,NULL,NULL,NULL, "burn:///",NULL,NULL,NULL, 2);
                    }
                    break;
                }
                case 2:
                {
                    num++;
                    if(num == 1)
                    {
                        newarea(1,cacheDrive,NULL,g_drive_get_name(cacheDrive),
                                g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),0)),
                                NULL,NULL,NULL, 1,NULL,NULL,NULL, "burn:///",NULL,NULL,NULL,1);
                    }
                    else
                    {
                        newarea(1,cacheDrive,NULL,g_drive_get_name(cacheDrive),
                                g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),0)),
                                NULL,NULL,NULL,1,NULL,NULL,NULL, "burn:///",NULL,NULL,NULL, 2);
                    }
                    break;
                }
                default :
                break;
            }
        }
        else
        {
            GList *volumeNumber = g_drive_get_volumes(cacheDrive);
            int DisNum = g_list_length(volumeNumber);
            driveMountNum = 0;

            if(singleSignal !=0 )
            {
                if (DisNum >0)
                {
                    if (g_drive_can_eject(cacheDrive) || g_drive_can_stop(cacheDrive))
                    {
                        /*
                            * to get the U disk partition's path,name,capacity and U disk's name,then we layout by the
                            * number of its volume
                            * */

                        //when the drive's volume number is 1
                        if(DisNum == 1)
                        {
                            num++;
                            char *driveName = g_drive_get_name(cacheDrive);
                            GVolume *element = (GVolume *)g_list_nth_data(volumeNumber,0);
                            char *volumeName = g_volume_get_name(element);
                            QString apiName = QString(volumeName);
                            char *deviceName = g_volume_get_identifier(element,G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
                            if(deviceName){
                                QString unixDeviceName = QString(deviceName);
                                handleVolumeLabelForFat32Me(apiName,unixDeviceName);
                                g_free(deviceName);
                            }
                            GFile *fileRoot = g_mount_get_root(g_volume_get_mount(element));
                            UDiskPathDis1 = g_file_get_path(fileRoot);
                            GFile *file = g_file_new_for_path(UDiskPathDis1);
                            GFileInfo *info = g_file_query_filesystem_info(file,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE,nullptr,nullptr);
                            totalDis1 = g_file_info_get_attribute_uint64(info,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);
                            /*new show u disk in next code*/
                            root = g_mount_get_default_location(g_volume_get_mount(volume));
                            mount_uri = g_file_get_uri(root);


                            //when the drive's volume number is 1
                            /*determine whether the drive is only one and whether if the drive is the fisrst one,
                            *if the answer is yes,we set the last parameter is 1.*/
                            if(num == 1)
                            {
                                newarea(DisNum,cacheDrive,NULL,driveName,
                                        apiName,
                                        NULL,NULL,NULL, totalDis1,NULL,NULL,NULL, QString(mount_uri),NULL,NULL,NULL,1);
                            }
                            else
                            {
                                newarea(DisNum,cacheDrive,NULL,driveName,
                                        apiName,
                                        NULL,NULL,NULL, totalDis1,NULL,NULL,NULL, QString(mount_uri),NULL,NULL,NULL,2);
                            }

                            g_object_unref(info);
                            g_free(driveName);
                            g_free(volumeName);
                            g_object_unref(element);
                            g_free(UDiskPathDis1);
                            g_object_unref(file);
                            g_object_unref(root);
                            g_free(mount_uri);
                        }
                        //when the drive's volume number is 2
                        if(DisNum == 2)
                        {
                            num++;
                            char *driveName = g_drive_get_name(cacheDrive);
                            GVolume *element1 = (GVolume *)g_list_nth_data(volumeNumber,0);
                            char *volumeName1 = g_volume_get_name(element1);
                            QString apiName1 = QString(volumeName1);
                            char *deviceName1 = g_volume_get_identifier(element1,G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
                            if(deviceName1){
                                QString unixDeviceName = QString(deviceName1);
                                handleVolumeLabelForFat32Me(apiName1,unixDeviceName);
                                g_free(deviceName1);
                            }
                            GFile *fileRoot1 = g_mount_get_root(g_volume_get_mount(element1));
                            UDiskPathDis1 = g_file_get_path(fileRoot1);
                            GFile *file1 = g_file_new_for_path(UDiskPathDis1);
                            GFileInfo *info1 = g_file_query_filesystem_info(file1,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE,nullptr,nullptr);
                            totalDis1 = g_file_info_get_attribute_uint64(info1,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);
                            root = g_mount_get_default_location(g_volume_get_mount(element1));
                            mount_uri = g_file_get_uri(root);


                            GVolume *element2 = (GVolume *)g_list_nth_data(volumeNumber,1);
                            char *volumeName2 = g_volume_get_name(element2);
                            QString apiName2 = QString(volumeName2);
                            char *deviceName2 = g_volume_get_identifier(element2,G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
                            if(deviceName2){
                                QString unixDeviceName = QString(deviceName2);
                                handleVolumeLabelForFat32Me(apiName2,unixDeviceName);
                                g_free(deviceName2);
                            }
                            GFile *fileRoot2 = g_mount_get_root(g_volume_get_mount(element2));
                            UDiskPathDis2 = g_file_get_path(fileRoot2);
                            GFile *file2 = g_file_new_for_path(UDiskPathDis2);
                            GFileInfo *info2 = g_file_query_filesystem_info(file2,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE,nullptr,nullptr);
                            totalDis2 = g_file_info_get_attribute_uint64(info2,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);
                            rootSecond = g_mount_get_default_location(g_volume_get_mount(element2));
                            mount_uriSecond = g_file_get_uri(rootSecond);

                            //when the drive's volume number is 1
                            /*determine whether the drive is only one and whether if the drive is the fisrst one,
                                *if the answer is yes,we set the last parameter is 1.*/
                            if(num == 1)
                            {
                                newarea(DisNum,cacheDrive,NULL,driveName,
                                        apiName1,
                                        apiName2,
                                        NULL,NULL, totalDis1,totalDis2,NULL,NULL, QString(mount_uri),QString(mount_uriSecond),NULL,NULL,1);
                            }
                            else
                            {
                                newarea(DisNum,cacheDrive,NULL,driveName,
                                        apiName1,
                                        apiName2,
                                        NULL,NULL, totalDis1,totalDis2,NULL,NULL, QString(mount_uri),QString(mount_uriSecond),NULL,NULL,2);
                            }

                            g_free(driveName);
                            g_free(volumeName1);
                            g_free(volumeName2);
                            g_object_unref(element1);
                            g_object_unref(element2);
                            g_free(UDiskPathDis1);
                            g_free(UDiskPathDis2);
                            g_object_unref(file1);
                            g_object_unref(file2);
                            g_object_unref(root);
                            g_free(mount_uri);
                            g_object_unref(rootSecond);
                            g_free(mount_uriSecond);
                        }
                        //when the drive's volume number is 3
                        if(DisNum == 3)
                        {
                            num++;
                            UDiskPathDis1 = g_file_get_path(g_mount_get_root(g_volume_get_mount((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),0))));
        //                        QByteArray dateDis1 = UDiskPathDis1.toLocal8Bit();
        //                        char *p_ChangeDis1 = dateDis1.data();
                            GFile *fileDis1 = g_file_new_for_path(UDiskPathDis1);
                            GFileInfo *infoDis1 = g_file_query_filesystem_info(fileDis1,"*",nullptr,nullptr);
                            totalDis1 = g_file_info_get_attribute_uint64(infoDis1,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);

                            UDiskPathDis2 = g_file_get_path(g_mount_get_root(g_volume_get_mount((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),1))));
        //                        QByteArray dateDis2 = UDiskPathDis2.toLocal8Bit();
        //                        char *p_ChangeDis2 = dateDis2.data();
                            GFile *fileDis2 = g_file_new_for_path(UDiskPathDis2);
                            GFileInfo *infoDis2 = g_file_query_filesystem_info(fileDis2,"*",nullptr,nullptr);
                            totalDis2 = g_file_info_get_attribute_uint64(infoDis2,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);

                            UDiskPathDis3 = g_file_get_path(g_mount_get_root(g_volume_get_mount((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),2))));
        //                        QByteArray dateDis3 = UDiskPathDis3.toLocal8Bit();
        //                        char *p_ChangeDis3 = dateDis3.data();
                            GFile *fileDis3 = g_file_new_for_path(UDiskPathDis3);
                            GFileInfo *infoDis3 = g_file_query_filesystem_info(fileDis3,"*",nullptr,nullptr);
                            totalDis3 = g_file_info_get_attribute_uint64(infoDis3,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);
                            if(findGDriveList()->size() == 1)
                            {
                                newarea(DisNum,cacheDrive,NULL,g_drive_get_name(cacheDrive),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),0)),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),1)),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),2)),
                                        NULL, totalDis1,totalDis2,totalDis3,NULL, QString(UDiskPathDis1),QString(UDiskPathDis2),QString(UDiskPathDis3),NULL,1);
                            }

                            else if(num == 1)
                            {
                                newarea(DisNum,cacheDrive,NULL,g_drive_get_name(cacheDrive),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),0)),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),1)),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),2)),
                                        NULL, totalDis1,totalDis2,totalDis3,NULL, QString(UDiskPathDis1),QString(UDiskPathDis2),QString(UDiskPathDis3),NULL,1);
                            }

                            else
                            {
                                newarea(DisNum,cacheDrive,NULL,g_drive_get_name(cacheDrive),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),0)),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),1)),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),2)),
                                        NULL, totalDis1,totalDis2,totalDis3,NULL, QString(UDiskPathDis1),QString(UDiskPathDis2),QString(UDiskPathDis3),NULL,2);
                            }

                        }
                        //when the drive's volume number is 4
                        if(DisNum == 4)
                        {
                            num++;
                            UDiskPathDis1 = g_file_get_path(g_mount_get_root(g_volume_get_mount((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),0))));
                            GFile *fileDis1 = g_file_new_for_path(UDiskPathDis1);
                            GFileInfo *infoDis1 = g_file_query_filesystem_info(fileDis1,"*",nullptr,nullptr);
                            totalDis1 = g_file_info_get_attribute_uint64(infoDis1,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);

                            UDiskPathDis2 = g_file_get_path(g_mount_get_root(g_volume_get_mount((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),1))));
                            GFile *fileDis2 = g_file_new_for_path(UDiskPathDis2);
                            GFileInfo *infoDis2 = g_file_query_filesystem_info(fileDis2,"*",nullptr,nullptr);
                            totalDis2 = g_file_info_get_attribute_uint64(infoDis2,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);

                            UDiskPathDis3 = g_file_get_path(g_mount_get_root(g_volume_get_mount((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),2))));
                            GFile *fileDis3 = g_file_new_for_path(UDiskPathDis3);
                            GFileInfo *infoDis3 = g_file_query_filesystem_info(fileDis3,"*",nullptr,nullptr);
                            totalDis3 = g_file_info_get_attribute_uint64(infoDis3,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);

                            UDiskPathDis4 = g_file_get_path(g_mount_get_root(g_volume_get_mount((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),3))));
                            GFile *fileDis4 = g_file_new_for_path(UDiskPathDis4);
                            GFileInfo *infoDis4 = g_file_query_filesystem_info(fileDis4,"*",nullptr,nullptr);
                            totalDis4 = g_file_info_get_attribute_uint64(infoDis4,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);

                            if(findGDriveList()->size() == 1)
                            {
                                newarea(DisNum,cacheDrive,NULL,g_drive_get_name(cacheDrive),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),0)),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),1)),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),2)),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),3)),
                                        totalDis1,totalDis2,totalDis3,totalDis4, QString(UDiskPathDis1),QString(UDiskPathDis2),QString(UDiskPathDis3),QString(UDiskPathDis4),1);
                            }

                            else if(num == 1)
                            {
                                newarea(DisNum,cacheDrive,NULL,g_drive_get_name(cacheDrive),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),0)),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),1)),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),2)),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),3)),
                                        totalDis1,totalDis2,totalDis3,totalDis4, UDiskPathDis1,UDiskPathDis2,UDiskPathDis3,UDiskPathDis4,1);
                            }

                            else
                            {
                                newarea(DisNum,cacheDrive,NULL,g_drive_get_name(cacheDrive),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),0)),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),1)),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),2)),
                                        g_volume_get_name((GVolume *)g_list_nth_data(g_drive_get_volumes(cacheDrive),3)),
                                        totalDis1,totalDis2,totalDis3,totalDis4, UDiskPathDis1,UDiskPathDis2,UDiskPathDis3,UDiskPathDis4,2);
                            }

                        }
                    }
                }
            }
        }
    }
    if(insertorclick == false && findTeleGVolumeList()->size() >= 1)
    {
        qDebug()<<"findTeleVolume"<<findTeleGVolumeList()->size();
        for(auto cacheVolume:*findGVolumeList())
        {
            num++;
            hign = findGMountList()->size() *40 + findGDriveList()->size() *55 + findTeleGMountList()->size() *90;
            ui->centralWidget->setFixedSize(280,hign);
            qDebug()<<__FUNCTION__<<"---------------window height1:"<<hign;
//                  if(g_volume_get_drive(cacheVolume) == NULL)
//                  {
//                  qDebug()<<"has no drive device";
//                  root = g_mount_get_default_location(g_volume_get_mount(cacheVolume));
//                  mount_uri = g_file_get_uri(root);
//                  }
//                  qDebug()<<"telephone mount uri"<<mount_uri;
            root = g_mount_get_default_location(g_volume_get_mount(cacheVolume));
            mount_uri = g_file_get_uri(root);
            if(g_str_has_prefix(mount_uri,"mtp://") || g_str_has_prefix(mount_uri,"gphoto2://"))
            {
                GFile *fileRoot = g_mount_get_root(g_volume_get_mount(cacheVolume));
                UDiskPathDis1 = g_file_get_path(fileRoot);
                GFile *file = g_file_new_for_path(UDiskPathDis1);
                GFileInfo *info = g_file_query_filesystem_info(file,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE,nullptr,nullptr);
                totalDis1 = g_file_info_get_attribute_uint64(info,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);
                QString telephoneName = tr("telephone device");
//                      QByteArray strTelePhone = telephoneName.toLatin1();  注意这里的类型转换需要调toLocal8Bit不能用toLatin1，否则会出现中文乱码。
                QByteArray strTelePhone = telephoneName.toLocal8Bit();
                char *realTele = strTelePhone.data();

                if(num == 1)
                {
                    newarea(1,NULL,cacheVolume,realTele,
                            g_volume_get_name(cacheVolume),
                            NULL,NULL,NULL,totalDis1,NULL,NULL,NULL, QString(UDiskPathDis1),NULL,NULL,NULL,1);
                }
                else
                {
                    newarea(1,NULL,cacheVolume,realTele,
                            g_volume_get_name(cacheVolume),
                            NULL,NULL,NULL,totalDis1,NULL,NULL,NULL, QString(UDiskPathDis1),NULL,NULL,NULL,2);
                }

                g_free(g_volume_get_name(cacheVolume));
                g_object_unref(file);
                g_free(UDiskPathDis1);
                g_object_unref(fileRoot);
            }
            else
            {
                qDebug()<<"---telephone not exist";
            }
        }
    }

    ui->centralWidget->show();
    moveBottomNoBase();
}

void MainWindow::ifgetPinitMount()
{
    int pointMountNum = 0;
    QFile file(tmpPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QString content = file.readLine().trimmed();
        while (!file.atEnd())
        {
            if (content.contains(".mount"))
                pointMountNum += 1;
                content = file.readLine().trimmed();
                if(pointMountNum >= 1)
                    findPointMount = true;
                else
                    findPointMount = false;
        }
    }
    file.close();
}

void MainWindow::GpartedStartedWindowShow()
{
    num = 0;
    QList<QClickWidget *> listMainWindow = this->findChildren<QClickWidget *>();
    for(QClickWidget *listItem:listMainWindow)
    {
        listItem->deleteLater();
    }

    QList<QWidget *> listLine = this->findChildren<QWidget *>();
    for(QWidget *listItem:listLine)
    {
        if(listItem->objectName() == "lineWidget")
          listItem->deleteLater();
    }
    this->getTransparentData();
    qDebug()<<"findGMountList.size"<<findGMountList()->size();
    qDebug()<<"findGVolumeList.size"<<findGVolumeList()->size();
    qDebug()<<"findTeleGVolumeList.size"<<findTeleGVolumeList()->size();
    qDebug()<<"findGDriveList.size"<<findGDriveList()->size();
    QString strTrans;
    strTrans =  QString::number(m_transparency, 10, 2);
#if (QT_VERSION < QT_VERSION_CHECK(5,7,0))
    QString convertStyle = "#centralWidget{background:rgba(19,19,20,0.95);}";
#else
//    QString convertStyle = "#centralWidget{background:rgba(19,19,20," + strTrans + ");}";
#endif
    qDebug()<<deviceMap.size()<<"-size-";

    for(it = deviceMap.begin(); it != deviceMap.end(); ++it)
    {
        num++;
//        int DisNum = g_list_length(g_drive_get_volumes(it.key()));
        hign = findGMountList()->size() * 100;
        ui->centralWidget->setFixedSize(280,hign);
        qDebug()<<__FUNCTION__<<"---------------window height2:"<<hign;
//        if(DisNum == 1)
//        {
            char *driveName = g_drive_get_name(it.key());
            qDebug()<<"driveName"<<driveName;
            GMount *element = it.value()[0];
            char *volumeName = g_mount_get_name(element);
            QString apiName = QString(volumeName);
//            char *deviceName = g_volume_get_identifier(element,G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
//            if(deviceName){
//                QString unixDeviceName = QString(deviceName);
//                handleVolumeLabelForFat32Me(apiName,unixDeviceName);
//                g_free(deviceName);
//            }
            GFile *fileRoot = g_mount_get_root(element);
            UDiskPathDis1 = g_file_get_path(fileRoot);
            GFile *file = g_file_new_for_path(UDiskPathDis1);
            GFileInfo *info = g_file_query_filesystem_info(file,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE,nullptr,nullptr);
            totalDis1 = g_file_info_get_attribute_uint64(info,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);
            //when the drive's volume number is 1
            /*determine whether the drive is only one and whether if the drive is the fisrst one,
             *if the answer is yes,we set the last parameter is 1.*/
            if(num == 1)
            {
                newarea(1,it.key(),NULL,driveName,
                        apiName,
                        NULL,NULL,NULL, totalDis1,NULL,NULL,NULL, QString(UDiskPathDis1),NULL,NULL,NULL,1);
            }
            else
            {
                newarea(1,it.key(),NULL,driveName,
                        apiName,
                        NULL,NULL,NULL, totalDis1,NULL,NULL,NULL, QString(UDiskPathDis1),NULL,NULL,NULL,2);
            }

            g_free(driveName);
            g_free(volumeName);
            g_object_unref(element);
            g_free(UDiskPathDis1);
            g_object_unref(file);
//        }
//        if(DisNum == 2)
//        {
//            num++;
//            char *driveName = g_drive_get_name(it.key());
//            GVolume *element1 = it.value()[0];
//            char *volumeName1 = g_volume_get_name(element1);
//            QString apiName1 = QString(volumeName1);
//            char *deviceName1 = g_volume_get_identifier(element1,G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
//            if(deviceName1){
//                QString unixDeviceName = QString(deviceName1);
//                handleVolumeLabelForFat32Me(apiName1,unixDeviceName);
//                g_free(deviceName1);
//            }
//            GFile *fileRoot1 = g_mount_get_root(g_volume_get_mount(element1));
//            UDiskPathDis1 = g_file_get_path(fileRoot1);
//            GFile *file1 = g_file_new_for_path(UDiskPathDis1);
//            GFileInfo *info1 = g_file_query_filesystem_info(file1,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE,nullptr,nullptr);
//            totalDis1 = g_file_info_get_attribute_uint64(info1,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);

//            GVolume *element2 = it.value()[1];
//            char *volumeName2 = g_volume_get_name(element2);
//            QString apiName2 = QString(volumeName2);
//            char *deviceName2 = g_volume_get_identifier(element2,G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
//            if(deviceName2){
//                QString unixDeviceName = QString(deviceName2);
//                handleVolumeLabelForFat32Me(apiName2,unixDeviceName);
//                g_free(deviceName2);
//            }
//            GFile *fileRoot2 = g_mount_get_root(g_volume_get_mount(element2));
//            UDiskPathDis2 = g_file_get_path(fileRoot2);
//            GFile *file2 = g_file_new_for_path(UDiskPathDis2);
//            GFileInfo *info2 = g_file_query_filesystem_info(file2,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE,nullptr,nullptr);
//            totalDis2 = g_file_info_get_attribute_uint64(info2,G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);
//            //when the drive's volume number is 1
//            /*determine whether the drive is only one and whether if the drive is the fisrst one,
//             *if the answer is yes,we set the last parameter is 1.*/
//            if(num == 1)
//            {
//                newarea(DisNum,it.key(),NULL,driveName,
//                        apiName1,
//                        apiName2,
//                        NULL,NULL, totalDis1,totalDis2,NULL,NULL, QString(UDiskPathDis1),QString(UDiskPathDis2),NULL,NULL,1);
//            }
//            else
//            {
//                newarea(DisNum,it.key(),NULL,driveName,
//                        apiName1,
//                        apiName2,
//                        NULL,NULL, totalDis1,totalDis2,NULL,NULL, QString(UDiskPathDis1),QString(UDiskPathDis2),NULL,NULL,2);
//            }

//            g_free(driveName);
//            g_free(volumeName1);
//            g_free(volumeName2);
//            g_object_unref(element1);
//            g_object_unref(element2);
//            g_free(UDiskPathDis1);
//            g_free(UDiskPathDis2);
//            g_object_unref(file1);
//            g_object_unref(file2);
//        }
    }
    moveBottomNoBase();
    ui->centralWidget->show();
}

void MainWindow::on_Maininterface_hide()
{
    ui->centralWidget->hide();
    this->driveVolumeNum = 0;
    interfaceHideTime->stop();
}

void MainWindow::moveBottomNoBase()
{
//    screen->availableGeometry();
//    screen->availableSize();
//    if(screen->availableGeometry().x() == screen->availableGeometry().y() && screen->availableSize().height() < screen->size().height())
//    {
//        qDebug()<<"the positon of panel is down";
//        this->move(screen->availableGeometry().x() + screen->size().width() -
//                   this->width() - DistanceToPanel,screen->availableGeometry().y() +
//                   screen->availableSize().height() - this->height() - DistanceToPanel);
//    }

//    if(screen->availableGeometry().x() < screen->availableGeometry().y() && screen->availableSize().height() < screen->size().height())
//    {
//        qDebug()<<"this position of panel is up";
//        this->move(screen->availableGeometry().x() + screen->size().width() -
//                   this->width() - DistanceToPanel,screen->availableGeometry().y());
//    }

//    if(screen->availableGeometry().x() > screen->availableGeometry().y() && screen->availableSize().width() < screen->size().width())
//    {
//        qDebug()<<"this position of panel is left";
//        this->move(screen->availableGeometry().x() + DistanceToPanel,screen->availableGeometry().y()
//                   + screen->availableSize().height() - this->height());
//    }

//    if(screen->availableGeometry().x() == screen->availableGeometry().y() && screen->availableSize().width() < screen->size().width())
//    {
//        qDebug()<<"this position of panel is right";
//        this->move(screen->availableGeometry().x() + screen->availableSize().width() -
//                   DistanceToPanel - this->width(),screen->availableGeometry().y() +
//                   screen->availableSize().height() - (this->height())*(DistanceToPanel - 1));
//    }
    int position=0;
    int panelSize=0;
    if(QGSettings::isSchemaInstalled(QString("org.ukui.panel.settings").toLocal8Bit()))
    {
        QGSettings* gsetting=new QGSettings(QString("org.ukui.panel.settings").toLocal8Bit());
        if(gsetting->keys().contains(QString("panelposition")))
            position=gsetting->get("panelposition").toInt();
        else
            position=0;
        if(gsetting->keys().contains(QString("panelsize")))
            panelSize=gsetting->get("panelsize").toInt();
        else
            panelSize=SmallPanelSize;
    }
    else
    {
        position=0;
        panelSize=SmallPanelSize;
    }

    int x=QApplication::primaryScreen()->geometry().x();
    int y=QApplication::primaryScreen()->geometry().y();

    if(position==0)
        ui->centralWidget->setGeometry(QRect(x + QApplication::primaryScreen()->geometry().width()-ui->centralWidget->width() - DISTANCEMEND - DISTANCEPADDING,y+ QApplication::primaryScreen()->geometry().height()-panelSize-ui->centralWidget->height() - DISTANCEPADDING,ui->centralWidget->width(),ui->centralWidget->height()));
    else if(position==1)
        ui->centralWidget->setGeometry(QRect(x + QApplication::primaryScreen()->geometry().width()-ui->centralWidget->width() - DISTANCEMEND - DISTANCEPADDING,y+ panelSize + DISTANCEPADDING,ui->centralWidget->width(),ui->centralWidget->height()));  // Style::minw,Style::minh the width and the height of the interface  which you want to show
    else if(position==2)
        ui->centralWidget->setGeometry(QRect(x+panelSize + DISTANCEPADDING,y + QApplication::primaryScreen()->geometry().height() - ui->centralWidget->height() - DISTANCEPADDING,ui->centralWidget->width(),ui->centralWidget->height()));
    else
        ui->centralWidget->setGeometry(QRect(x+QApplication::primaryScreen()->geometry().width()-panelSize-ui->centralWidget->width() - DISTANCEPADDING,y + QApplication::primaryScreen()->geometry().height() - ui->centralWidget->height() - DISTANCEPADDING,ui->centralWidget->width(),ui->centralWidget->height()));
}

/*
 * determine how to open the maininterface,if trigger is 0,the main interface show when we inset USB
 * device directly,if trigger is 1,we show main interface by clicking the systray icon.
*/
bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    #if 0
    if(triggerType == 0)
    {
        if(event->type() == QEvent::Enter)
        {
            disconnect(interfaceHideTime, SIGNAL(timeout()), this, SLOT(on_Maininterface_hide()));
            this->show();
        }

        if(event->type() == QEvent::Leave)
        {
            connect(interfaceHideTime, SIGNAL(timeout()), this, SLOT(on_Maininterface_hide()));
            interfaceHideTime->start(2000);
        }
    }

    if(triggerType == 1){}

    if (obj == this)
    {
        if (event->type() == QEvent::WindowDeactivate && !(this->isHidden()))
        {
            this->hide();
            return true;
        }
    }
    if (!isActiveWindow())
    {
        activateWindow();
    }
    #endif
    return false;
}

//new a gsettings object to get the information of the opacity of the window
void MainWindow::initTransparentState()
{
    const QByteArray idtrans(THEME_QT_TRANS);

    if(QGSettings::isSchemaInstalled(idtrans))
    {
        m_transparency_gsettings = new QGSettings(idtrans);
    }
}

//use gsettings to get the opacity
void MainWindow::getTransparentData()
{
    if (!m_transparency_gsettings)
    {
       m_transparency = 0.95;
       return;
    }

    QStringList keys = m_transparency_gsettings->keys();
    if (keys.contains("transparency"))
    { 
        m_transparency = m_transparency_gsettings->get("transparency").toDouble();
    }
}


void MainWindow::paintEvent(QPaintEvent *event)
{
//    QStyleOption opt;
//    opt.init(this);
//    QPainter p(this);
//    QRect rect = this->rect();
//    p.setRenderHint(QPainter::Antialiasing);  // 反锯齿;
//    p.setBrush(opt.palette.color(QPalette::Base));
//    p.setOpacity(m_transparency);
//    p.setPen(Qt::NoPen);
//    p.drawRoundedRect(rect, 12, 12);
//    QWidget::paintEvent(event);

//    double trans = objKyDBus->getTransparentData();

//    QStyleOption opt;
//    opt.init(this);
//    QPainter p(this);
//    QRect rect = this->rect();
//    p.setRenderHint(QPainter::Antialiasing);  // 反锯齿;
//    p.setBrush(opt.palette.color(QPalette::Base));
//    p.setOpacity(m_transparency);
//    p.setPen(Qt::NoPen);
//    p.drawRoundedRect(rect, 6, 6);
//    QWidget::paintEvent(event);

//    KWindowEffects::enableBlurBehind(this->winId(), true, QRegion(path.toFillPolygon().toPolygon()));

    QPainterPath path;
    auto rect = this->rect();
    rect.adjust(1, 1, -1, -1);
    path.addRoundedRect(rect, 4, 4);
    setProperty("blurRegion", QRegion(path.toFillPolygon().toPolygon()));

    QStyleOption opt;
    opt.init(this);
    QPainter p(this);
    QRect rectReal = this->rect();
    p.setRenderHint(QPainter::Antialiasing);  // 反锯齿;
    p.setBrush(opt.palette.color(QPalette::Base));
    p.setOpacity(m_transparency);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rectReal, 4, 4);
    QWidget::paintEvent(event);

    KWindowEffects::enableBlurBehind(this->winId(), true, QRegion(path.toFillPolygon().toPolygon()));
}

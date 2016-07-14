/**********************************************************************************************
    Copyright (C) 2014 Oliver Eichler oliver.eichler@gmx.de

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

**********************************************************************************************/

#include "CMainWindow.h"
#include "canvas/CCanvas.h"
#include "helpers/CDraw.h"
#include "helpers/CSettings.h"
#include "map/CMapDraw.h"
#include "map/CMapItem.h"
#include "map/CMapList.h"
#include "map/CMapPathSetup.h"
#include "map/IMap.h"
#include "map/cache/CDiskCache.h"
#include "setup/IAppSetup.h"

#include <QtGui>
#include <QtWidgets>



QList<CMapDraw*> CMapDraw::maps;
QString CMapDraw::cachePath = "";
QStringList CMapDraw::mapPaths;
QStringList CMapDraw::supportedFormats = QString("*.vrt|*.jnx|*.img|*.rmap|*.wmts|*.tms|*.gemf").split('|');


CMapDraw::CMapDraw(CCanvas *parent)
    : IDrawContext("map", CCanvas::eRedrawMap, parent)
{
    mapList = new CMapList(canvas);
    CMainWindow::self().addMapList(mapList, canvas->objectName());
    connect(canvas,  &CCanvas::destroyed,   mapList, &CMapList::deleteLater);
    connect(mapList, &CMapList::sigChanged, this,    &CMapDraw::emitSigCanvasUpdate);

    buildMapList();

    maps << this;
}

CMapDraw::~CMapDraw()
{
    maps.removeOne(this);
}

void CMapDraw::setProjection(const QString& proj) /* override */
{
    // --- save the active maps
    QStringList keys;
    saveActiveMapsList(keys);
    // --- now set the new projection
    IDrawContext::setProjection(proj);
    // --- now build the map list from scratch. This will deactivate -> activate all maps
    //     By that everything is restored with the new projection
    buildMapList();
    restoreActiveMapsList(keys);
}

void CMapDraw::setupMapPath()
{
    QStringList paths = mapPaths;
    if(cachePath.isEmpty())
    {
        cachePath =  IAppSetup::getPlatformInstance()->defaultCachePath();
    }
    CMapPathSetup dlg(paths, cachePath);
    if(dlg.exec() != QDialog::Accepted)
    {
        return;
    }

    setupMapPath(paths);
}

void CMapDraw::setupMapPath(const QString &path)
{
    if(mapPaths.contains(path))
    {
        return;
    }

    QStringList paths(mapPaths);
    paths << path;
    setupMapPath(paths);
}

void CMapDraw::setupMapPath(const QStringList& paths)
{
    mapPaths = paths;

    for(CMapDraw * map : maps)
    {
        QStringList keys;
        map->saveActiveMapsList(keys);
        map->buildMapList();
        map->restoreActiveMapsList(keys);
    }
}

void CMapDraw::saveMapPath(QSettings& cfg)
{
    cfg.setValue("mapPath", mapPaths);
    cfg.setValue("cachePath", cachePath);
}

void CMapDraw::loadMapPath(QSettings& cfg)
{
    mapPaths  = cfg.value("mapPath", mapPaths).toStringList();
    cachePath = cfg.value("cachePath", cachePath).toString();

    if(cachePath.isEmpty())
    {
        cachePath = IAppSetup::getPlatformInstance()->defaultCachePath();
    }
}



void CMapDraw::getInfo(const QPoint& px, QString& str)
{
    if(isRunning())
    {
        return;
    }
    CMapItem::mutexActiveMaps.lock();
    if(mapList)
    {
        for(int i = 0; i < mapList->count(); i++)
        {
            CMapItem * item = mapList->item(i);

            if(!item || item->mapfile.isNull())
            {
                // as all active maps have to be at the top of the list
                // it is ok to break ass soon as the first map with no
                // active files is hit.
                break;
            }

            item->mapfile->getInfo(px, str);
        }
    }
    CMapItem::mutexActiveMaps.unlock();
}

void CMapDraw::getToolTip(const QPoint& px, QString& str)
{
    if(isRunning())
    {
        return;
    }
    CMapItem::mutexActiveMaps.lock();
    if(mapList)
    {
        for(int i = 0; i < mapList->count(); i++)
        {
            CMapItem * item = mapList->item(i);

            if(!item || item->mapfile.isNull())
            {
                // as all active maps have to be at the top of the list
                // it is ok to break ass soon as the first map with no
                // active files is hit.
                break;
            }

            item->mapfile->getToolTip(px, str);
        }
    }
    CMapItem::mutexActiveMaps.unlock();
}

bool CMapDraw::findPolylineCloseBy(const QPointF& pt1, const QPointF& pt2, qint32 threshold, QPolygonF& polyline)
{
    if(isRunning())
    {
        return false;
    }
    bool res = false;
    CMapItem::mutexActiveMaps.lock();
    if(mapList)
    {
        for(int i = 0; i < mapList->count(); i++)
        {
            CMapItem * item = mapList->item(i);

            if(!item || item->mapfile.isNull())
            {
                // as all active maps have to be at the top of the list
                // it is ok to break ass soon as the first map with no
                // active files is hit.
                break;
            }

            res = item->mapfile->findPolylineCloseBy(pt1, pt2, threshold, polyline);
            if(res)
            {
                break;
            }
        }
    }
    CMapItem::mutexActiveMaps.unlock();
    return res;
}

void CMapDraw::saveConfig(QSettings& cfg) /* override */
{
    // store group context for later use
    cfgGroup = cfg.group();
    // -------------------
    QStringList keys;
    cfg.beginGroup("map");
    saveActiveMapsList(keys, cfg);
    cfg.setValue("active", keys);
    cfg.setValue("zoomIndex", zoomIndex);
    cfg.endGroup();
}

void CMapDraw::loadConfig(QSettings& cfg) /* override */
{
    // store group context for later use
    cfgGroup = cfg.group();
    // -------------------
    cfg.beginGroup("map");
    if(cfgGroup.isEmpty())
    {
        restoreActiveMapsList(cfg.value("active", "").toStringList(), cfg);
    }
    else
    {
        restoreActiveMapsList(cfg.value("active", "").toStringList());
    }
    int idx = cfg.value("zoomIndex",zoomIndex).toInt();
    cfg.endGroup();

    zoom(idx);
}



void CMapDraw::buildMapList()
{
    QCryptographicHash md5(QCryptographicHash::Md5);

    QMutexLocker lock(&CMapItem::mutexActiveMaps);
    mapList->clear();

    QSet<QString> maps;

    for(const QString &path : mapPaths)
    {
        QDir dir(path);

        // find available maps
        for(const QString &filename : dir.entryList(supportedFormats, QDir::Files|QDir::Readable, QDir::Name))
        {
            QFileInfo fi(filename);

            CMapItem * item = new CMapItem(*mapList, this);

            maps.insert(fi.baseName());

            item->setText(0, fi.baseName().replace("_", " "));
            item->filename = dir.absoluteFilePath(filename);
            item->updateIcon();

            // calculate MD5 hash from the file's first 1024 bytes
            QFile f(dir.absoluteFilePath(filename));
            f.open(QIODevice::ReadOnly);
            md5.reset();
            md5.addData(f.read(1024));
            item->key = md5.result().toHex();
            f.close();
        }
    }

    CDiskCache::cleanupRemovedMaps(maps);

    mapList->updateHelpText();
}

void CMapDraw::saveActiveMapsList(QStringList& keys)
{
    SETTINGS;
    cfg.beginGroup(cfgGroup);
    cfg.beginGroup("map");
    saveActiveMapsList(keys, cfg);
    cfg.endGroup();
    cfg.endGroup();
}

void CMapDraw::saveActiveMapsList(QStringList& keys, QSettings& cfg)
{
    QMutexLocker lock(&CMapItem::mutexActiveMaps);

    for(int i = 0; i < mapList->count(); i++)
    {
        CMapItem * item = mapList->item(i);
        if(item && !item->mapfile.isNull())
        {
            item->saveConfig(cfg);
            keys << item->key;
        }
    }
}

void CMapDraw::loadConfigForMapItem(CMapItem * item)
{
    if(cfgGroup.isEmpty())
    {
        return;
    }

    SETTINGS;
    cfg.beginGroup(cfgGroup);
    cfg.beginGroup("map");
    item->loadConfig(cfg);
    cfg.endGroup();
    cfg.endGroup();
}

void CMapDraw::restoreActiveMapsList(const QStringList& keys)
{
    QMutexLocker lock(&CMapItem::mutexActiveMaps);

    for(const QString &key : keys)
    {
        for(int i = 0; i < mapList->count(); i++)
        {
            CMapItem * item = mapList->item(i);

            if(item && item->key == key)
            {
                /**
                    @Note   the item will load it's configuration upon successful activation
                            by calling loadConfigForMapItem().
                 */
                item->activate();
                break;
            }
        }
    }

    mapList->updateHelpText();
}

void CMapDraw::restoreActiveMapsList(const QStringList& keys, QSettings& cfg)
{
    QMutexLocker lock(&CMapItem::mutexActiveMaps);

    for(const QString &key : keys)
    {
        for(int i = 0; i < mapList->count(); i++)
        {
            CMapItem * item = mapList->item(i);

            if(item && item->key == key)
            {
                if(item->activate())
                {
                    item->loadConfig(cfg);
                }
                break;
            }
        }
    }

    mapList->updateHelpText();
}


void CMapDraw::reportStatusToCanvas(const QString& key, const QString& msg)
{
    canvas->reportStatus(key, msg);
}

void CMapDraw::drawt(IDrawContext::buffer_t& currentBuffer) /* override */
{
    // iterate over all active maps and call the draw method
    CMapItem::mutexActiveMaps.lock();
    if(mapList && (mapList->count() != 0))
    {
        for(int i = 0; i < mapList->count(); i++)
        {
            CMapItem * item = mapList->item(i);

            if(!item || item->mapfile.isNull())
            {
                // as all active maps have to be at the top of the list
                // it is ok to break ass soon as the first map with no
                // active files is hit.
                break;
            }

            item->mapfile->draw(currentBuffer);
        }
    }
    else
    {
        const int offMargin = currentBuffer.image.size().width()*0.1;
        const int offTop    = currentBuffer.image.size().height()/3;
        QPainter p(&currentBuffer.image);
        p.setPen(Qt::black);
        p.translate(offMargin,offTop);

        QString msg = tr(
            "There are no maps right now. "
            "QMapShack is no fun without maps. "
            "You can install maps by pressing the 'Help! I want maps!' button in the 'Maps' dock window. "
            "Or you can press the F1 key to open the online documentation that tells you how to use QMapShack. "
            "\n\n"
            "If it's no fun, why don't you provide maps? Well to host maps ready for download and installation "
            "requires a good server. And this is not a free service. The project lacks the money. Additionally "
            "map and DEM data has a copyright. Therefore the copyright holder has to be asked prior to package "
            "the data. This is not that easy as it might sound and for some data you have to pay royalties. "
            "The project simply lacks resources to do this. And we think installing the stuff yourself is not "
            "that much to ask from you. After all the software is ditributed without a fee."
            );

        QTextDocument doc;
        doc.setPlainText(msg);
        doc.setTextWidth(currentBuffer.image.width() - offMargin*2);
        doc.drawContents(&p);
    }
    CMapItem::mutexActiveMaps.unlock();
}


